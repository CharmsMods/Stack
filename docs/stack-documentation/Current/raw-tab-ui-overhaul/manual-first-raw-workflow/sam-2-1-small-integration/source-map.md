# Current Stack Source Map

This map identifies likely seams. It does not authorize changes.

## Retired Optional Neural-Pack Precedent

- `docs/stack-documentation/Archived/neural-denoise/RESTORMER_ONNX_INTEGRATION.md`
  - historical external optional pack and runtime behavior;
  - useful evidence, not an active API or the subject-selection authority.
- `src/NeuralDenoise/NeuralDenoiseManager.cpp` in repository history
  - retired external root and manifest resolution.
- `src/NeuralDenoise/NeuralDenoiseTypes.h`
  - compatibility-only saved-project settings; it is not a current package
    contract.
- `src/NeuralDenoise/OnnxDenoiseBackend.cpp` in repository history
  - retired dynamic ONNX Runtime C API loading and session behavior.

Reuse lessons only where justified. Do not revive the denoise-specific
manifest, runtime loader, settings, or lifecycle as the subject-selection
service.

## Current RAW Target Interaction

- `src/Editor/EditorModule.h`
  - RAW workspace target state, transient overlay state, and edit ownership.
- `src/Editor/RawLocalRangeTargetInteraction.h`
  - pure gesture/state helpers and generation acceptance.
- `src/Editor/EditorRenderWorker.h`
  - RAW workspace snapshot/result target-preview fields.
- `src/Editor/EditorRenderWorker.cpp`
  - target sample and overlay capture/delivery.
- `src/Renderer/MaskRenderTypes.h`
  - `RawLocalRangeTargetPreviewRequest` and render-graph transport.
- `src/Renderer/RenderPipeline.h`
  - local qualifier, selection bits, outline, readback, refinement, and target
    sample resources.
- `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`
  - graph request capture and overlay scheduling.
- `src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp`
  - pre-Local-Range source, target sampling, Local Range application, and
    overlay point in the RAW pipeline.
- `src/App/Validation/Suites/DevelopSmokeValidation.cpp`
  - current target sample, selection, refinement, and outline GL validation.

## UI Surfaces

- `src/Editor/Internal/EditorModuleRawWorkspace.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspaceLab.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspaceLocalRange.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspaceAnalysis.cpp`

The RAW UI has been actively decomposed. Confirm function ownership again
before editing. The model backend and mask transaction must be shared by RAW
and RAW Lab rather than copied into either presentation.

## Build, Release, and Notices

- `CMakeLists.txt`
  - Stack targets and copied license/notice files.
- `tools/create_release.ps1`
  - staged release tree and license copying.
- `installer/StackInstaller.iss`
  - base installer packaging.
- `LICENSE`
  - Stack proprietary license and third-party-material boundary.
- `THIRD_PARTY_NOTICES.md`
  - existing distribution notices and external-provider policy.

The base release should not recursively absorb optional model packs. Package
installation needs a separate signed download/update path.

## Expected New Ownership

Exact filenames are not locked, but future code should separate:

- generic approved-package verification;
- subject-service process/client and versioned protocol;
- SAM model contract and provider implementation;
- analysis-proxy production and fingerprinting;
- transient AI proposal state;
- Stack-authored mask composition/persistence;
- package-manager UI and license display;
- focused service, package, conversion-parity, and editor integration tests.

Do not create a public arbitrary model-provider ABI during the first
implementation.
