# Retired Denoise Artifact Inventory

## Purpose

This is the deletion boundary for the 2026-07-24 denoise retirement. It records
the local ignored research artifacts that existed at the time of the code
cleanup so a later denoise project can evaluate them deliberately.

These files are not tracked application dependencies. None of them was copied,
modified, promoted into a build, or deleted during the retirement pass.

## Stack Custom-Model Playground

Root:

`_workspace/neural_denoise_training/`

Observed contents include:

- a local Python virtual environment;
- PyTorch training, inference, dataset, and tiled-inference code;
- DIV2K/synthetic-noise and optional SIDD-oriented tooling;
- a Flask development dashboard;
- local data, output, and checkpoint directories;
- a NAFNet-style model implementation;
- training and quality-roadmap notes.

The playground is useful R&D source material, but its current README describes
it as a locally hosted playground. It is not evidence of production accuracy,
camera-general noise modeling, stable export, reproducible training, or a
redistributable Stack model.

Observed checkpoints:

| Path | Bytes | SHA-256 |
|---|---:|---|
| `_workspace/neural_denoise_training/checkpoints/best_model.pth` | 93,587,542 | `FB726AA9FD71C0D154414C5B9D183FAB4D1252633604D3AE77C9EDE08BD52B95` |
| `_workspace/neural_denoise_training/checkpoints/latest_model.pth` | 93,589,982 | `2F769B50E9F9414490878D3C8A489E656DFFDD5573EA1505EFB9FD75D5C9CBA0` |

The checkpoint format is a Python/PyTorch serialization format. Do not load an
untrusted replacement merely to inspect it; treat checkpoints as executable
deserialization inputs unless a safe inspection path is established.

## Retired Restormer Test Pack

Root:

`build/denoise/`

Observed core files:

| Path | Bytes | SHA-256 |
|---|---:|---|
| `build/denoise/models/restormer_rgb_denoise.onnx` | 6,008,545 | `B57BB29104AD638AFFA4F4C8E217CB0A280545892D9C1CF6A4DC52092274FEB3` |
| `build/denoise/models/restormer_rgb_denoise.onnx.data` | 104,595,456 | `1E4CACB770F0C310D503F11A8379008375B5911B3D3614A058F3FDFD803C3633` |
| `build/denoise/manifest.json` | 756 | `5EBCF0F5217A72D5B50DA5C865D7AFE0CDD4F4FEA222F3CFE746A8871633F06B` |

The pack also historically contained optional ONNX Runtime/CUDA binaries and
license material. Its presence in a local build tree does not make it a
supported or legally cleared distribution. The active Stack source no longer
discovers or executes this pack.

## Retired Export Tools

Root:

`tools/neural_denoise/`

Observed scripts:

- `create_identity_onnx.py`
- `create_local_test_pack.py`
- `create_restormer_test_pack.py`
- `export_restormer_onnx.py`
- `validate_restormer_onnx.py`

They remain local historical tooling. They target the retired manifest/runtime
contract and must not be used as the foundation of the new system without a
fresh technical and licensing review.

## Tracked Compatibility Surface

The following tracked concepts intentionally remain:

- `NeuralDenoiseTypes` settings serialization for old projects;
- `RawNeuralDenoise` graph kind, payload, sockets, and serializer support;
- `LinearRgbNeuralDenoiseLayer` construction for old RGB graph nodes;
- legacy classical-node construction for existing projects.

The following tracked runtime concepts were intentionally removed:

- neural model discovery/manager;
- ONNX backend loading and inference;
- runtime/provider availability contracts;
- model-pack records used only by the retired backend;
- active browser entries for the retired neural nodes.

## Future Handling

Before reusing any artifact:

1. verify provenance and the exact source revision;
2. identify the code, weights, data, and runtime licenses independently;
3. reproduce model architecture and preprocessing assumptions;
4. test against Stack's defined scene-linear or mosaic contract;
5. establish quality and regression data;
6. put distributable packages behind a new versioned external-package
   interface rather than reviving the retired one.

