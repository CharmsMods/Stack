# Implementation Progress

Last updated: July 24, 2026.

| Work item | State | Evidence / remaining work |
|---|---|---|
| Versioned recipe contract | Implemented | Schema 12; two AI methods, two mappings, exact package/model/adapter pins |
| RAW Lab method and mapping controls | Implemented | Text-first controls in RGB Denoise |
| Scene-linear proxy CPU reference | Implemented | `RawRestormerAdapter`; V2 adds reversible, robust model-only exposure normalization for dark pre-exposure RAW data |
| Safe and processed residual CPU references | Implemented | Alpha, finite guards, zero-strength identity tests |
| Package manifest and hash validation | Implemented foundation | Release allowlist intentionally empty pending authorization |
| Package template and local stager | Implemented | The stager hashes all artifacts and produces provenance plus an SPDX SBOM; it never packages Python/PyTorch |
| PyTorch-to-ONNX export/equivalence harness | Implemented and exercised | Self-contained opset-18 output; external-data sidecars are rejected |
| Official checkpoint acquisition | Complete for local development | Official Real Photo and Gaussian Color Blind files are ignored by Git and recorded by source ID/SHA-256; do not redistribute |
| Written checkpoint authorization | Not obtained | Use the request record in this folder |
| Frozen FP32 ONNX conversions | Complete for local development | Both conversions pass PyTorch/ONNX Runtime comparison below `1e-4`; hashes are in the artifact ledger |
| Windows ML helper | Implemented foundation | External Windows x64 target; both real models pass DirectML and forced-CPU inference smoke tests |
| Named-pipe/shared-memory client | Implemented foundation | Health/Denoise/Cancel/Shutdown; image bytes do not travel through the pipe; one pipe owner publishes queued worker completions |
| Raised-cosine tiling | Implemented | 256/32 preview, 384/64 settled; odd-size identity/seam tests pass |
| Latest-wins cancellation | Implemented foundation | Cancel is checked between tiles and stale render generations are not published |
| Neutral AI-output cache | Implemented | Model output key excludes mapping, denoise sliders, Exposure, Zones, and Tone/View |
| Automatic preview / settled scheduling | Implemented foundation; user retest required | The first live MotionCam test exposed a blocking full-resolution call on the UI-owned RAW renderer. Inference and CPU residual application now run in a separate async job while the UI-owned GL path retains the previous/base presentation, polls completion, invalidates provisional caches, and publishes the accepted result. This deliberately does not enable the unsafe legacy RAW render worker. |
| Missing-package render/export block | Implemented foundation | Exact package validation occurs before cache use; the prior preview is marked stale and AI failure has no Classical bypass |
| PBO-ring texture transfer | Open | Never map in issuing frame |
| Package Manager install/rollback/remove | Open | Must display licenses and authorization status |
| Signed release allowlist | Blocked | Requires final immutable artifacts and legal approval |
| MotionCam/SIDD A/B testing | In progress | The first MotionCam run exposed a near-identity V1 input-domain mismatch; V2 and a RAW Lab 100% preview toggle are ready for user retest |

"Implemented foundation" is not equivalent to a redistributable AI feature.
The public package gate remains closed.

## July 24 Live-Integration Correction

The initial packaged-model smoke tests proved model loading, DirectML/CPU
execution, shared-memory transport, and PyTorch/ONNX equivalence, but they did
not prove that the RAW Lab integration remained responsive. The first
full-resolution MotionCam test found two defects:

1. `RenderRawDevelopmentRgbDenoise` synchronously waited for every model tile
   and ran the full CPU adapter inside the UI-owned RAW render call.
2. the synchronous RAW path did not surface a Restormer failure when no new
   output texture was produced, so the previous image could remain visible
   without a useful explanation.

The corrected development path now:

- runs external inference and CPU adapter work asynchronously;
- keeps an undenoised before-state visible while the first result is pending;
- does not persist provisional pass-through pixels after completion;
- forces a follow-up graph evaluation when the model job finishes;
- exposes `AI denoise updating`, measured ready/provider timing, and explicit
  failure status in RAW Lab;
- keeps full-resolution export/validation pipelines synchronous and exact;
- leaves the UI-owned OpenGL texture read/upload boundary in place until the
  PBO-ring work item is implemented; and
- provides `Recheck Package` even after a recipe is already pinned.

Both packaged ONNX files were also run directly through ONNX Runtime with a
fixed non-uniform input. Their mean absolute output deltas were nonzero
(`0.07918` Gaussian Blind and `0.09022` Real Photo), ruling out identity model
artifacts. Real-image visual acceptance and latency remain pending the next
user test.

## July 24 Dark-RAW Input-Domain Correction

The responsive integration exposed a second, separate issue: completed model
jobs changed the model proxy by only about `0.0011`, and the accepted
scene-linear image by roughly `0.00005` through `0.00058`. The viewport was
publishing those results, but the correction was effectively invisible.

Adapter V1 fed the pre-exposure scene-linear image directly into checkpoints
trained on normally developed RGB photographs. Adapter V2 now computes a
bounded robust model-only exposure, applies it before proxy compression, and
divides it back out while transferring the predicted residual. This does not
change RAW Exposure, image headroom, recipe exposure, or output brightness.
The adapter change is versioned, requires the local `1.0.0-dev.6` package, and
invalidates V1 model pins rather than silently reusing them.

RAW Lab also now has a bare `100%` preview toggle. Fit remains the default;
the pixel-scale view exists so denoise is not hidden by viewport downsampling.
