#!/usr/bin/env python3
"""Export frozen Restormer checkpoints to FP32 ONNX and verify equivalence.

This tool deliberately requires a local official Restormer checkout and local
checkpoint paths. It does not download or redistribute model artifacts.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np
import onnx
import onnxruntime as ort
import torch


@dataclass(frozen=True)
class ModelRecord:
    kind: str
    source_checkpoint: str
    source_sha256: str
    onnx_path: str
    onnx_sha256: str
    opset: int
    maximum_absolute_error: float
    mean_absolute_error: float
    passed: bool


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def restormer_commit(repository: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repository), "rev-parse", "HEAD"],
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def load_architecture(repository: Path) -> type[torch.nn.Module]:
    architecture = (
        repository / "basicsr" / "models" / "archs" / "restormer_arch.py"
    )
    if not architecture.is_file():
        raise FileNotFoundError(
            f"Restormer architecture not found at {architecture}"
        )
    spec = importlib.util.spec_from_file_location(
        "stack_official_restormer_arch", architecture
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("Could not load the official Restormer architecture")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.Restormer


def build_model(restormer_type: type[torch.nn.Module]) -> torch.nn.Module:
    return restormer_type(
        inp_channels=3,
        out_channels=3,
        dim=48,
        num_blocks=[4, 6, 6, 8],
        num_refinement_blocks=4,
        heads=[1, 2, 4, 8],
        ffn_expansion_factor=2.66,
        bias=False,
        LayerNorm_type="BiasFree",
        dual_pixel_task=False,
    ).eval()


def checkpoint_state(payload: Any) -> dict[str, torch.Tensor]:
    if not isinstance(payload, dict):
        raise TypeError("Checkpoint must contain a state dictionary")
    for key in ("params_ema", "params", "state_dict"):
        candidate = payload.get(key)
        if isinstance(candidate, dict):
            payload = candidate
            break
    if not isinstance(payload, dict):
        raise TypeError("Checkpoint does not contain Restormer parameters")
    return {
        (key[7:] if key.startswith("module.") else key): value
        for key, value in payload.items()
    }


def export_one(
    kind: str,
    checkpoint: Path,
    output: Path,
    restormer_type: type[torch.nn.Module],
    tolerance: float,
) -> ModelRecord:
    model = build_model(restormer_type)
    payload = torch.load(checkpoint, map_location="cpu", weights_only=False)
    missing, unexpected = model.load_state_dict(
        checkpoint_state(payload), strict=False
    )
    if missing or unexpected:
        raise RuntimeError(
            f"{kind} checkpoint mismatch; missing={missing}, "
            f"unexpected={unexpected}"
        )

    generator = torch.Generator(device="cpu").manual_seed(24072026)
    sample = torch.rand((1, 3, 256, 256), generator=generator, dtype=torch.float32)
    output.parent.mkdir(parents=True, exist_ok=True)
    legacy_external_data = output.with_name(output.name + ".data")
    legacy_external_data.unlink(missing_ok=True)
    with torch.inference_mode():
        torch_output = model(sample).cpu().numpy()
        torch.onnx.export(
            model,
            sample,
            output,
            export_params=True,
            do_constant_folding=True,
            external_data=False,
            input_names=["input"],
            output_names=["output"],
            dynamic_axes={
                "input": {0: "batch", 2: "height", 3: "width"},
                "output": {0: "batch", 2: "height", 3: "width"},
            },
            opset_version=18,
        )

    graph = onnx.load(str(output))
    onnx.checker.check_model(graph, full_check=True)
    standard_opsets = [
        item.version for item in graph.opset_import if item.domain in ("", "ai.onnx")
    ]
    if len(standard_opsets) != 1:
        raise RuntimeError(f"{kind} has an ambiguous ONNX opset declaration")
    external_initializers = [
        initializer.name
        for initializer in graph.graph.initializer
        if initializer.data_location == onnx.TensorProto.EXTERNAL
    ]
    if external_initializers:
        raise RuntimeError(
            f"{kind} is not self-contained; external initializers include "
            f"{external_initializers[:3]}"
        )
    session = ort.InferenceSession(
        str(output), providers=["CPUExecutionProvider"]
    )
    onnx_output = session.run(["output"], {"input": sample.numpy()})[0]
    difference = np.abs(torch_output - onnx_output)
    maximum = float(difference.max(initial=0.0))
    mean = float(difference.mean())
    return ModelRecord(
        kind=kind,
        source_checkpoint=str(checkpoint.resolve()),
        source_sha256=sha256_file(checkpoint),
        onnx_path=str(output.resolve()),
        onnx_sha256=sha256_file(output),
        opset=standard_opsets[0],
        maximum_absolute_error=maximum,
        mean_absolute_error=mean,
        passed=maximum <= tolerance,
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--restormer-repo", type=Path, required=True)
    parser.add_argument("--real-checkpoint", type=Path, required=True)
    parser.add_argument("--gaussian-blind-checkpoint", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--tolerance", type=float, default=1.0e-4)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    restormer_type = load_architecture(arguments.restormer_repo)
    records = [
        export_one(
            "real-photo",
            arguments.real_checkpoint,
            arguments.output_dir / "restormer-real-photo-fp32.onnx",
            restormer_type,
            arguments.tolerance,
        ),
        export_one(
            "gaussian-blind",
            arguments.gaussian_blind_checkpoint,
            arguments.output_dir / "restormer-gaussian-blind-fp32.onnx",
            restormer_type,
            arguments.tolerance,
        ),
    ]
    report = {
        "schemaVersion": 1,
        "restormerRepository": str(arguments.restormer_repo.resolve()),
        "restormerCommit": restormer_commit(arguments.restormer_repo),
        "torchVersion": torch.__version__,
        "onnxVersion": onnx.__version__,
        "onnxRuntimeVersion": ort.__version__,
        "opset": records[0].opset if len({record.opset for record in records}) == 1 else 0,
        "tolerance": arguments.tolerance,
        "models": [asdict(record) for record in records],
        "passed": all(record.passed for record in records),
    }
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    report_path = arguments.output_dir / "equivalence-report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
