# Restormer ONNX Integration

## Purpose

Restormer-style RGB denoising is the first real inference target for Stack's `Linear RGB Neural Denoise` layer. It is external and optional: Stack does not bake model weights into the EXE, and the application must launch and edit images normally without a denoise pack, ONNX Runtime DLLs, CUDA provider DLLs, or model files.

## Expected Model Pack

The default external layout is:

```text
Stack/
  Stack.exe
  denoise/
    manifest.json
    runtimes/
      onnxruntime.dll
      onnxruntime_providers_cuda.dll
      onnxruntime_providers_shared.dll
    models/
      restormer_rgb_denoise.onnx
    licenses/
      Restormer_LICENSE.txt
      ONNXRuntime_LICENSE.txt
      NVIDIA_NOTICES.txt
```

`STACK_DENOISE_DIR` may point at a different `denoise` root for development or testing.

## Manifest Contract

The first supported model contract is:

- `type`: `linear_rgb`
- `architecture`: `restormer_like`
- `preferredBackend`: `onnx_cuda`
- `inputFormat`: `nchw`
- `inputRange`: `0_1`
- `precision`: `["fp32"]`
- `inputChannels`: `3`
- `outputChannels`: `3`
- optional `inputName` / `outputName`: defaults to discovered model I/O if omitted
- optional `requiredInputMultiple`: model dimension multiple, currently `8` for Restormer-style exports
- same-size RGB output

Example:

```json
{
  "version": 1,
  "models": [
    {
      "id": "restormer_rgb_denoise",
      "displayName": "Restormer RGB Denoise",
      "file": "models/restormer_rgb_denoise.onnx",
      "type": "linear_rgb",
      "architecture": "restormer_like",
      "preferredBackend": "onnx_cuda",
      "precision": ["fp32"],
      "inputFormat": "nchw",
      "inputRange": "0_1",
      "inputChannels": 3,
      "outputChannels": 3,
      "supportsTiling": true,
      "tileSize": 256,
      "tileOverlap": 32,
      "requiredInputMultiple": 8,
      "license": "MIT - verify before shipping official weights"
    }
  ]
}
```

## Runtime Behavior

Stack loads ONNX Runtime dynamically from `denoise/runtimes/onnxruntime.dll` and resolves the C API at runtime. It does not link against ONNX Runtime at build time and does not require ONNX Runtime to be installed globally.

CUDA is preferred for `onnx_cuda` models. If the CUDA provider files are missing or CUDA session creation fails, the node bypasses unless the user explicitly enables CPU fallback or selects CPU. CPU fallback is not silent.

## Local CUDA Runtime Pack

For Windows development smoke tests, the CUDA runtime pack can be assembled from the Python `onnxruntime-gpu` package without making Stack depend on Python:

```powershell
$root = Join-Path $env:TEMP 'stack_ort_gpu'
python -m venv $root\.venv
& $root\.venv\Scripts\python.exe -m pip install "onnxruntime-gpu[cuda,cudnn]" numpy onnx
```

Copy the ONNX Runtime C API DLLs from the venv's `onnxruntime\capi` folder and the CUDA/cuDNN DLLs from the venv's `nvidia\*\bin` folders into `denoise/runtimes/`. Keep this pack local; do not commit DLLs.

Expected first-pass CUDA validation:

- `nvidia-smi` sees the NVIDIA GPU.
- Python ONNX Runtime reports `CUDAExecutionProvider`.
- Creating an ONNX Runtime session succeeds with `providers=['CUDAExecutionProvider']`.
- Stack's node UI reports `CUDA provider dependencies loaded` / `Ready for CUDA`.
- With provider `Auto` or `CUDA` and CPU fallback off, `Run Denoise` should use CUDA and report `Last provider: CUDA`.

## Local Smoke Test Before Restormer

Before trying a real Restormer export, validate the full Stack inference path with tiny self-generated ONNX graphs:

```powershell
python tools/neural_denoise/create_local_test_pack.py
```

This creates a local development-only pack:

```text
denoise/
  manifest.json
  models/
    identity_rgb.onnx
    half_rgb.onnx
  runtimes/
    README_PLACE_ONNXRUNTIME_DLLS_HERE.txt
  licenses/
    TEST_MODEL_NOTICE.txt
```

The scripts require the Python `onnx` package only for generating the tiny test graphs:

```powershell
python -m pip install onnx
```

Download or use an ONNX Runtime build separately. For CUDA testing on Windows, place these files in `denoise/runtimes/`:

- `onnxruntime.dll`
- `onnxruntime_providers_cuda.dll`
- `onnxruntime_providers_shared.dll`
- any CUDA/cuDNN dependency DLLs required by the ONNX Runtime build

The CUDA, cuDNN, and ONNX Runtime versions must match. If the CUDA provider fails and CPU fallback is off, `Linear RGB Neural Denoise` should bypass and report the CUDA/provider problem. If CPU fallback is explicitly enabled, the identity model should run with CPU ONNX Runtime.

Expected validation behavior:

- `Identity RGB Test Model` at 100% strength should visually match the original image for ordinary `0..1` RGB content.
- `Identity RGB Test Model` at 0% strength should always match the original image.
- `Half RGB Test Model` should visibly darken the RGB image at 100% strength and confirms the output is coming from ONNX inference, not pass-through.
- Alpha should remain unchanged.
- Dimensions should remain unchanged.
- Tiling should not produce seams. If seams appear, inspect tile crop coordinates, Y orientation, channel order, and upload format before testing Restormer.

Do not commit generated `denoise/` output unless the project explicitly decides to version tiny local test artifacts. Do not place Restormer weights in the repo.

## Restormer Export Workflow

The identity and half models validate Stack's runtime plumbing only. A real Restormer model must be exported from a local PyTorch checkpoint into an external ONNX file before Stack can run it.

See [RESTORMER_EXPORT_WORKFLOW.md](RESTORMER_EXPORT_WORKFLOW.md) for the local/dev-only workflow:

- export from the official `swz30/Restormer` architecture with `tools/neural_denoise/export_restormer_onnx.py`
- validate the ONNX contract with `tools/neural_denoise/validate_restormer_onnx.py`
- create a local `denoise/` pack with `tools/neural_denoise/create_restormer_test_pack.py`

This workflow does not ship Restormer weights, ONNX models, Python dependencies, or runtime DLLs with Stack.

PyTorch may export large Restormer weights as ONNX external data beside the graph, commonly:

```text
restormer_rgb_denoise.onnx
restormer_rgb_denoise.onnx.data
```

Keep both files together in `denoise/models/`. The local pack script copies the external data sibling when it exists.

## Image Math

The layer reads the OpenGL input texture into an RGBA float staging buffer, converts RGB to NCHW FP32, runs tiled ONNX inference, and blends the model estimate:

```text
final.rgb = original.rgb + (model.rgb - original.rgb) * strength * differenceAmount
final.a = original.a
```

For `inputRange: "0_1"`, only the model input tensor is clamped to `[0, 1]`. The original RGB values remain the blend reference. This is a conservative first pass for HDR/scene-linear images; dedicated HDR-aware model handling is future work.

## Tiling

Inference uses a safe tiled path. Each tile includes overlap padding, ONNX runs on the padded tile, and only the core tile region is copied into the final model-output image. Edge tiles clamp to image bounds. If `requiredInputMultiple` is set in the manifest, the ONNX tile request is padded to that multiple before inference. Feathered overlap blending is reserved for a later refinement.

## Manual Editor Test Checklist

Use this checklist for the first real editor pass:

- Export and validate `restormer_rgb_denoise.onnx` with the workflow above.
- Create a local pack with `tools/neural_denoise/create_restormer_test_pack.py`.
- Copy matching ONNX Runtime DLLs into `denoise/runtimes/`.
- Launch Stack with `STACK_DENOISE_DIR` pointing at the local pack if it is not beside the EXE.
- Add or select `Linear RGB Neural Denoise`.
- Select `Restormer RGB Denoise`.
- Keep CPU fallback off first and confirm CUDA/provider errors are readable if CUDA is unavailable.
- Enable CPU fallback only for a small test image if CUDA is unavailable.
- Press `Run Denoise` or `Refresh Denoise`; inference is manual to avoid per-frame editor stalls.
- Test strength `0%`, `50%`, and `100%`; changing settings should make the cached result stale until refreshed.
- Test a transparent image and confirm alpha remains unchanged.
- Re-render without changing anything and confirm status stays on cached output rather than rerunning inference.
- Change tile size or overlap, then refresh and confirm dimensions remain unchanged.
- Test a non-multiple-of-8 image; tile requests should pad to `requiredInputMultiple: 8` and crop back to the original dimensions.

## Licensing

Restormer code is MIT, but official model weights, conversion scripts, ONNX Runtime, CUDA, cuDNN, and other redistributed binaries still require verified notices before an official Stack model pack can ship. Do not commit model weights or runtime DLLs to the source repo.

## Known Limitations

- NCHW FP32 RGB `0..1` models only.
- No INT8 quantization.
- No FP16 path yet.
- No RAW/CFA neural inference yet.
- No OpenGL/CUDA interop yet; this pass uses CPU staging for correctness.
- No arbitrary model schema negotiation beyond manifest-declared RGB fields.
- No official Restormer model pack is bundled yet.
- Cache invalidation is conservative and manual; upstream edits should be followed by `Refresh Denoise` until the layer receives a reliable source content revision.
