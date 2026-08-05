#!/usr/bin/env python3
"""Stage a complete local-only stack-restormer-denoise-v1 package.

This tool copies only frozen runtime/model/legal artifacts into the package.
Python, PyTorch, and the source checkpoint files are never included.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


PACKAGE_ID = "stack-restormer-denoise-v1"
ADAPTER_VERSION = "restormer-rgb-adapter-v2"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_file(path: Path, label: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"{label} not found: {resolved}")
    return resolved


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def artifact(root: Path, relative: str) -> dict[str, str]:
    path = root / relative
    return {"path": relative, "sha256": sha256_file(path)}


def find_equivalence_model(
    report: dict[str, Any], kind: str
) -> dict[str, Any]:
    for model in report.get("models", []):
        if model.get("kind") == kind:
            return model
    raise ValueError(f"Equivalence report does not contain {kind}")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--windows-ml-root", type=Path, required=True)
    parser.add_argument("--real-model", type=Path, required=True)
    parser.add_argument("--gaussian-blind-model", type=Path, required=True)
    parser.add_argument("--equivalence-report", type=Path, required=True)
    parser.add_argument("--restormer-license", type=Path, required=True)
    parser.add_argument("--sidd-license", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--package-version", default="1.0.0-dev")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    output = arguments.output_dir.resolve()
    if output.exists():
        raise FileExistsError(
            f"Refusing to overwrite an existing package directory: {output}"
        )

    build_dir = arguments.build_dir.resolve()
    winml_root = arguments.windows_ml_root.resolve()
    service = require_file(
        build_dir / "StackModelService.exe", "StackModelService"
    )
    runtime_sources = {
        "onnxruntime.dll": require_file(
            build_dir / "onnxruntime.dll", "ONNX Runtime"
        ),
        "DirectML.dll": require_file(
            build_dir / "DirectML.dll", "DirectML"
        ),
        "Microsoft.Windows.AI.MachineLearning.dll": require_file(
            build_dir / "Microsoft.Windows.AI.MachineLearning.dll",
            "Windows ML API",
        ),
    }
    real_model = require_file(arguments.real_model, "Real Photo ONNX model")
    gaussian_model = require_file(
        arguments.gaussian_blind_model, "Gaussian Blind ONNX model"
    )
    equivalence_path = require_file(
        arguments.equivalence_report, "equivalence report"
    )
    restormer_license = require_file(
        arguments.restormer_license, "Restormer license"
    )
    sidd_license = require_file(arguments.sidd_license, "SIDD license")
    winml_license = require_file(winml_root / "license.txt", "Windows ML license")
    winml_notices = require_file(
        winml_root / "ThirdPartyNotices.txt", "Windows ML third-party notices"
    )

    report = json.loads(equivalence_path.read_text(encoding="utf-8"))
    if not report.get("passed", False):
        raise ValueError("The PyTorch-versus-ONNX equivalence report did not pass")
    expected = {
        "real-photo": sha256_file(real_model),
        "gaussian-blind": sha256_file(gaussian_model),
    }
    for kind, actual_hash in expected.items():
        record = find_equivalence_model(report, kind)
        if record.get("onnx_sha256", "").lower() != actual_hash:
            raise ValueError(f"{kind} does not match the equivalence report")

    staging = output.with_name(
        f".{output.name}.staging-{uuid.uuid4().hex}"
    )
    staging.mkdir(parents=True)
    try:
        copy_file(service, staging / "StackModelService.exe")
        for name, source in runtime_sources.items():
            copy_file(source, staging / name)
        copy_file(
            real_model,
            staging / "models" / "restormer-real-photo-fp32.onnx",
        )
        copy_file(
            gaussian_model,
            staging / "models" / "restormer-gaussian-blind-fp32.onnx",
        )
        copy_file(
            restormer_license, staging / "licenses" / "Restormer-MIT.txt"
        )
        copy_file(
            sidd_license,
            staging / "licenses" / "SIDD-License-Source-Record.txt",
        )
        windows_notice = (
            "Microsoft Windows AI Machine Learning package license\n"
            "======================================================\n\n"
            + winml_license.read_text(encoding="utf-8", errors="replace")
            + "\n\nThird-party notices\n===================\n\n"
            + winml_notices.read_text(encoding="utf-8", errors="replace")
        )
        notice_path = staging / "licenses" / "WindowsML-NOTICES.txt"
        notice_path.parent.mkdir(parents=True, exist_ok=True)
        notice_path.write_text(windows_notice, encoding="utf-8")

        provenance = staging / "provenance"
        provenance.mkdir(parents=True, exist_ok=True)
        packaged_report = {
            **report,
            "restormerRepository": "official Restormer repository",
            "models": [
                {
                    **model,
                    "source_checkpoint": Path(
                        model.get("source_checkpoint", "checkpoint")
                    ).name,
                    "onnx_path": Path(
                        model.get("onnx_path", "model.onnx")
                    ).name,
                }
                for model in report.get("models", [])
            ],
        }
        (provenance / "equivalence-report.json").write_text(
            json.dumps(packaged_report, indent=2) + "\n",
            encoding="utf-8",
        )
        conversion_record = {
            "schemaVersion": 1,
            "restormerRepository": report.get("restormerRepository", ""),
            "restormerCommit": report.get("restormerCommit", ""),
            "torchVersion": report.get("torchVersion", ""),
            "onnxVersion": report.get("onnxVersion", ""),
            "onnxRuntimeVersion": report.get("onnxRuntimeVersion", ""),
            "opset": report.get("opset", 0),
            "models": [
                {
                    "kind": model.get("kind", ""),
                    "sourceCheckpoint": Path(
                        model.get("source_checkpoint", "checkpoint")
                    ).name,
                    "sourceSha256": model.get("source_sha256", ""),
                    "onnxSha256": model.get("onnx_sha256", ""),
                }
                for model in report.get("models", [])
            ],
        }
        (provenance / "conversion-record.json").write_text(
            json.dumps(conversion_record, indent=2) + "\n",
            encoding="utf-8",
        )

        sbom_files = []
        verification_hashes = []
        for path in sorted(p for p in staging.rglob("*") if p.is_file()):
            relative = path.relative_to(staging).as_posix()
            sha1 = sha1_file(path)
            verification_hashes.append(sha1)
            sbom_files.append(
                {
                    "fileName": relative,
                    "SPDXID": "SPDXRef-File-" + hashlib.sha1(
                        relative.encode("utf-8")
                    ).hexdigest(),
                    "checksums": [
                        {
                            "algorithm": "SHA1",
                            "checksumValue": sha1,
                        },
                        {
                            "algorithm": "SHA256",
                            "checksumValue": sha256_file(path),
                        }
                    ],
                    "licenseConcluded": "NOASSERTION",
                    "copyrightText": "NOASSERTION",
                }
            )
        sbom = {
            "spdxVersion": "SPDX-2.3",
            "dataLicense": "CC0-1.0",
            "SPDXID": "SPDXRef-DOCUMENT",
            "name": f"{PACKAGE_ID}-{arguments.package_version}",
            "documentNamespace": (
                f"https://stack.local/spdx/{PACKAGE_ID}/"
                f"{arguments.package_version}/{uuid.uuid4()}"
            ),
            "creationInfo": {
                "created": datetime.now(timezone.utc).strftime(
                    "%Y-%m-%dT%H:%M:%SZ"
                ),
                "creators": ["Tool: Stack Restormer development packager"],
            },
            "packages": [
                {
                    "name": PACKAGE_ID,
                    "SPDXID": "SPDXRef-Package",
                    "versionInfo": arguments.package_version,
                    "downloadLocation": "NOASSERTION",
                    "filesAnalyzed": True,
                    "packageVerificationCode": {
                        "packageVerificationCodeValue": hashlib.sha1(
                            "".join(sorted(verification_hashes)).encode("ascii")
                        ).hexdigest()
                    },
                    "licenseConcluded": "NOASSERTION",
                    "licenseDeclared": "NOASSERTION",
                    "copyrightText": "NOASSERTION",
                }
            ],
            "files": sbom_files,
            "relationships": [
                {
                    "spdxElementId": "SPDXRef-DOCUMENT",
                    "relationshipType": "DESCRIBES",
                    "relatedSpdxElement": "SPDXRef-Package",
                },
                *[
                    {
                        "spdxElementId": "SPDXRef-Package",
                        "relationshipType": "CONTAINS",
                        "relatedSpdxElement": item["SPDXID"],
                    }
                    for item in sbom_files
                ],
            ],
        }
        (staging / "sbom.spdx.json").write_text(
            json.dumps(sbom, indent=2) + "\n", encoding="utf-8"
        )

        manifest = {
            "schemaVersion": 1,
            "packageId": PACKAGE_ID,
            "packageVersion": arguments.package_version,
            "protocolVersion": 1,
            "adapterVersion": ADAPTER_VERSION,
            "developmentPackage": True,
            "authorizationStatus": "development-only",
            "service": artifact(staging, "StackModelService.exe"),
            "runtimeArtifacts": [
                artifact(staging, name) for name in runtime_sources
            ],
            "legalArtifacts": [
                artifact(staging, "licenses/Restormer-MIT.txt"),
                artifact(staging, "licenses/SIDD-License-Source-Record.txt"),
                artifact(staging, "licenses/WindowsML-NOTICES.txt"),
                artifact(staging, "provenance/conversion-record.json"),
                artifact(staging, "provenance/equivalence-report.json"),
                artifact(staging, "sbom.spdx.json"),
            ],
            "models": [
                {
                    "kind": "real-photo",
                    **artifact(
                        staging,
                        "models/restormer-real-photo-fp32.onnx",
                    ),
                    "inputName": "input",
                    "outputName": "output",
                },
                {
                    "kind": "gaussian-blind",
                    **artifact(
                        staging,
                        "models/restormer-gaussian-blind-fp32.onnx",
                    ),
                    "inputName": "input",
                    "outputName": "output",
                },
            ],
        }
        (staging / "manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
        staging.rename(output)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    print(f"Staged local development package: {output}")
    print("Set STACK_RESTORMER_DENOISE_DIR to that directory.")
    print("Set STACK_ALLOW_LOCAL_RESTORMER_PACKAGE=1 for local testing.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Restormer package staging failed: {error}", file=sys.stderr)
        sys.exit(1)
