# Benchmark and Validation Plan

## Purpose

Decide whether SAM 2.1 Small is good enough, fast enough, reproducible enough,
and legally clean enough to justify product integration.

The benchmark must compare:

1. Stack's current deterministic qualifier alone;
2. SAM 2.1 Small;
3. SAM 2.1 Tiny if a lower-resource fallback remains under consideration;
4. combined SAM plus Stack qualifier workflows.

## Test Corpus

Build a private, permission-controlled corpus of representative RAW images.
Do not place user photographs in a public repository without explicit
permission.

Required scene categories:

- a person and background with similar color or luminance;
- hair against foliage, sky, or backlight;
- multiple people and overlapping limbs;
- sports equipment, racquets, nets, fences, and thin lines;
- pets and fur;
- foliage, branches, and irregular silhouettes;
- reflective, translucent, and partially occluded objects;
- dark subjects in dark environments;
- bright subjects against bright backgrounds;
- small subjects and edge-of-frame subjects;
- no obvious primary subject;
- images whose white balance or exposure is initially poor.

Create a smaller annotated truth subset for objective boundary comparison and
a larger real-use subset for interaction review.

## Reproducible Inputs

For every fixture record:

- source fingerprint and permitted use;
- RAW decode/processing version;
- crop/orientation;
- WB, demosaic, camera transform, and analysis-view parameters;
- exact model-input pixels or their immutable hash;
- prompt coordinates and order;
- expected comparison masks when available.

All models must receive the same normalized analysis proxy and prompts.

## Quality Measures

- mask IoU on annotated fixtures;
- boundary F-score or equivalent contour accuracy;
- false inclusion/exclusion area;
- thin-structure retention;
- soft-edge quality where a matte is appropriate;
- stability under nearby prompt placement;
- consistency across repeated identical runs;
- number of positive/negative prompts required for an acceptable selection;
- elapsed user time and manual cleanup effort;
- qualitative failure taxonomy instead of only an average score.

Do not conceal catastrophic failures inside aggregate metrics.

## Performance Measures

- package/helper cold start;
- model load and session creation;
- first image encoding;
- warm prompt-decoder response;
- transfer/upload/download time;
- CPU utilization;
- process RAM and peak RAM;
- GPU memory and peak GPU memory;
- cancellation and latest-generation latency;
- embedding-cache reuse and eviction;
- behavior under rapid prompt changes;
- impact on Stack's UI and RAW render-owner frame time.

Measure separately for CPU, the selected broad Windows GPU provider, and any
later vendor-specific provider.

## Hardware Matrix

Exact systems remain TBD. At minimum include:

- minimum supported CPU-only system;
- integrated DirectX 12 GPU;
- representative midrange discrete AMD GPU;
- representative midrange discrete NVIDIA GPU;
- high-end development system;
- low-memory and multi-GPU laptop cases where practical.

Record OS, driver, CPU, RAM, GPU, VRAM, provider, runtime version, model hash,
and power mode with every result.

## Conversion Parity

Compare the frozen reference implementation with the proposed native artifact:

- encoder embeddings or agreed intermediate outputs;
- raw mask logits where accessible;
- thresholded masks;
- candidate scores/order;
- repeated positive/negative refinement.

Set numeric tolerances from measured FP32/FP16/provider behavior and document
why they are acceptable. Do not invent a tolerance before the export exists.

## Functional Validation

- missing pack;
- missing/corrupt model;
- wrong hash or signature;
- incompatible protocol/runtime;
- helper crash during encode and prompt;
- request timeout and cancellation;
- stale result arriving after source change;
- repeated package install/update/rollback/uninstall;
- save/reload with pack present and absent;
- mask accept/cancel/undo/redo;
- Add/Subtract/Intersect parity;
- export with baked masks and no service;
- application shutdown with a busy helper.

## Security and Privacy Validation

- no network access;
- no downloaded Python or arbitrary code execution;
- path traversal and absolute-path rejection;
- malformed dimensions, strides, and sizes;
- resource-exhaustion limits;
- untrusted ONNX/model parsing isolated from `Stack.exe`;
- license/notice visibility;
- SBOM and release-stage artifact reconciliation.

## Acceptance Threshold Process

Phase 02 establishes baselines before numeric release thresholds are locked.
The final decision must answer:

- Is Small materially better than the current qualifier on the difficult
  cases that motivated AI assistance?
- Is its warm prompted interaction acceptable on typical hardware?
- Does Tiny preserve enough quality to justify a fallback?
- Can the native artifact reproduce the reference reliably?
- Can Stack recover cleanly from every missing/broken-service case?
- Is the correction effort low enough that the feature helps rather than
  creating another opaque mask workflow?

Record final thresholds and results in a dated benchmark report; do not
overwrite this plan with one machine's measurements.
