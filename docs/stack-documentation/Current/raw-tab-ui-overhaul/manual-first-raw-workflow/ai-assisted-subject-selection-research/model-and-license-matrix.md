# Model and License Matrix

## Decision Scale

- **Green candidate:** licensing evidence is unusually clear enough to justify
  a full artifact audit and benchmark. It is not automatic approval to ship.
- **Yellow candidate:** technically relevant, but weight, dependency,
  provenance, or redistribution details need more work.
- **Red for official pack:** do not distribute with proprietary Stack under the
  default upstream terms.

## Shortlist

| Candidate | Intended role | Published license evidence | Initial status | Main concern |
|---|---|---|---|---|
| Meta SAM 2 / 2.1 | Point/box-prompted general object selection | Official repository explicitly says checkpoints and code are Apache-2.0 | Green candidate | Windows production runtime and ONNX conversion must be engineered and audited |
| EfficientSAM | Compact point-prompted selection | Official repository is Apache-2.0 and includes checkpoints | Yellow-green | Freeze and audit every checkpoint and ONNX/export artifact; benchmark boundary quality |
| MobileSAM | Compact SAM-compatible point/box selection | Official repository shows Apache-2.0, ships a checkpoint, and documents ONNX export | Yellow-green | Confirm checkpoint provenance and all inherited SAM/TinyViT components |
| MODNet | Portrait/hair alpha-matting refinement | Official repository expressly licenses code, models, and demos under Apache-2.0 | Green candidate for a narrow optional role | Portrait-only; not a general subject selector |
| BiRefNet | Automatic salient-foreground proposal | Official GitHub code and official Hugging Face weights are marked MIT | Yellow | Not click-prompted; dependency/backbone and training-data audit remains |
| U2-Net | Older salient-object/human segmentation fallback | Official repository is Apache-2.0 | Yellow/low priority | Older quality and checkpoint provenance need review |

## Strongest Core Candidate: SAM 2.1

Why it fits Stack:

- accepts positive/negative points and boxes;
- supports iterative correction;
- separates image embedding from prompt decoding, which allows the expensive
  image work to be cached while mouse prompts update;
- can return multiple plausible masks and confidence scores;
- directly addresses low color-contrast boundaries where Stack's current
  qualifier lacks semantic understanding.

Why it is not an immediate implementation:

- Meta's reference implementation is PyTorch-oriented and its Windows setup
  guidance is not a native production deployment recipe;
- an ONNX conversion must be verified numerically and licensed as its own
  artifact;
- the Tiny and Small checkpoints still need real measurements on Stack's
  supported hardware;
- the SA-V/SA-1B dataset terms are separate from the checkpoint license and
  should be recorded in the provenance review even though the repository
  expressly licenses the checkpoints under Apache-2.0.

## EfficientSAM and MobileSAM

These are valuable because the product needs fast repeated prompts more than
it needs automatic generation of every object in the image.

Both should be tested against the same difficult images as SAM 2.1. A smaller
model is preferable only if it reduces real end-to-end latency without
creating visibly worse hair, netting, racquets, limbs, foliage, backlighting,
or same-color foreground/background boundaries.

Repository-level Apache-2.0 labels are encouraging but not the end of the
review. The frozen pack must identify the exact checkpoint source, inherited
components, conversion script, runtime, and every copied file.

## Narrow Refiners

### MODNet

MODNet is a real-time, RGB-only portrait matting model. Its official repository
explicitly includes code and models in the Apache-2.0 grant. It may be useful
after a general person mask to create a softer portrait alpha around hair.

It should not be presented as general subject selection. It is specialized for
people and may fail on sports equipment, pets, products, or groups.

### BiRefNet

BiRefNet produces a detailed foreground/background mask and may be useful for
an automatic `Propose Main Subject` action. It does not naturally answer the
question "which one of these several people did the user click?" as directly
as a promptable SAM-family model.

Its official model page identifies the weights as MIT, but a shipping decision
still needs a frozen dependency, backbone, checkpoint, and training-data
review.

## Candidates to Exclude From the Default Pack

### BRIA RMBG

BRIA's current official model page describes the downloadable RMBG-2.0 weights
as noncommercial and says commercial/self-hosted use requires an agreement.
This is not appropriate for Stack's freely redistributable official model pack
under the default terms.

It could be reconsidered only with a signed commercial license that explicitly
covers closed-source desktop distribution, updates, user count, territories,
and continued use after the agreement ends.

### Ultralytics YOLO

Ultralytics says its trained models default to AGPL-3.0 and offers an
Enterprise license for proprietary embedding. Do not distribute those models
or code in Stack's official pack under the default AGPL terms.

An Enterprise agreement could change the answer, but promptable segmentation
is a closer fit than object detection for the first Stack feature anyway.

### Robust Video Matting

The official project is GPL-3.0. Even though it is technically strong for
people in video, it is the wrong legal and functional fit for Stack's first
still-image selector.

A separate process is not a magic exemption from copyleft analysis. The Free
Software Foundation's own FAQ says sufficiently intimate communication and
shared complex structures can still make programs a combined work. Avoid this
question entirely when good permissive alternatives exist.

## Runtime

ONNX Runtime remains the preferred first runtime:

- its official source license is MIT;
- Stack already dynamically loads its C API for optional denoise packs;
- CPU fallback is available;
- DirectML provides broad DirectX 12 GPU coverage on Windows.

Microsoft now describes DirectML as sustained engineering and recommends WinML
for new Windows development. The benchmark should compare:

1. ONNX Runtime CPU;
2. ONNX Runtime DirectML using a pinned supported build;
3. WinML if its native deployment and operator coverage fit Stack's helper.

CUDA/TensorRT should be a later optional acceleration pack, not the first
shipping dependency. Their redistributable components and version matrix add
vendor-specific licensing, download size, driver, and support obligations.

## Approval Rule

The package manager must use an allowlist. A manifest declaring
`license = Apache-2.0` is not enough. Stack's release process should approve a
specific tuple:

```text
pack id
pack version
model source commit/tag
original weight hash
converted model hash
runtime version and hash
license inventory hash
protocol version
Stack reviewer/legal approval record
```

Anything else stays unavailable even if a user drops files into the package
directory.
