# Architecture Organization Guide

This is a standing rule for future Stack development: every feature, bug-fix pass, and refactor should reserve a small part of the work for file and folder organization.

Before adding code to an existing file, ask whether the change belongs in a new file, a narrower helper, or a domain folder. Large coordinator files should call into focused code; they should not keep absorbing new behavior.

Prefer folders that match product domains and runtime responsibilities: `Editor`, `Renderer`, `Raw`, `Develop`, `Composite`, `Library`, `App`, `Persistence`, and `Utils`. If a feature crosses domains, put shared data and pure logic in the domain folder, then keep UI, rendering, persistence, and validation adapters near the systems that own those jobs.

For Library work, keep storage/indexing and import/export decisions separate from preview texture lifecycle, image decoding/probing, and ImGui/module orchestration. A manager file can coordinate a workflow, but media helpers and UI surfaces should have focused owners when they grow.

For storage-heavy Library manager work, extract reusable filename/extension checks, timestamp formatting, byte IO, legacy decode, and asset fingerprint/similarity logic into focused internal storage helpers before splitting larger workflows. Keep async workflow ordering, conflict queues, status text, and app-facing decisions in the manager unless a whole workflow has a cleaner owner.

For Library refresh work, keep filesystem scanning, startup refresh traces, signature calculation, async refresh snapshots, and auto-refresh throttling together. Those concerns change for startup/performance and refresh correctness reasons, separate from project save/load or preview texture lifecycle.

For Library editor project persistence, keep `.stack` document read/write, source/rendered PNG packaging, node-browser thumbnail persistence, load/deferred-apply handoff, project rename/delete, and absolute-path editor project loading together. Those flows change with project format and editor handoff behavior, separate from library scanning, bundle import/export, and Composite-module persistence.

For Library project and bundle import/export work, keep dropped-file routing, `.stacklib` bundle read/write, project conflict preview/resolve, and export commands together. That flow changes for import/export compatibility reasons and should not be mixed back into indexing, project save/load, or asset-file lifecycle code.

For Library asset-file lifecycle work, keep synced project asset generation, loose image saves, `.hash` metadata handling, orphan cleanup, asset import conflicts, and asset export/delete together. That work changes for asset storage reasons, not for project load/save workflow reasons.

For Library Composite-module persistence, keep direct `CompositeModule` save/load orchestration separate from editor project save/load and library bundle import/export. That workflow changes with Composite document application, current project naming, dirty-state handling, and Composite-specific async status text.

When a UI module shares drawing primitives across cards, modals, drawers, and conflict views, extract the reusable visual/layout helpers first. Move whole workflows only after their shared helper boundary is clear.

For repeated-item UI such as Library cards, keep the item renderer and item context menu together in a focused file once the shared drawing helpers exist. Keep grid layout, filtering, virtualized rendering, search/mode chrome, and scroll behavior together in a grid owner; leave the parent module responsible for top-level workflow orchestration.

For modal-heavy UI, move the complete modal workflow together: transition state, detail panel, commands, close/cancel cleanup, and any nested confirmation popup that belongs only to that modal.

Menus that primarily open or configure modal workflows should live with that modal workflow owner, while the parent surface only decides where the menu is shown.

For Library tag/filter drawer work, keep hover-open behavior, drawer animation, tag filter toggles, selection tag application, and drawer-local theme preset controls together. The parent Library module should decide where the drawer appears in the main render flow, not own the drawer's full ImGui body.

For renderer graph execution work, keep support primitives such as cache-key hashing, framebuffer probes, graph-context setup, and small exposure/stat helpers separate from the execution body. Split render execution only around complete node families or pass owners, and only when the validation surface covers the behavior being moved.

For renderer graph analysis work, keep socket classification, scalar-vs-image output decisions, and Data Math input collection together as graph-structure analysis. Graph execution should ask for those decisions during fingerprinting and evaluation, not own their recursive graph-walk implementation.

For renderer graph HDR Merge work, keep graph-side representative-source lookup, HDR input metadata context resolution, and metadata/manual exposure reliability resolution together. The HDR Merge render pass should own GL rendering/alignment/deghost work, while graph execution should only request resolved HDR parameters during node dispatch.

For renderer graph texture-cache work, keep generic image/mask cache entry deletion, full-cache destruction, per-key release, store/replace, and inactive-node pruning together. Graph execution should record and request cache entries, but texture lifetime policy should live in the cache owner.

For renderer RawDevelop stage-cache work, keep texture clone, lookup/promotion, store/replace, entry deletion, byte accounting, trimming, destruction, and validation-facing sizing wrappers together. Graph execution should request stage-cache hits/stores but should not own the stage-cache memory policy.

For renderer graph RAW stage work, keep upstream RAW source traversal, embedded-vs-file RAW data loading, shared RAW base rendering for RAW Decode and Develop, stage-cache hit adoption, and raw-base graph-cache publication together. Graph execution should decide which node/socket is being evaluated, but it should not own RAW file loading or shared RAW base render/cache plumbing.

For renderer RawDevelop graph-node work, keep hidden pre-finish stage-cache lookup/publication, scene-prep rendering, integrated ToneCurve finish execution, finish-mask blending, auto rewrite feedback collection, and RawDevelop black-output guardrails together. Graph execution should delegate the RawDevelop node body, while RAW source/base rendering remains in the RAW stage owner and stage-cache memory policy remains in the RawDevelop stage-cache owner.

For renderer Raw Detail graph-node work, keep Raw Detail Auto Mask / Fusion graph dispatch, auto-mask-source setting inheritance, debug-preview routing, generated-mask handoff, and Pre-Local Exposure summary publication together. Low-level Auto Gain, Raw Detail Auto Mask, Raw Detail Fusion, and Pre-Local Exposure shader/pass primitives should stay in the Raw Detail pass owner.

For renderer Layer graph-node work, keep layer-registry instantiation, layer JSON deserialization, GL layer execution, ToneCurve auto rewrite feedback, default ToneCurve blank-output guardrails, and layer mask blending together. Graph execution should delegate the Layer node body and only handle traversal plus generic graph-cache publication.

For renderer DataMath graph-node work, keep scalar/image input resolution, multi-input Average accumulation/division, optional mask/base blending, blank base creation, and simple two-input math dispatch together. Graph analysis should still own DataMath input-list and scalar-vs-image classification helpers, while the DataMath node owner should consume those decisions during render execution.

For renderer LUT texture-cache work, keep LUT stage hashing, 1D/3D GL texture upload, per-stage texture replacement, cache-key clearing, and inactive-node LUT texture pruning together. Texture upload/cache lifecycle belongs in the focused LUT texture-cache owner, while shader/program setup stays with renderer program setup.

For renderer LUT graph-node work, keep image/channel input resolution, channel-combine fallback, cache texture requests, LUT shader uniform binding/draw, optional mask blending, and temporary texture cleanup together. Graph execution should delegate the whole LUT node body to the graph-LUT node owner instead of owning that branch inline.

For renderer graph render-target wrapper work, keep empty target texture creation and the FBO bind/viewport restore wrapper together. Node-family dispatch code should request these helpers without duplicating GL state-save boilerplate.

For node graph work, keep primitive graph concepts such as node kinds, socket ids, socket definitions, and tiny helper functions separate from payload structs, graph document structs, UI, serialization, and render execution code. Payload structs and graph document structs can have their own focused headers; serializer/UI files should not be the place where ownership is defined.

Use names that say what the file owns:

- `*Types.h` for shared structs/enums with little behavior.
- `*Controller.{h,cpp}` for editor/app coordination around a workflow.
- `*Renderer.{h,cpp}` for rendering a specific stage or visual surface.
- `*Serializer.{h,cpp}` for persistence format code.
- `*Validation.cpp` for command-line validation suites.
- `*Utils.{h,cpp}` only for genuinely reusable helpers.

Keep command dispatch thin. If a file parses CLI flags, routes menu actions, or coordinates a workflow, it should usually call focused suites/controllers instead of owning all implementation details itself.

Do not create a generic helper file just to hide complexity. Split code when the new boundary has a clear owner, a clear reason to change, and a clear set of dependencies.

For large changes, include an organization checkpoint before finishing: identify any file that grew substantially, any new cross-domain dependency, and any code that should become a separate file before the next pass.

Source code that is included, compiled, or required by tracked code must be visible to version control. Ignore model weights, generated outputs, local data, and runtime caches, but do not hide C++ source or headers behind local-only ignore rules.

For the current detailed ownership map and remaining cleanup candidates, see `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`.

