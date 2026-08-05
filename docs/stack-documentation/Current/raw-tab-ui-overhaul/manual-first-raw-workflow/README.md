# Manual-First RAW Workflow

## Purpose

This is the entry point for Stack's current RAW editor direction.

The automatic-editor goal was retired on July 23, 2026. Stack should now open a
RAW image with the smallest explicit, deterministic interpretation needed to
make it editable, then let the user shape the image manually.

## Read Order

Before changing RAW import defaults, the RAW workspace, the Develop node, Local
Range, Finish Tone, or View Transform UI, read:

```text
README.md
implementation-progress.md
project-session-lifecycle.md
multi-image-raw-project-foundation.md
interactive-preview-performance-pass.md
local-exposure-direct-targeting-implementation.md
research-order-and-stack-gap-audit.md
2026-07-23-manual-first-raw-editor-direction.md
automatic-ui-archive-inventory.md
```

Read the archived automatic packet only when historical behavior or preserved
backend code must be understood:

```text
docs/stack-documentation/Archived/raw-tab-ui-overhaul/automatic-raw-editor-2026-07-23/
```

## Product Direction

Stack is a manual RAW editor first.

The default image should be:

- deterministic
- minimally styled
- explicit about camera metadata and transforms it applies
- free of automatic exposure, local-range, tone, or display-fitting edits
- composed from values the user can inspect and change

“Truthful” cannot mean displaying sensor samples with no interpretation. A RAW
image still needs black/white normalization, demosaic, white-balance policy, a
camera-to-working-space transform, and a view transform before a monitor can
show it. In Stack, truthful means those necessary steps are conservative,
documented, reproducible, and separated from creative edits.

## Initial Manual Workflow

This is a starting hypothesis, not a frozen final layout:

```text
1. Open a neutral, deterministic RAW conversion.
2. Inspect capture metadata, clipping, and channel headroom.
3. Choose White Balance.
4. Set scene-linear RAW Exposure.
5. Use Local Range for regional or tonal-zone corrections.
6. Use Finish Tone to shape global tonal relationships.
7. Set View Transform / display mapping explicitly.
8. Add detail, noise, cleanup, crop, and output decisions as needed.
```

The UI should make that sequence spatial rather than forcing it into one long
vertical form. Local Range and Finish Tone graphs are reference interactions
for the redesign. Future passes should consider docked or switchable work
windows, persistent scopes, direct graph manipulation, and concise inspectors.

## Current Boundary

Phase 00 archived the automatic edit surfaces. Phase 01 now supplies a
versioned `truthful-v1` foundation for new recipes, while saved recipes from
before recipe schema 7 remain on `legacy-v1`.

`truthful-v1` is a bounded foundation, not a claim that Stack has finished a
production camera pipeline. The current implementation is explicit about the
remaining gaps: unsupported DNG opcodes, non-Bayer mosaics, nonlinear camera
profiles, pre-demosaic highlight reconstruction, a validated guided-filter
Local Range path, and monitor ICC presentation. See
`research-order-and-stack-gap-audit.md` for the current comparison and
`implementation-progress.md` for the active gates.

The first interactive-performance pass is documented in
`interactive-preview-performance-pass.md`. Read it before changing RAW render
threading, preview resolution, GPU readbacks, stage caches, or invalidation.

The image-first left-rail prototype is implemented as a separate `RAW Lab`
root tab. It shares the existing RAW catalog, project, recipe, preview, render
pipeline, caches, and dirty/save lifecycle while keeping the original `RAW`
presentation available as the baseline. Its implementation boundary and
remaining promotion gates are recorded in
`raw-lab-left-rail-implementation.md`.

RAW Workspace schema 3 and the source-set project/storage foundation are
documented in `multi-image-raw-project-foundation.md`. Multi-image projects are
still one application-wide project session; they do not create a second RAW
project beside the Editor graph. The first Multi-Frame Denoise (MFD)
pre-processing boundary accepts compatible, still-mosaiced RAW captures and
creates protected per-frame graph nodes feeding one MFD node. Its result still
reports `Result unavailable` to the graph. RAW Lab can now run the implemented
RA-CFA V1 processor and publish neutral inspection artifacts, but the
authoritative Bayer-to-shared-RAW-pipeline handoff is not connected yet.
RA-CFA implementation status is kept in `../../MFD/STATUS.md`.
Demosaiced/Linear RGB DNG support remains a future workflow.

Recipe schema 9 adds a disabled-by-default pre-demosaic denoise block. Recipe
schema 10 adds a separate, disabled-by-default post-demosaic scene-linear RGB
denoise block. Recipe schema 11 adds optional, exactly pinned Restormer methods
without changing the built-in Classical Multiscale default. RAW Lab exposes
these as CFA Denoise and RGB Denoise before Exposure. Their processing
foundations and remaining validation gates are recorded in
`../../engineering/denoise-rewrite/classical-pre-demosaic-foundation.md` and
`../../engineering/denoise-rewrite/classical-post-demosaic-foundation.md`; the
optional-model boundary is owned by
`../../engineering/denoise-rewrite/restormer-rgb-denoise-v1/README.md`.

The schema-8 Local Exposure target-zone model, direct image gestures,
connected-area qualifier, overlap math, shared overlay delivery, and remaining
interaction boundaries are recorded in
`local-exposure-direct-targeting-implementation.md`.

Do not delete the archived solver backend merely to simplify the UI. Preserve
compatibility with existing recipes and use later cleanup passes to decide
which unreachable implementation can be removed safely.
