# stack-restormer-denoise-v1 Package Template

This directory is source-controlled packaging metadata only. It must never
contain official Restormer checkpoints or converted ONNX weights until the
written authorization and release ledger pass review.

For local development:

1. Convert the two locally obtained checkpoints with
   `tools/restormer/export_verify_restormer.py`.
2. Build `StackModelService` so the self-contained Windows ML runtime is
   present beside it.
3. Run `tools/restormer/stage_development_package.py` with the build folder,
   Windows ML NuGet root, converted models, equivalence report, and local
   Restormer/SIDD license files. It creates the manifest, provenance record,
   hashes, notices, and SPDX SBOM without packaging Python or PyTorch.
4. Launch Stack with `tools/restormer/run_development_stack.ps1`, passing the
   staged directory as `-PackageDirectory`. The launcher sets
   `STACK_RESTORMER_DENOISE_DIR` and
   `STACK_ALLOW_LOCAL_RESTORMER_PACKAGE=1` only for that Stack process.
5. Use `-ForceCpu` only as a development fallback/diagnostic; DirectML is the
   default.

`manifest.development.template.json` remains a readable schema example. Do not
point Stack at this source-controlled template directory.
