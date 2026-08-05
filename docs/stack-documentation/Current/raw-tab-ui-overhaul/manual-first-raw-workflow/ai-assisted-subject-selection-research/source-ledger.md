# Source Ledger

All web sources were checked on July 24, 2026. Prefer the exact frozen license
and source files included with a release over this research-time summary.

## Vendor Product Behavior

- Adobe Lightroom Classic masking:
  <https://helpx.adobe.com/lightroom-classic/help/masking.html>
- Adobe Lightroom desktop masking:
  <https://helpx.adobe.com/lightroom/desktop/edit-photos/masking.html>
- Adobe Lightroom mobile masking and Adobe Sensei:
  <https://helpx.adobe.com/lightroom/mobile/masks-and-selective-adjustments/masking-tools.html>
- Blackmagic Design DaVinci Resolve and AI Neural Engine:
  <https://www.blackmagicdesign.com/products/davinciresolve>
- Blackmagic Design Resolve 20 New Features Guide:
  <https://documents.blackmagicdesign.com/SupportNotes/DaVinci_Resolve_20_New_Features_Guide.pdf>

## Candidate Models

- Meta SAM 2 official repository and checkpoint license statement:
  <https://github.com/facebookresearch/sam2>
- SAM 2 Apache-2.0 license:
  <https://github.com/facebookresearch/sam2/blob/main/LICENSE>
- EfficientSAM official repository:
  <https://github.com/yformer/EfficientSAM>
- MobileSAM official repository:
  <https://github.com/ChaoningZhang/MobileSAM>
- MODNet official repository:
  <https://github.com/ZHKKKe/MODNet>
- BiRefNet official source:
  <https://github.com/ZhengPeng7/BiRefNet>
- BiRefNet official weights:
  <https://huggingface.co/ZhengPeng7/BiRefNet>
- Robust Video Matting official GPL repository:
  <https://github.com/PeterL1n/RobustVideoMatting>
- BRIA RMBG-2.0 official model page and commercial-use restriction:
  <https://huggingface.co/briaai/RMBG-2.0>
- Ultralytics licensing:
  <https://www.ultralytics.com/license>

## Runtime and License Interpretation

- ONNX Runtime official MIT license:
  <https://github.com/microsoft/onnxruntime/blob/main/LICENSE>
- ONNX Runtime execution providers:
  <https://onnxruntime.ai/docs/execution-providers/>
- ONNX Runtime DirectML provider:
  <https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html>
- ONNX Runtime Windows/WinML guidance:
  <https://onnxruntime.ai/docs/get-started/with-windows.html>
- Apache License 2.0:
  <https://www.apache.org/licenses/LICENSE-2.0>
- Apache guidance on LICENSE and NOTICE:
  <https://www.apache.org/legal/apply-license.html>
- Open Source Initiative MIT license:
  <https://opensource.org/license/mit>
- GNU GPL FAQ on programs, plugins, and process boundaries:
  <https://www.gnu.org/licenses/gpl-faq.html#GPLPlugins>

The GNU FAQ reflects the Free Software Foundation's interpretation and is not a
court ruling or substitute for legal advice.

## Stack Sources Reviewed

- `LICENSE`
- `THIRD_PARTY_NOTICES.md`
- `tools/create_release.ps1`
- `installer/StackInstaller.iss`
- `docs/stack-documentation/Archived/neural-denoise/RESTORMER_ONNX_INTEGRATION.md`
- retired `src/NeuralDenoise/NeuralDenoiseManager.cpp` from repository history
- `src/NeuralDenoise/NeuralDenoiseTypes.h` compatibility schema
- retired `src/NeuralDenoise/OnnxDenoiseBackend.cpp` from repository history

## Verification Boundary

Verified:

- vendor-disclosed product behavior;
- repository/model-page license labels and express checkpoint statements;
- Stack's current optional denoise-pack and release-notice patterns.

Not yet verified:

- the complete transitive dependency and training-data provenance of each
  candidate;
- the legal effect of each model license in every target jurisdiction;
- a native ONNX conversion and numerical parity;
- candidate performance on Stack's real RAW corpus and supported hardware;
- the final signed package format and package-manager implementation.
