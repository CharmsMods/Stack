# Composite tab implementation guidelines
_Exported on 4/20/2026 at 18:24:19 PDT from Cursor (3.1.17)_

---

**User**

i want to build the composite tab that's currently empty@Stack , so in order to do this, youll need to create some guidelines and info in a document to follow, so go to 

@Important Sources/Whole Site Context.txt 
and search it for mention of a composite tab, then read through the code for it, (my website code) and then describe all its features in a new implimentation document so we can re-create it with threading and saving/loading and all it's editing features in the cpp program
ESPECIALLY it's link with the editor tab, and how the program preserves projects that are not from the composite tab when trying to open something else i.g. the saving features



---

**Cursor**

Gathering context from the site document and locating composite-tab code to draft an implementation spec.



Reading the composite implementation and bridge/save logic.



Creating a detailed implementation guide in `Stack/` (C++ rebuild target), synthesizing `Whole Site Context.txt`, `src/composite/*`, and `bootstrap.js` (bridge, adapters, autosave).



Here is what I did and where it lives.

## Source review

From **`Important Sources/Whole Site Context.txt`** (grep + read of the Composite payload and §11 save/load): Composite is a separate tab with self-contained `composite-document` JSON, Library integration, Editor bridge, sidebar labels (Layers / Selected / View / Export), settings-driven export path, and autosave rules (no autosave from pan/zoom alone).

From **your site code**: `src/composite/document.js` (full schema, layers, crop, blend modes, export block), `src/composite/engine.js` (viewport + draw + PNG export + preview), `src/composite/ui.js` (sidebar, marquee multi-select, crop handles, Replace With, Use View As Export), and **`src/app/bootstrap.js`** for the parts you stressed: **`compositeEditorBridge`**, **`openCompositeLayerInEditorFlow`**, **`syncBridgeEditorDocumentBackToComposite`**, **`ensureProjectCanBeReplaced`**, composite **project adapter** (`restorePayload` / `serializeDocument` / `captureDocument`), **debounced Library autosave**, and **flush on leaving Composite**.

## New document

**`Stack/Composite_Web_Parity_Implementation_Guide.md`**

It is written as a **parity spec for the C++ Stack** and includes:

1. **Separate documents per section** (`document` vs `compositeDocument`, etc.) and how **loading Composite does not wipe Editor** — only the targeted adapter runs after **`ensureProjectCanBeReplaced`** for that adapter’s type.  
2. **Full `composite-document` schema** (layers: image / editor-project / text / shapes), transforms, blend modes, crop, export auto/custom, `preview`, and **`source.renderWidth` / `renderHeight`** for editor-backed layers.  
3. **Rendering & threading**: main vs worker 2D path, autosave debounce (~10s), flush when switching away from Composite, task domain shape.  
4. **Editor link end-to-end**: bridge state, open-in-editor flow (including **skip save prompt** when the current Editor payload **matches** another linked layer via stable serialization), sync-back when leaving Editor, hidden second engine for captures, **Update Original**, image→editor-project conversion and **`persistGeneratedCompositeEditorProjects`**.  
5. **Save/load**: local JSON, Library adapter behavior, when autosave runs vs when it does not (e.g. viewport-only changes).  
6. **UI behaviors** from `ui.js`: marquee selection, group transforms, Replace With, crop mode, shared font list with Editor.  
7. A **C++ checklist** and a **source file map** back to the web repo.

If you want this file under a different folder (e.g. only `Stack-Rebuild-Docs/Composite/`), say where and I can move or duplicate it.

---

**User**

lets impliement, and during implimentation, respect how Stack may do things differently than the web since they are different platforms, even with things like the saving for projects already being different since they are saved as binary files with their own extention

---

**Cursor**

Exploring the Stack C++ project structure and how Editor projects are saved so we can implement Composite with platform-appropriate persistence.



Implementing CompositeModule, binary persistence via existing `.stack` format with `projectKind: composite`, and Library integration.



Fixing CompositeModule: removing the extern hack, including OpenGL headers, and wiring LibraryManager save.



Implementing LibraryManager load/save and fixing CompositeModule.



Here is what was implemented and how it differs from the web app.

## Summary

### Native model (Stack)
- **Same `.stack` binary** as editor/render (`StackBinaryFormat`), with **`projectKind = "composite"`** in metadata instead of a separate file extension. Payload shape is **not** web JSON: layer PNGs live in the **pipeline** section as a single JSON object (`layers[]` with `imagePng` binary blobs), plus **thumbnail** and **full export** PNGs in the usual THMB/SRCI sections so Library previews behave like render projects.

### New code
- **`Stack/src/Composite/CompositeModule.{h,cpp}`** — layer list (image-only for now), pan/zoom, checker, drag selected layer, opacity/scale/z, CPU rasterize for save, GL textures for the stage.
- **`StackBinaryFormat.h`** — `kCompositeProjectKind`.
- **`LibraryManager`** — `RequestSaveCompositeProject`, `RequestLoadCompositeProject`, `RequestLoadCompositeProjectFromPath`; **preview** path keeps the real `projectKind` (so composite is not mislabeled as editor); **import/drop** opens composite in tab **2**; **editor load** rejects composite payloads.
- **`LibraryModule`** — preview UI: composite summary, **“Load Project Into Composite”**, asset card can open a linked composite project.
- **`AppShell`** — `m_Composite`, init/shutdown, **Composite** tab runs `m_Composite.RenderUI()`, Library gets `&m_Composite`, file drop passes composite into `RequestImportAndLoad`.

### Deliberate gaps (future work)
- No Editor-tab bridge yet (no hidden second engine / embedded `.stack` layers).
- No `ensureProjectCanBeReplaced`-style prompts when switching tabs with unsaved composite (only dirty flag is there).
- Threading: save still uses the existing **background task** pattern like render; heavy work is the PNG pack/write on the worker thread.

If you want the next slice, a good order is: **embedded editor-reference layers** (load another `.stack` as a layer + thumbnail from library path) and **dirty/save prompts** when leaving Composite or loading another project.

---

**User**

wonderful, i compiled again, and tested it, so far it seems to be really good progress, however:
loading an image loads it flipped in both directions
the canvas is super short vertically
the UI should be more modular, and organization should be really important UI wise.
I would like the canvas to be a main part of the tab (not that you aren't doing that)
after fixing those issues, lets continue

