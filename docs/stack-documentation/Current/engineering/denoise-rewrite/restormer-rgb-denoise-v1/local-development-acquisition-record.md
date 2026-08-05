# Local Development Acquisition Record

Last updated: July 24, 2026.

This record covers local evaluation artifacts only. None of the checkpoint or
converted model files are tracked by Git or approved for public redistribution.

## Frozen upstream

- Repository: `https://github.com/swz30/Restormer`
- Commit: `68dc6ac472db26f16361150cb7a96a1bc87da93f`
- Repository license file: MIT
- Official denoising documentation:
  `https://github.com/swz30/Restormer/blob/main/Denoising/README.md`

## Official checkpoints

| Model | Official Google Drive file ID | Size | SHA-256 |
|---|---|---:|---|
| `real_denoising.pth` | `1FF_4NTboTWQ7sHCq4xhyLZsSl0U0JfjH` | 104,611,957 bytes | `4cae18bed8a291b9a5deeeab48756bbe6f61fcf7f31a4cca0969b24608601387` |
| `gaussian_color_denoising_blind.pth` | `1yMDOlLYUVruC9ytXDKQdQICnhnTGq09A` | 104,611,957 bytes | `4e2314dae77bb907184da6e2ac625b921491d6b33de26a52512eff1c6bb37fbd` |

The public download links support acquisition for local evaluation. They are
not being treated as an explicit grant to commercially redistribute the
checkpoint files or converted derivatives.

## Conversion

- Tool: `tools/restormer/export_verify_restormer.py`
- PyTorch: `2.13.0+cpu`
- ONNX: `1.22.0`
- ONNX Runtime: `1.28.0`
- ONNX opset: 18
- Output: self-contained FP32 ONNX; external weight sidecars are rejected
- Required maximum absolute error: `1e-4`

| Model | ONNX SHA-256 | Maximum absolute error | Result |
|---|---|---:|---|
| Real Photo | `dbbae6c2524464aab16c54fde15c031a6d4126ed7159a8f0bc878050bf9309ec` | `7.092952728271484e-06` | Pass |
| Gaussian Blind | `e25cc133818f848eed7bb12f742f062ff88336576471f7fe892e120998d8c086` | `7.748603820800781e-07` | Pass |

## Runtime smoke evidence

Both models completed through `StackModelService.exe`, the versioned named-pipe
protocol, shared-memory image buffers, and the package's own model files.
DirectML and forced-CPU paths both passed. These tiny synthetic checks validate
loading and transport, not photograph quality or full-resolution performance.

The first real-model smoke run exposed a synchronous named-pipe ownership
deadlock. The helper now queues worker completions and lets only its control
thread perform pipe I/O.

## Local paths

All downloaded source, checkpoints, conversions, packages, and the isolated
Python environment are beneath `_workspace/restormer/`, which is excluded by
the repository root `.gitignore`.

The current staged package remains marked:

```text
packageVersion: 1.0.0-dev.6
developmentPackage: true
authorizationStatus: development-only
```

Its ignored local directory is:
`_workspace/restormer/packages/stack-restormer-denoise-v1-dev6/`.

Development package `dev.6` contains the same audited ONNX model artifacts as
`dev.5`; it advances the exact adapter pin to
`restormer-rgb-adapter-v2`. The adapter code, not the model files, changed.

The release gate in `checkpoint-authorization-request.md` remains closed.
