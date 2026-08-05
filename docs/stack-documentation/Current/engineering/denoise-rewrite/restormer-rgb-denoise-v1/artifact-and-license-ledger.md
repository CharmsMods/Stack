# Artifact and License Ledger

Last updated: July 24, 2026.

Package ID: `stack-restormer-denoise-v1`

| Artifact | Intended source | License / permission | Hash | Release state |
|---|---|---|---|---|
| `models/restormer-real-photo-fp32.onnx` | Official Real Denoising checkpoint converted by Stack tooling | Restormer code MIT; separate written checkpoint conversion/commercial redistribution authorization required | `dbbae6c2524464aab16c54fde15c031a6d4126ed7159a8f0bc878050bf9309ec` | Local development only; public redistribution prohibited |
| `models/restormer-gaussian-blind-fp32.onnx` | Official Gaussian Color Blind checkpoint converted by Stack tooling | Same separate checkpoint authorization required | `e25cc133818f848eed7bb12f742f062ff88336576471f7fe892e120998d8c086` | Local development only; public redistribution prohibited |
| `StackModelService.exe` | Stack source | Stack proprietary | Frozen in each development manifest | Development only |
| `onnxruntime.dll` | Microsoft Windows ML NuGet 2.1.70 | MIT / Microsoft notices from frozen package | `f64c098b67eee3c769416564c4abc32e605eaa9a1804e41c03e74b4713d04333` | Development package; notice review retained |
| `DirectML.dll` | Microsoft Windows ML NuGet 2.1.70 | Microsoft notices from frozen package | `7ae717a1304253c71f0793e5d8a172ad779934e7465b0648b64013da832fd8ae` | Development package; notice review retained |
| `Microsoft.Windows.AI.MachineLearning.dll` | Microsoft Windows ML NuGet 2.1.70 | Microsoft notices from frozen package | `9439653d0bbba26414e3f5356d23efb8e70d49b3d3fd07ace88b2a5e3d6423c3` | Development package; notice review retained |
| Restormer license | Official Restormer repository commit `68dc6ac472db26f16361150cb7a96a1bc87da93f` | MIT for repository software | `f1196706e2eb406366589085ece6786b93eee973b95d7760b397b68c2b411353` | Included |
| SIDD license source record | Official SIDD project page | Page states MIT for SIDD dataset/code; does not settle checkpoint redistribution | Frozen in development manifest | Included as scope-limited record |
| Windows ML / ONNX Runtime notices | Frozen NuGet package 2.1.70 | Package license and third-party notices | Frozen in development manifest | Included |
| Checkpoint authorization | Rights holder correspondence | Written grant | TBD | Missing |
| Conversion record | Stack conversion harness output | Data record | Frozen in development manifest | Produced |
| PyTorch/ONNX equivalence report | Stack verification harness output | Data record | Frozen in development manifest | Passed |
| SPDX SBOM | Generated from frozen package | Data record | Frozen in development manifest | Produced |
| Package signature | Stack release key | Signature | TBD | Missing |

The manifest must enumerate and hash every executable, model, runtime, license,
notice, provenance record, conversion report, and SBOM. A file being covered
by an open-source code license does not prove that a separately hosted trained
checkpoint may be commercially redistributed or converted.
