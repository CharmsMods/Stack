# Neural Denoise System Plan

## Goals

Stack's neural denoise system is optional infrastructure for high-quality, model-backed denoising. It must never be required for startup, image loading, RAW handling, or existing OpenGL processing. If the model pack, manifest, runtime DLLs, or individual model files are missing, nodes stay in the graph, preserve their settings, and pass their input through unchanged with a readable status.

The first implementation pass builds discovery, settings, serialization, graph integration, UI, and honest bypass behavior. It does not ship model weights, ONNX Runtime DLLs, CUDA provider DLLs, or placeholder license files.

## External Model Pack

Models and runtime binaries live outside the executable so Stack's code license can remain separate from model and runtime licenses. The default layout beside the built EXE is:

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
      stack_rgb_denoise_quality.onnx
      stack_rgb_denoise_balanced.onnx
      stack_raw_bayer_denoise.onnx
    licenses/
      ONNXRuntime_LICENSE.txt
      Restormer_LICENSE.txt
      NAFNet_LICENSE.txt
      PMRID_LICENSE.txt
      NVIDIA_NOTICES.txt
```

`STACK_DENOISE_DIR` can override the denoise root for development, testing, or future user-installed packs. Stack then falls back to `<exe>/denoise`, then a development `denoise` folder under the current working directory.

## Licensing Rules

Model and runtime licensing is separate from Stack's source license. A future shipped model pack must have explicit permission for redistribution, conversion to the shipped format, and use inside Stack. Do not ship GPL, non-commercial-only, research-only, or unclear-license models unless that isolation and licensing impact has been reviewed first.

This repository should not contain fake license files for models or runtimes that are not actually distributed. Documentation may describe expected license locations and requirements, but the files themselves should arrive only with a verified pack.

## Planned Model Families

- Restormer-style quality RGB denoise for scene-linear or normal RGB after demosaic.
- NAFNet-style balanced RGB denoise for post-demosaic workflows.
- PMRID-style or NAFNet-Bayer-style RAW/CFA denoise before demosaic, operating on packed Bayer data such as RGGB/BGGR/GRBG/GBRG packed into four channels.

The model manifest describes model type, architecture family, preferred backend, precision, channel layout, tiling support, and license notes. The manifest is versioned so additional metadata can be added without hard-coding one model path into Stack.

## Manifest Example

```json
{
  "version": 1,
  "models": [
    {
      "id": "stack_rgb_denoise_quality",
      "displayName": "Stack RGB Denoise Quality",
      "file": "models/stack_rgb_denoise_quality.onnx",
      "type": "linear_rgb",
      "architecture": "restormer_like",
      "preferredBackend": "onnx_cuda",
      "precision": ["fp32", "fp16"],
      "inputChannels": 3,
      "outputChannels": 3,
      "supportsTiling": true,
      "license": "TBD - do not ship until verified"
    },
    {
      "id": "stack_raw_bayer_denoise",
      "displayName": "Stack RAW Bayer Denoise",
      "file": "models/stack_raw_bayer_denoise.onnx",
      "type": "raw_bayer_packed_4ch",
      "architecture": "pmrid_or_nafnet_bayer_like",
      "preferredBackend": "onnx_cuda",
      "precision": ["fp32", "fp16"],
      "inputChannels": 4,
      "outputChannels": 4,
      "supportsTiling": true,
      "license": "TBD - do not ship until verified"
    }
  ]
}
```

## Runtime Strategy

The runtime layer is backend-based rather than hard-wired to one model. `INeuralDenoiseBackend` is the stable interface for future ONNX Runtime, TensorRT, ncnn, MNN, DirectML, or deliberate CPU fallback implementations.

The first backend, `OnnxDenoiseBackend`, only detects runtime availability. On Windows it probes `onnxruntime.dll` dynamically from the external runtime folder so missing DLLs do not prevent Stack from launching. CUDA provider DLL availability is reported separately. Real inference is intentionally not implemented in this pass.

GPU/CUDA execution is preferred where available, but image quality takes priority over raw speed. Stack should not silently fall back to CPU. If CUDA or GPU inference is unavailable, the UI should report that clearly and require a deliberate future fallback choice.

## Processing Modes

RAW/CFA denoise runs before demosaic on mosaiced RAW data. It requires enough metadata to know that the input is actually mosaiced CFA data, including CFA pattern and black/white level behavior. Linear DNG or unsupported RAW layouts should show an unsupported-input warning and bypass.

Linear RGB denoise runs after demosaic or on ordinary RGB images. It should preserve scene-linear workflow where available, avoid permanent tone mapping or clamping unless a model explicitly requires it, and treat alpha according to the selected policy.

Model output is a denoise estimate, not an unconditional replacement:

```text
modelOutput = neuralDenoise(input)
difference = modelOutput - input
final = input + difference * strength * masks * noiseMap
```

This supports controlled blending, masks, shadow-biased denoise, chroma-heavy cleanup, and preserving luminance texture.

## Quality Modes

- Quality: FP32 preferred, maximum safe overlap, no INT8 quantization, no aggressive visually risky optimizations.
- Balanced: FP16 allowed when supported and visually safe, good tile overlap.
- Fast: more aggressive provider options can be exposed later, but INT8 remains off by default unless tested and explicitly surfaced.

INT8 quantization is not a default because denoise artifacts are highly visible and can damage subtle texture, color, and shadow transitions.

## Tiling

Large images must not assume a full frame fits in VRAM or a model's input window. The foundation stores tile size, overlap, feathering, and full-frame policy so future inference can merge tiles without visible seams and expose quality presets that control overlap.

The first implementation pass stores and serializes tiling settings but does not run real tiled inference.

## Current Implementation

- `NeuralDenoiseManager` resolves the denoise root, loads `manifest.json`, reports missing manifests, model files, runtime DLLs, CUDA provider DLLs, license files, bad manifests, and placeholder runtime state.
- `INeuralDenoiseBackend` defines the future inference interface.
- `OnnxDenoiseBackend` performs optional dynamic runtime detection without linking Stack against ONNX Runtime.
- `Linear RGB Neural Denoise` is a registry-backed advanced layer node with compact graph presentation, detailed ImGui settings, serialization, and pass-through execution.
- `RAW/CFA Neural Denoise` is a RAW-in/RAW-out graph node between `RawSource` and `RawDevelop`, with detailed RAW controls, status reporting, serialization, and pass-through behavior.

## Future Work

- Implement real ONNX Runtime inference with CUDA provider selection.
- Add TensorRT and DirectML backends when their packaging and quality constraints are clear.
- Add OpenGL/CUDA or OpenGL/DirectML interop after correctness is stable; the staging path is acceptable first.
- Add user-loadable model packs and manifest validation UI.
- Add generated noise maps, external mask inputs, shadow-only denoise, and chroma/luma difference previews.
- Add training/fine-tuning notes and provenance tracking for official Stack model packs.
