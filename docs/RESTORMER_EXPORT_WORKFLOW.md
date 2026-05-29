# Restormer Export Workflow

## Purpose

Stack's `Linear RGB Neural Denoise` layer can run external ONNX models through the optional neural denoise pack. Restormer is PyTorch-first, so a local development step is required before Stack can use it: export a compatible Restormer checkpoint to ONNX, validate the ONNX contract, then place it in a local `denoise/` test pack.

This workflow is local/dev-only. It does not add Python, PyTorch, Restormer, ONNX, ONNX Runtime, model weights, runtime DLLs, or generated packs to Stack's build.

## Stack Model Contract

The first Restormer target for Stack is a conservative RGB denoise contract:

- Input tensor: `float32` RGB
- Input shape: `[1, 3, H, W]`
- Layout: NCHW
- Input range: `0.0` to `1.0`
- Output tensor: `float32` RGB
- Output shape: `[1, 3, H, W]`
- Output size: same height and width as input
- Alpha: not part of the model; Stack preserves the original alpha channel externally
- Color/gamma: Stack does not gamma-convert, tone-map, or color-transform unless a future manifest explicitly declares that behavior
- Quantization: no INT8 path

Scene-linear values above `1.0` are currently clamped only for model input when `inputRange` is `0_1`; the original RGB remains the final blend reference inside Stack.

## Source Layout

The default target is the official Restormer repository:

- Repository: [swz30/Restormer](https://github.com/swz30/Restormer)
- Architecture file: `basicsr/models/archs/restormer_arch.py`
- Paper/code page: [CVPR 2022 Restormer](https://openaccess.thecvf.com/content/CVPR2022/html/Zamir_Restormer_Efficient_Transformer_for_High-Resolution_Image_Restoration_CVPR_2022_paper.html)

The export script expects a local checkout, for example:

```text
external/
  Restormer/
    basicsr/
      models/
        archs/
          restormer_arch.py
```

The official demos commonly pad image dimensions to multiples of 8 before inference. Stack's manifest therefore records `requiredInputMultiple: 8`, and the runtime pads tiled ONNX requests to that multiple while copying only the valid tile core back to the output.

## Dependencies

Install export dependencies in a local Python environment, separate from Stack:

```powershell
python -m pip install torch onnx onnxruntime
```

Use an ONNX Runtime GPU package only when validating CUDA in Python:

```powershell
python -m pip install onnxruntime-gpu
```

Stack itself remains C++ only and does not import these packages at build time.

## Export

Use `tools/neural_denoise/export_restormer_onnx.py` with a local Restormer checkout and checkpoint:

```powershell
python tools/neural_denoise/export_restormer_onnx.py `
  --restormer-root D:\dev\Restormer `
  --checkpoint D:\models\restormer_real_denoising.pth `
  --output D:\models\restormer_rgb_denoise.onnx `
  --task real-denoising `
  --export-size 256 `
  --dynamic-axes `
  --device cpu
```

Dynamic height/width export is preferred. If dynamic export fails, the script can retry a fixed-size export unless `--dynamic-only` is supplied. A fixed 256 or 512 export is acceptable for the first model validation pass, provided the manifest and Stack tiling settings use compatible tile sizes.

The script uses official Restormer-style RGB denoising defaults and can accept a JSON constructor override:

```powershell
python tools/neural_denoise/export_restormer_onnx.py `
  --restormer-root D:\dev\Restormer `
  --checkpoint D:\models\checkpoint.pth `
  --output D:\models\restormer_rgb_denoise.onnx `
  --model-config-json D:\models\restormer_config.json
```

Checkpoint loading handles common keys such as `params`, `params_ema`, `state_dict`, or a raw state dict. If a checkpoint was saved with different module names or training wrappers, use `--allow-partial-load` only for diagnosis and inspect the missing/unexpected keys carefully before trusting output quality.

## Validate

Validate the exported ONNX before creating a Stack pack:

```powershell
python tools/neural_denoise/validate_restormer_onnx.py `
  --model D:\models\restormer_rgb_denoise.onnx `
  --height 256 `
  --width 256 `
  --provider cpu `
  --required-multiple 8
```

The validator checks:

- ONNX structure loads and passes checker validation
- ONNX Runtime can create a session
- Providers are visible and the requested provider is selected when available
- Output is `float32`
- Output shape matches `[1, 3, H, W]`
- Output contains no NaN or Inf values
- A second non-square multiple-of-8 shape works by default

CUDA validation is optional:

```powershell
python tools/neural_denoise/validate_restormer_onnx.py `
  --model D:\models\restormer_rgb_denoise.onnx `
  --provider cuda
```

If CUDA is unavailable, validate on CPU first. Stack will still require CUDA provider DLLs for CUDA-first runtime unless CPU fallback is explicitly enabled in the node UI.

## Create Local Test Pack

After validation, create a local development pack:

```powershell
python tools/neural_denoise/create_restormer_test_pack.py `
  --onnx D:\models\restormer_rgb_denoise.onnx `
  --output D:\Program Development\Stack\denoise `
  --tile-size 256 `
  --tile-overlap 32 `
  --required-input-multiple 8
```

This creates:

```text
denoise/
  manifest.json
  models/
    restormer_rgb_denoise.onnx
    restormer_rgb_denoise.onnx.data  (when PyTorch exported external data)
  runtimes/
    README_PLACE_ONNXRUNTIME_DLLS_HERE.txt
  licenses/
    RESTORMER_NOTICE.txt
    TEST_PACK_NOTICE.txt
```

Place ONNX Runtime DLLs in `denoise/runtimes/` separately. Do not commit this generated pack.

If the exporter writes an external data file beside the ONNX graph, keep it beside the graph in `denoise/models/`. ONNX Runtime needs both files to load the model.

## Manifest Entry

The generated manifest uses the same model contract as Stack's backend:

```json
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
  "inputName": "input",
  "outputName": "output",
  "inputChannels": 3,
  "outputChannels": 3,
  "supportsTiling": true,
  "tileSize": 256,
  "tileOverlap": 32,
  "requiredInputMultiple": 8,
  "license": "verify before shipping official weights"
}
```

Restormer-specific shape requirements belong in manifest metadata such as `requiredInputMultiple`; they should not be hard-coded into Stack's backend.

## Licensing

Restormer code is MIT, but official model weights, conversion dependencies, ONNX Runtime, CUDA, cuDNN, and any redistributed notices must be reviewed before Stack ships an official model pack. Do not ship or commit pretrained weights until redistribution, conversion, and application-use permissions are documented.

Generated local packs should be treated as developer artifacts, not source assets.

## Current Limits

- Real Restormer export requires a local Restormer repo, compatible checkpoint, and PyTorch install.
- Export may need model-specific constructor overrides if the checkpoint was trained with non-default architecture parameters.
- Dynamic H/W export is preferred but may fail depending on PyTorch/ONNX operator support.
- FP16, TensorRT, DirectML, OpenGL/CUDA interop, feathered tile merge, and RAW/CFA neural denoise remain future work.
