# SAM 2.1 Small Artifact and License Ledger

## Rule

This file must describe exact files, not project names in the abstract.
`TBD` means the artifact is not approved and must not ship.

No model or runtime artifacts were downloaded or added during workspace
preparation.

## Candidate Identity

| Field | Current value |
|---|---|
| Stack pack id | `meta-sam2.1-small-stack-v1` (provisional) |
| Capability | `prompted-image-segmentation` |
| Upstream project | Meta SAM 2 |
| Candidate checkpoint | `sam2.1_hiera_small.pt` |
| Upstream source URL | `https://github.com/facebookresearch/sam2` |
| Frozen commit/tag | TBD |
| Retrieval date | TBD |
| Original checkpoint SHA-256 | TBD |
| Original checkpoint size | TBD |
| Configuration source/path/hash | TBD |
| Reference license | Apache-2.0; exact frozen file/hash TBD |
| Upstream NOTICE files | TBD |
| Model-card/provenance snapshot | TBD |
| Commercial distribution approval | Not reviewed |

## Artifact Inventory

| Artifact | Source | License | Original hash | Pack hash | Modified? | Status |
|---|---|---|---|---|---|---|
| SAM 2.1 Small checkpoint | Official Meta source, exact URL TBD | Apache-2.0 statement; frozen text TBD | TBD | N/A | No | Not acquired |
| SAM 2.1 Small configuration | Official repository at frozen commit | Apache-2.0 | TBD | TBD | Maybe normalized | Not acquired |
| Encoder ONNX | Reproducible conversion from frozen source/checkpoint | Derived-license review TBD | N/A | TBD | Yes | Not produced |
| Prompt decoder ONNX | Reproducible conversion from frozen source/checkpoint | Derived-license review TBD | N/A | TBD | Yes | Not produced |
| ONNX Runtime | Official pinned release | MIT plus third-party notices | TBD | TBD | No | Not selected |
| GPU execution provider | Official pinned release/system component | TBD | TBD | TBD | No | Not selected |
| `StackSubjectService.exe` | Stack source/build | Stack proprietary license plus linked notices | N/A | TBD | Stack-authored | Not implemented |
| Package manifest | Stack release process | Stack metadata | N/A | TBD | Stack-authored | Not designed |
| SPDX SBOM | Generated from frozen inventory | Data record | N/A | TBD | Generated | Not created |

## Conversion Record

Complete before accepting any ONNX artifact:

| Field | Value |
|---|---|
| Conversion source commit | TBD |
| Conversion tool and version | TBD |
| Conversion script source/license/hash | TBD |
| Exact command line | TBD |
| Python/PyTorch environment lock | TBD |
| ONNX opset | TBD |
| Input/output names, shapes, and dtypes | TBD |
| Dynamic versus fixed dimensions | TBD |
| Simplification/optimization passes | TBD |
| Numerical comparison corpus | TBD |
| Maximum/mean logit and mask difference | TBD |
| Reviewer and review date | TBD |

Generated artifacts must be reproducible from the frozen record. Community
ONNX exports may be used for research only until their source, license, and
numerical behavior are independently verified.

## Required License Packet

The eventual installed pack must contain:

- the exact SAM Apache-2.0 license;
- upstream notices required by the frozen source;
- ONNX Runtime MIT license and third-party notices;
- execution-provider licenses and notices;
- licenses for any incorporated preprocessing or postprocessing code;
- conversion/modification disclosure;
- an aggregated pack notice identifying the model as third-party;
- source and provenance references sufficient to reproduce the inventory.

## Release Approval

| Gate | Status | Evidence |
|---|---|---|
| Code license verified | Not started | TBD |
| Checkpoint license verified | Not started | TBD |
| Runtime/provider redistribution verified | Not started | TBD |
| Transitive dependency inventory complete | Not started | TBD |
| Training provenance reviewed | Not started | TBD |
| Patent/trademark review complete | Not started | TBD |
| Conversion reproducible | Not started | TBD |
| SBOM complete | Not started | TBD |
| Security review complete | Not started | TBD |
| Legal release approval | Not started | TBD |

This ledger is engineering evidence, not legal advice.
