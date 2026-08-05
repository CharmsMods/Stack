# Integration and Packaging Architecture

## Learn From, but Do Not Revive, Stack's Retired Pattern

Stack previously resolved an optional `denoise` directory, read a manifest,
checked model and license-file presence, and dynamically loaded ONNX Runtime.
That experiment was retired on 2026-07-24. It remains evidence that the base
application can be independent from neural assets, not a current API to reuse.

Subject selection should generalize the idea into a real package system rather
than revive `NeuralDenoiseManager` or add another denoise model type.

The current denoise manifest is not sufficient for downloaded executable
content. It does not provide the complete signed allowlist, artifact hashes,
provenance, or atomic update/rollback behavior required here.

## Recommended Separation

Use all three separations:

1. **Separate files:** never embed model weights or the neural runtime in
   `Stack.exe`.
2. **Separate process:** launch a versioned Stack-owned selection helper.
3. **Separate optional package:** install the runtime/model/licenses only when
   the user asks for AI selection.

```text
Stack.exe
  proprietary UI, recipe, RAW pipeline, masks, fusion, undo, save/export
       |
       | local authenticated IPC; fixed versioned messages
       v
StackSubjectService.exe
  Stack-owned adapter; no UI; no network; no arbitrary plugin loading
       |
       | fixed tensor contract
       v
approved ONNX Runtime + approved ONNX model
```

The helper does not need to be open source merely because it uses MIT or
Apache-2.0 components. The third-party license files and notices remain
available to the user, and Stack accurately identifies those components as
third-party.

The separate process is mainly for crash, memory, GPU, update, and security
isolation. Because the official packs should be permissive, Stack should not
rely on process separation as a theory for avoiding GPL/AGPL obligations.

## Data Contract

Keep the protocol narrow and versioned:

### Load image

- image generation and source fingerprint;
- fixed-size RGB analysis proxy;
- width, height, row stride, transfer description;
- optional current deterministic qualifier as an R8 prior.

### Prompt

- request generation;
- positive points;
- negative points;
- optional bounding box;
- previous low-resolution mask logits, when supported.

### Result

- request generation;
- R8 or R16 mask at the declared proxy size;
- confidence/quality fields with documented semantics;
- timing and provider diagnostics;
- no arbitrary serialized objects or executable responses.

Shared memory is reasonable for image and mask buffers, with small control
messages over a local pipe. Validate dimensions, strides, byte counts, and
generation IDs before mapping or copying.

## Model Input Domain

Do not feed Bayer mosaic values or the mutable final preview into a model
trained on ordinary photographs.

Create a stable `selection-analysis proxy`:

```text
RAW decode/normalization
  -> white balance
  -> demosaic
  -> camera-to-working-space transform
  -> fixed neutral analysis view
  -> model RGB proxy
```

Exclude the Local Exposure edit being authored and other rapidly changing
creative edits from the model input. Otherwise the mask could change merely
because the user changed exposure.

The neutral analysis view must be versioned and deterministic. Store its
fingerprint with the cached embedding and authored mask provenance.

## Interaction Model

Do not run the neural encoder on every hover event.

1. Stack's current qualifier produces the immediate hover outline.
2. The user explicitly invokes AI refinement by clicking, boxing, or choosing
   `Select Subject`.
3. The helper computes or reuses the analysis-proxy embedding.
4. Positive and negative prompts update the decoder.
5. Stack shows the proposed outline without modifying the recipe.
6. Add/Subtract/Intersect combines the semantic mask with Stack's existing
   luminance/color/connected-area mask.
7. Accept freezes an authored Stack mask.
8. Local Exposure dragging uses the accepted mask and existing renderer; it
   never reruns inference.

This preserves real-time editing after selection and makes projects independent
of a later model update or uninstalled pack.

## Full-Resolution Boundary

The neural model should operate on a bounded proxy, initially around
1024-1536 pixels on the longest side, subject to measurement. The final mask
should be refined to source coordinates using Stack-controlled edge-aware
upsampling.

For portraits, an optional MODNet pass may supply soft hair alpha. Do not
convert every general-object mask into a portrait matte or imply that a crisp
semantic edge and an opacity matte mean the same thing.

Store:

- the accepted full-resolution or tiled mask representation;
- model pack id/version and model hash as provenance;
- analysis-proxy fingerprint;
- user prompts and mask operations if useful for recompute;
- a baked result that remains authoritative if the model is unavailable.

Exports use the baked mask, not live inference.

## Package Shape

Suggested installed layout:

```text
packages/
  subject-selection/
    meta-sam2.1-small-stack-v1/
      manifest.json
      StackSubjectService.exe
      runtimes/
        onnxruntime.dll
      models/
        encoder.onnx
        decoder.onnx
      licenses/
        Apache-2.0.txt
        ONNX-Runtime-MIT.txt
        THIRD_PARTY_NOTICES.txt
      provenance/
        upstream.json
        conversion.json
        sbom.spdx.json
      hashes.json
      signature.json
```

The helper may be shared across compatible packs instead of duplicated, but
the installed inventory must still make version and responsibility explicit.

## Required Manifest Fields

- schema version, pack id, version, and capability;
- minimum/maximum supported Stack protocol versions;
- immutable model and runtime file paths, sizes, and SHA-256 hashes;
- package signature and signing-key id;
- source URLs, upstream commits/tags, and retrieval dates;
- code license, weight license, runtime license, and notice file paths;
- commercial-use and redistribution approval record;
- model input/output contract and normalization;
- runtime provider and required operator set/opset;
- conversion tool, version, command, source, modifications, and numerical
  verification result;
- training-data/model-card provenance links;
- supported architectures, OS versions, memory expectations, and fallback;
- privacy statement and whether any network access occurs.

## Package Manager Behavior

- Base Stack installer ships without the optional weights.
- Package Manager shows download size, hardware support, license summary, and
  third-party identity before installation.
- Download to a staging directory.
- Verify signature, all hashes, manifest policy, and disk space.
- Install atomically into a versioned directory.
- Never execute files from the staging/download directory.
- Retain the previous working version until the new helper passes a health
  check.
- Support explicit uninstall and rollback.
- Start Gallery/RAW normally when the pack is absent or broken.
- Make all licenses and notices readable from Stack's About/Licenses UI and
  present inside the installed pack.
- Start Gallery/RAW normally after the pack is uninstalled; existing baked
  masks remain usable.

## Security Rules

- Packs are allowlisted data and a pinned helper/runtime, not a general plugin
  system.
- Do not use Hugging Face `trust_remote_code=True`.
- Do not ship a mutable Python environment or run downloaded Python.
- Do not let manifests choose arbitrary DLL names or absolute paths.
- Restrict helper filesystem access and deny network access where practical.
- Apply memory, process-count, and job-object limits.
- Validate tensor shapes before allocation to avoid hostile or corrupt model
  files causing unreasonable memory use.
- Sign update metadata separately from transport TLS.
- Record a software bill of materials for every release.

## Benchmark Gate

Build the first benchmark outside `Stack.exe`. Use the same frozen analysis
proxy and prompts for every model.

Measure:

- cold helper start;
- cold image embedding;
- warm positive/negative prompt response;
- CPU/GPU memory and VRAM;
- mask stability across repeated prompts;
- boundary F-score/IoU where ground truth is practical;
- user clicks and time required to reach an acceptable mask;
- difficult people/background color matches;
- hair, nets, racquets, tree branches, foliage, backlighting, pets, multiple
  people, reflective and translucent objects;
- ONNX numerical difference from the reference implementation;
- DirectML, CPU, and possible WinML provider compatibility.

Do not set a public performance promise until measured on Stack's minimum,
typical, and high-end supported hardware.

## Suggested Delivery Order

1. Legal/SBOM gate for SAM 2.1, EfficientSAM, MobileSAM, and MODNet.
2. Standalone benchmark harness and fixed RAW-derived test corpus.
3. Choose one promptable general model by quality/latency, not popularity.
4. Author and verify a native ONNX pack.
5. Implement the helper protocol and signed package install path.
6. Integrate temporary `AI Refine` UI with positive/negative prompts.
7. Fuse with Stack qualifiers and bake the accepted mask.
8. Add optional portrait matting only if the general path is stable.
9. Re-audit licenses and exact hashes for every released pack version.
