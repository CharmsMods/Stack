# Composite Tab — Web Parity Implementation Guide (C++ / Stack)

This document captures how the **website** Composite tab works today, so the native **Stack** program can re-implement it with threading, save/load, and Editor integration. It is derived from:

- `Important Sources/Whole Site Context.txt` (Composite overview, payload shape, save/load notes)
- `src/composite/document.js`, `src/composite/engine.js`, `src/composite/ui.js`
- `src/app/bootstrap.js` (global state, Library adapters, **Editor bridge**, autosave, navigation)
- `src/workers/tasks/composite.js` + `src/workers/composite.worker.js` (background rendering)

---

## 1. Product role

Composite is a **2D layer compositor**: an infinite world-canvas where users stack **images**, **embedded Editor projects** (flat rasterized previews + full nested JSON), **text**, and **simple vector shapes** (square / circle / triangle), then **export** a PNG or save a **self-contained** `composite-document` JSON. It is intentionally **not** the Editor’s WebGL filter stack; composition is **CPU 2D canvas** (browser) with an optional **worker** path for large exports/previews.

---

## 2. Global app architecture (why Editor and Composite do not clobber each other)

The web app keeps **separate top-level documents** in one store:

| Section   | State field           | Adapter `type` |
|-----------|------------------------|----------------|
| Editor    | `document`             | `studio`       |
| Composite | `compositeDocument`    | `composite`    |
| Stitch    | `stitchDocument`       | `stitch`       |
| 3D        | `threeDDocument`       | `3d`           |

**Opening a Library project** calls `loadLibraryProject` → picks adapter from payload → `ensureProjectCanBeReplaced(adapter.type, …)` → `adapter.restorePayload`. Only the **target** section’s document is replaced; the others remain in memory until the user explicitly starts a new project in that section or loads something else there.

**`ensureProjectCanBeReplaced(projectType, actionLabel)`** (critical for native parity):

1. Uses the **project adapter** for that `projectType` (`composite`, `studio`, etc.).
2. If `adapter.isEmpty(currentDocument)` → allow without prompt.
3. Else computes **fingerprints** of the serialized draft vs Library: if an **exact match** exists in the Library → allow without prompt (already “saved”).
4. Else shows **Save to Library** vs **Skip** vs **Cancel** flow before destructive actions (new project, load another file, open another Library item, open Composite layer in Editor when it would replace unsaved Editor work, etc.).

This is how the site **preserves** an in-progress **Editor** project when you open a **Composite** Library item: Composite load only runs the **composite** adapter guard and then hydrates `compositeDocument`; `document` is untouched unless you switch to Editor and trigger an Editor-specific replace.

---

## 3. Document schema (`composite-document`)

Canonical shape (`normalizeCompositeDocument` in `document.js`):

- **Envelope:** `version: "mns/v2"`, `kind: "composite-document"`, `mode: "composite"`.
- **`workspace`:** `{ sidebarView }` where `sidebarView` ∈ `layers | transform | blend | export` (UI labels: Layers, **Selected**, View, Export).
- **`layers`:** ordered list; stacking uses integer **`z`** (re-sorted on normalize).
- **`selection`:** `{ layerId }` — primary selection for sidebar; the **stage** may additionally track **multi-selection** in UI state (see §7).
- **`view`:** `{ zoom, panX, panY, zoomLocked, showChecker }` — **pan/zoom alone must not trigger Library autosave** on the web app.
- **`export`:** `{ background: "transparent" | "#rrggbb", boundsMode: "auto" | "custom", bounds: {x,y,width,height}, customResolution: {width,height} }`.
- **`preview`:** optional `{ imageData, width, height, updatedAt }` for Library cards / local JSON convenience.

### 3.1 Layer kinds

| `kind`           | Payload fields |
|------------------|----------------|
| `image`          | `imageAsset`: `{ name, type, imageData (data URL), width, height }` |
| `editor-project` | `embeddedEditorDocument`: full **studio** `mns/v2` document subtree + `source` / `preview` as in Editor; `source` (layer-level): `originalLibraryId`, `originalLibraryName`, `originalProjectType`, `generatedFromCompositeImage`, **`renderWidth` / `renderHeight`** (authoritative render size vs decoded preview bitmap) |
| `text`           | `textAsset`: `{ text, fontFamily, fontSize, color }` — fonts align with **`EDITOR_TEXT_FONT_OPTIONS`** (shared with Editor text) |
| `square` / `circle` / `triangle` | `squareAsset` / `circleAsset` / `triangleAsset`: `{ color, width: 2, height: 2 }` base units, scaled on stage |

### 3.2 Transform & appearance (per layer)

- Position: **`x`, `y`** (world space, top-left of unrotated box before rotation centering in engine).
- Scale: **`scale`** (uniform), **`scaleX`, `scaleY`** (non-uniform); minimum/maximum constants `COMPOSITE_MIN_SCALE` … `COMPOSITE_MAX_SCALE`.
- **`rotation`** (radians in JS `canvas.rotate`).
- **`flipX`, `flipY`**.
- **`opacity`** 0–1.
- **`blendMode`:** `normal | multiply | screen | add | overlay | soft-light | hard-light | hue | color` — mapped to canvas `globalCompositeOperation` (see `engine.js` / worker `mapCanvasBlendMode`).
- **`resizeMode`:** `center-uniform | anchor-uniform | anchor-stretch` — **stretch** only allowed for text/shapes (`supportsCompositeStretching`).
- **`visible`, `locked`**, **`name`**, **`id`** (stable string IDs, typically `composite-…` / `layer-…` prefixes).

### 3.3 Non-destructive crop (image + editor-project)

- **`crop`:** `{ left, top, right, bottom }` in **source pixel** space, clamped so inner size ≥ 1.
- Engine samples a sub-rectangle from the decoded bitmap, accounting for mismatch between **metadata** size and actual bitmap dimensions (`bitmapScaleX/Y` in `drawDocumentToCanvas`).

### 3.4 Authoritative size for Editor-backed layers

`getCompositeLayerSourceImage` prefers, for `editor-project` layers:

1. `layer.source.renderWidth` / `renderHeight` if set  
2. else `embeddedEditorDocument.preview` dimensions  
3. else embedded `source` dimensions  

So export and handles stay aligned when preview resolution differs from true render intent.

---

## 4. Rendering engine (`CompositeEngine`)

**Responsibilities:**

1. **`layerAssets`:** `Map<layerId, { sourceKey, width, height, image }>` — decoded images; **cache invalidation** via `sourceKey` (includes editor preview timestamp, render size, text content, etc.).
2. **`syncDocument`:** drop stale layers, `ensureLayerAsset` for each (async `Image` from data URL).
3. **Screen draw:** clear, optional checker (from settings merged into document), iterate visible layers by `z`, apply world → screen transform using **viewport** derived from `view` + container size.
4. **Export draw (`mode === 'export'`)** — uses `export.boundsMode`:
   - **auto:** bounds = union of visible layer **axis-aligned** bounds after rotation (engine `computeDocumentBounds`).
   - **custom:** fixed world rectangle from `export.bounds` + output size from `export.customResolution` (see `computeViewport` in `engine.js`).
5. **`exportPngBlob`:** sized canvas → `drawDocumentToCanvas` → PNG blob.
6. **`capturePreview`:** full export render → downscale to max edge (e.g. 320) → data URL preview.

**C++ parity:** reproduce viewport math (`computeViewport`, `screenToWorld` / `worldToScreen`) and layer draw order; use same blend mapping; support transparent vs solid export background.

---

## 5. Threading & background tasks

The web app registers a **`composite`** background domain (`backgroundTasks.registerDomain`) with worker URL `composite.worker.js`. Tasks in `src/workers/tasks/composite.js` include (non-exhaustive):

- Prepare dropped **image files** off the main thread (`prepare-image-files` pattern in bootstrap).
- **`render-export-png`** / **`render-preview`** style rendering using **`createImageBitmap`** + `OffscreenCanvas` where available.

**Settings** (`settings.composite`): **`exportBackend`:** `auto | worker | main-thread` — `auto` picks worker above size thresholds (`autoWorkerThresholdMegapixels`, `autoWorkerThresholdEdge` in diagnostics). Fallback logs a reason.

**Autosave (Library-linked Composite only):**

- `updateCompositeDocument` may set `meta.compositeAutosave: true` → bumps a version counter → **`scheduleCompositeAutosave`** (debounced **10s** timer).
- **Does not schedule** if there is no **`activeLibraryOrigin('composite')`** with an id, or if updates pass `skipCompositeAutosave`.
- **`flushCompositeAutosave`** runs `saveProjectToLibrary` with `promptless: true`, `preferExisting: true`, and **capture options** that prefer worker for preview/export when allowed.
- **`setActiveSection`:** when leaving Composite → **`flushCompositeAutosave` first** so pending changes hit the Library before another section runs.

**C++ parity:** worker thread or job queue for PNG export + preview capture; debounced autosave to project store; flush on tab hide or section change.

---

## 6. Editor ↔ Composite bridge (critical)

### 6.1 UI state: `compositeEditorBridge`

Stored under `ui.compositeEditorBridge`:

```text
{ active: boolean, layerId, originalLibraryId, originalLibraryName }
```

- **`active`** means the Editor is currently showing a **slice** of the Composite document (one linked `editor-project` layer).
- **`originalLibraryId`** is set when the layer was created from a **generated-from-image** flow and the Editor project was persisted to the Library — used for **“Update Original Editor Project”** actions.

### 6.2 Opening a Composite layer in Editor — `openCompositeLayerInEditorFlow`

1. Resolve selected layer (by id or current selection).
2. If layer is **plain image** and conversion allowed → optionally **`ensureProjectCanBeReplaced('studio', …)`** then **`convertCompositeImageLayerToEditorProject`** (embeds captured studio payload, sets `source.generatedFromCompositeImage`, `renderWidth`/`Height`).
3. Require **`editor-project`** with `embeddedEditorDocument`.
4. **Skip extra save prompt** when the **current Editor document serializes identically** to some Composite `editor-project` layer’s embedded payload (`findMatchingCompositeEditorLayerForEditorDocument` compares **stable-serialized** payloads with previews stripped for fingerprint-style equality). This matches the product note: switching between linked layers that match the current Editor should not nag.
5. Otherwise **`ensureProjectCanBeReplaced('studio', …)`** if needed.
6. **`buildEditorDocumentFromPayload`** + **`syncStudioEngineToDocument`** into the **visible** Editor engine.
7. **Library origin for Editor:** if `generatedFromCompositeImage` and `originalLibraryId` → `setActiveLibraryOrigin('studio', …)` else **`clearActiveLibraryOrigin('studio')`** (embedded-only layer).
8. **`setCompositeBridgeState({ active: true, layerId, … })`**, **`commitActiveSection('editor')`**, request render.

### 6.3 Syncing Editor changes back — `syncBridgeEditorDocumentBackToComposite`

When **`compositeEditorBridge.active`** and user navigates **away from Editor** to a non-Editor section (or any path that calls this):

1. **`captureStudioDocumentSnapshot`** on the live Editor engine.
2. Patch **only** the bridged layer’s `embeddedEditorDocument` and `source.renderWidth`/`renderHeight` (and possibly `originalLibraryId` if still in generated-from-image flow with active studio origin).
3. **`updateCompositeDocument`** with **`compositeAutosave: true`**.
4. **`clearCompositeBridgeState`**.

### 6.4 Hidden second Editor engine for Composite-only rendering

`ensureCompositeEditorRenderEngineReady` constructs a **separate** `NoiseStudioEngine` + off-DOM canvas to **`captureCompositeEditorLayerDocument`** without disturbing the user’s visible Editor. Used when refreshing embedded previews or converting images.

**C++ parity:** either serialize “render this nested JSON” in an isolated context, or a second engine instance — must not mutate the primary Editor session unless explicitly bridging.

### 6.5 “Update Original Editor Project”

When bridge is active and **`originalLibraryId`** exists, user can confirm overwriting that **Library** studio record with the current Editor snapshot, then patch the Composite layer’s embedded document + queue autosave (`updateOriginalEditorProjectFromComposite`).

### 6.6 Generated-from-image layers and Library save

**`persistGeneratedCompositeEditorProjects`** scans layers with `source.generatedFromCompositeImage`: for each, saves/updates a real **studio** Library entry, writes back `originalLibraryId` / name / render sizes into the Composite layer, optionally **`mergePersistedGeneratedCompositeDocument`** when merging live + saved state.

---

## 7. Composite workspace UI (`ui.js`) — editing features

### 7.1 Sidebar

Four tabs: **Layers**, **Selected** (`transform` internally), **View** (`blend` internally — checker + view-related controls), **Export**.

**Stability requirement (from site context):** while typing or dragging sliders, avoid rebuilding the entire sidebar DOM mid-interaction.

### 7.2 Stage interaction

- Pan / zoom with wheel; **checker** and **zoom lock** from **Settings → Composite** merged via `applyCompositeSettingsToDocument` / `createSettingsDrivenCompositeDocument`.
- **Click empty stage** → deselect.
- **Click layer** → select; drag to move; handles for scale (per-layer `resizeMode`); rotation support where implemented.
- **Multi-select:** right-drag marquee on empty stage; **group bounds** + **group scale/move**; **Delete** removes selected set.
- **Crop mode** for image/editor layers: toggles **blue** handles; insets `L/T/R/B`; reset crop.

### 7.3 Replace With

Overlay to replace the selected layer with **another Editor project** (from Library picker) or **image file** — uses `buildCompositeReplacementLayer` to **preserve transform box** (fit replacement into previous rendered bounds).

### 7.4 Export panel

- **Auto vs custom** bounds, background transparent/solid.
- **“Use View As Export”** seeds custom export from the **live viewport-visible** world bounds (product rule: frame follows what you see, including imported editor layers).

---

## 8. Save / load paths

### 8.1 Local JSON (`saveCompositeState` / `openCompositeStateFile`)

- Save requires **≥ 1 layer**; captures **preview + payload** (`captureCompositeDocumentSnapshot`), writes self-contained JSON.
- If draft had **`activeLibraryOrigin('composite')`**, after disk save the app tries **`flushCompositeAutosave`** to sync the Library record.
- Load: **`ensureProjectCanBeReplaced('composite', …)`**, validate `composite-document`, **`clearCompositeAutosaveTimer`**, **`clearCompositeBridgeState`**, **`clearActiveLibraryOrigin('composite')`**, apply settings merge, switch to Composite section.

### 8.2 Library (adapter `type: 'composite'`)

- **`isEmpty`:** no layers.
- **`serializeDocument`:** `buildCompositeLibraryPayload` (stripped/normalized for DB).
- **`captureDocument`:** snapshot with preview/blob for card.
- **`restorePayload`:** same as file load but **`setActiveLibraryOrigin('composite', libraryId, libraryName)`** so autosave can target that record.

### 8.3 Autosave eligibility

Scheduled only when:

- `compositeAutosave` meta flag on committed edits, **and**
- `getActiveLibraryOrigin('composite')?.id` is set, **and**
- document has layers, **and**
- not already in-flight (versioned coalescing).

---

## 9. Navigation edge cases (web behavior to mirror)

1. **Composite → Editor** via tab bar: if selected layer is editor-backed or image-backed, **`openCompositeLayerInEditorFlow`** is invoked automatically (can convert image first).
2. **Composite → anything else:** flush Composite autosave; if **`compositeEditorBridge.active`**, attempt **`syncBridgeEditorDocumentBackToComposite`** (warn on failure).
3. **Leaving Composite** does not implicitly save **Editor** — separate origins and adapters.

---

## 10. C++ / Stack implementation checklist

1. **Parallel project model:** `CompositeDocument` alongside `StudioDocument`, same Library store with type discriminator (`composite` vs `studio`).
2. **Replace guards:** port **`ensureProjectCanBeReplaced`** semantics (empty draft, fingerprint match in Library, else tri-modal prompt).
3. **Schema:** implement `normalizeCompositeDocument` rules including layer kind coercion edge cases (image vs editor detection).
4. **Renderer:** 2D raster path with blend modes, rotation, flip, opacity, crop rects, export auto/custom viewport — numerical parity with `engine.js`.
5. **Threading:** background export + preview; join/cancel on shutdown; **flush** on tab switch.
6. **Bridge struct:** `compositeEditorBridge` + **`syncBridgeEditorDocumentBackToComposite`** on section change + explicit “apply”.
7. **Hidden render context** for nested editor JSON → bitmap for layers.
8. **UI:** four panels; marquee multi-select; replace overlay; crop mode; “Use View As Export”.
9. **Settings:** composite preferences (checker, zoom lock, export backend, thresholds) **not** stored inside project JSON except where document explicitly duplicates view defaults.
10. **Library thumbnails:** save preview; if capture fails, site prefers **fallback to export blob** for card (note in Whole Site Context).

---

## 11. Primary source file map

| Concern | Web files |
|---------|-----------|
| Schema / math | `src/composite/document.js` |
| Draw / export | `src/composite/engine.js` |
| Stage + sidebar DOM | `src/composite/ui.js` |
| Store, bridge, autosave, adapters | `src/app/bootstrap.js` |
| Worker tasks | `src/workers/tasks/composite.js` |
| Narrative + payload JSON | `Important Sources/Whole Site Context.txt` §Composite, §11 |
| High-level recap | `docs-stack-research/current-state/14_COMPOSITE.md`, `Stack-Rebuild-Docs/Composite/01_Composite_Info.md` |

This spec is intentionally **behavioral**: match outputs, UX guards, and persistence rules; internal class names may differ in C++.
