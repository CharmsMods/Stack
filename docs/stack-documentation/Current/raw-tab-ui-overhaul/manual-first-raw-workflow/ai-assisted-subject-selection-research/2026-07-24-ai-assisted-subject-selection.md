# AI-Assisted Subject Selection

- Captured: 2026-07-24 12:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-24-1218-ai-assisted-subject-selection.md`
- Type: research
- Topic: ai-assisted-subject-selection
- Verification: partially-verified

## Plain-Language Answer

This is a meaningful project, but it is not an unreasonable stretch. Stack
already has two important pieces:

1. a working deterministic Local Exposure qualifier and connected-area mask;
2. an optional external ONNX denoise-pack pattern that proves a neural runtime
   and weights do not have to be linked into or embedded in `Stack.exe`.

The AI should solve the one problem the current qualifier cannot solve
reliably: recognizing that two visually similar regions belong to different
semantic objects. It should not be responsible for exposure math, RAW
processing, or continuous image rendering.

Stack can remain proprietary. Permissively licensed third-party components do
not become Stack-owned, but Stack's application code, UI, recipes, mask
combination logic, and package manager remain Stack's proprietary work.
Distribution must accurately attribute the model and runtime and preserve
their required licenses and notices.

## What Adobe Discloses

Adobe's current Lightroom documentation calls Select Subject, Sky,
Background, Objects, People, and Landscape automatic or AI-powered masks.
Adobe identifies Adobe Sensei as the system powering intelligent masking on
at least its Lightroom surfaces.

Adobe does not publicly identify the exact model architecture, checkpoint,
training set, or redistribution terms used for Select Subject. It therefore
provides product and interaction precedent, not reusable model technology.

The most valuable Adobe precedent is the workflow:

```text
automatic proposal
    -> visible mask
    -> Add / Subtract / Intersect
    -> brush, color range, or luminance range refinement
    -> local adjustments
```

That is a better model for Stack than an opaque one-click result. The
selection remains editable and the user can combine semantic and photographic
criteria.

## What Blackmagic Design Discloses

Blackmagic says Magic Mask is powered by the proprietary DaVinci AI Neural
Engine. Its public material describes deep neural networks, machine learning,
object detection, user prompts, tracking/propagation, and mask-finesse
controls. Magic Mask is a Resolve Studio feature.

Blackmagic does not disclose the model architecture, weights, training data,
or a license that would allow Stack to reuse the technology. Resolve again
provides a product pattern rather than an implementation:

```text
point/stroke prompt
    -> semantic person/object mask
    -> tracking or propagation
    -> mask refinement
    -> grading
```

For still RAW images, Stack needs the prompt-and-refine part, not Resolve's
temporal tracking system.

## Recommended Product Direction

The first AI feature should be `AI Refine Selection`, not a promise that Stack
can always find the correct subject automatically.

Recommended gestures:

- Click or rough box: positive prompt for the intended subject.
- Additional click: another positive prompt.
- Alt-click: negative prompt for an excluded area.
- Add, Subtract, and Intersect: combine the AI mask with Stack's existing
  luminance/color/connected-area qualifier.
- Accept: freeze the result as an authored Stack mask.

An automatic `Select Main Subject` action can be tested later. Prompted
selection is more predictable in multiple-person, sports, event, and cluttered
scenes and fits the direct-targeting work already present in RAW Lab.

## Recommended Initial Models

Use a benchmark rather than selecting a model by reputation:

1. **SAM 2.1 Tiny and Small** as the quality and licensing reference.
2. **EfficientSAM-Ti/S** as a likely lower-latency promptable candidate.
3. **MobileSAM** as another compact promptable candidate.
4. **MODNet** only as an optional portrait/hair alpha-matting refinement.
5. **BiRefNet** only as an automatic foreground proposal experiment.

SAM 2 is the cleanest initial legal candidate among these because its official
repository expressly says its model checkpoints, demo code, and training code
are Apache-2.0. This is stronger evidence than a repository badge that may
apply only to source code.

Do not put BRIA RMBG, Ultralytics YOLO, Robust Video Matting, or any
noncommercial/custom-license checkpoint in Stack's default downloadable pack
without a separate commercial agreement and legal review.

## What Must Be Proven Before Shipping

- Exact code license.
- Exact checkpoint/weight license.
- Commercial-use and redistribution permission.
- License and notice obligations for the inference runtime.
- Licenses for conversion/export tools and any code copied from them.
- Licenses for bundled preprocessing, tokenizer, backbone, and custom
  operators.
- Training-data provenance and any model-card restrictions.
- Patent terms and trademark/attribution requirements.
- A reproducible source URL, commit/tag, original hash, converted hash, and
  record of every modification.
- A clean-room package inventory showing that no GPL, AGPL, noncommercial, or
  remotely executed Python entered the official pack accidentally.

A dataset license does not automatically impose the same license on every
trained checkpoint, but unclear or unauthorized training provenance can still
create contractual, copyright, privacy, or commercial risk. It must be
reviewed separately rather than assumed safe.

## Follow-Up

The next work should be an external benchmark harness, not production UI:

1. freeze candidate source commits and weight hashes;
2. complete a dependency and license bill of materials for each candidate;
3. export or obtain audited ONNX graphs;
4. measure first-image encoding time, warm prompt latency, memory, and boundary
   quality on difficult Stack RAW images;
5. reject candidates that cannot meet the legal gate before investing in UI;
6. then specify the versioned helper protocol and package manifest.

## Related Docs

- `model-and-license-matrix.md`
- `integration-architecture.md`
- `source-ledger.md`
- `docs/stack-documentation/Archived/neural-denoise/RESTORMER_ONNX_INTEGRATION.md`
- `docs/stack-documentation/Current/raw-tab-ui-overhaul/manual-first-raw-workflow/local-exposure-direct-targeting-implementation.md`
