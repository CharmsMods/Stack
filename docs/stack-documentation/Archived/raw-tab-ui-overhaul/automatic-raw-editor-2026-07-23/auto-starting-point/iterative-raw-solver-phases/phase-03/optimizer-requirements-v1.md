# Phase 04 Benchmark Requirements From Measured Surfaces

- Input dataset: `objective-surfaces-v1.json`
- Dataset SHA-256: `ABDC9368B86D10E76B78E912ECE70C3854B97ECD38A7CFE2C5B0FDAAED94AD06`
- Decision made here: required benchmark properties only
- Optimizer selected: no
- Objective weights selected: no
- Numeric acceptance thresholds selected: no

## What Was Measured

The frozen command measured Stack's actual OpenGL RAW Development path for one
development DNG and one manifest validation ARW. Each source has all 13 first
dimensions, seven required interactions, and the exact Pass 94 warm candidate
in all 20 surfaces. The locked partition was untouched.

At 256px candidate proxies:

| Source | Unique candidates | Exact memo hits | Complete | Hard rejected |
| --- | ---: | ---: | ---: | ---: |
| `IMG_260608_203425.dng` development | 52 | 51 | 52 | 0 |
| `DSC00650.ARW` validation | 50 | 57 | 5 | 45 |

All five deliberate repeatability stage comparisons had zero mean and maximum
relative delta on both sources. The sampled renderer is deterministic at this
fixed proxy/feature policy on this machine.

## Surface Findings

The DNG surfaces show that stage ownership is observable rather than collapsed:

- the RAW Exposure slice changed Raw Placement p50 by `1.4998 EV` and Finish
  p50 by `1.5035 EV` across the sampled range;
- the Local Range delta slice changed local detail ratio by `0.1120` and local
  adjacent-band evidence by `0.0836` while Raw Placement stayed owned upstream;
- the Finish shadow ordinate slice changed pre-display p50 by `3.0181 EV` and
  Finish structure ratio by `0.1330`;
- the Display white slice changed display high-clip fraction by `0.3976` and
  linear display p50 by `0.6141`; and
- the declared two-dimensional interactions changed multiple owning terms and
  expose direction changes/plateaus individually in the JSON.

These are response measurements, not evidence that larger movement is better.
They show that the objective surfaces carry enough signal to benchmark search
methods without inventing one normal histogram.

The validation ARW exposes a hard feasibility problem: its unchanged Pass 94
warm recipe has `0 EV` RAW Exposure, but the Phase 01 limiting-plane
WB-scaled headroom is below that point. The warm candidate remains present and
fully measured, but Tier 1 correctly rejects it and 45/50 unique candidates.
Phase 04 must not weaken this constraint or allow display terms to compensate.
Its benchmarks must include a feasibility-restoration case or prove that their
initial bounds can enter the safe RAW domain.

## Resolution And Cost Findings

The DNG warm candidate was promoted from `256x192` to the real `4080x3060`
Stack render without applying it. Per-stage proxy/full comparisons retained 35
comparable features for the four scene stages and 10 for Display Candidate.
Mean relative disagreement was `0.1094` to `0.1187`; maximum disagreement was
`0.6597` to `1.0` depending on stage/feature.

This is too large to silently treat a 256px winner as full-resolution truth.
It is not converted into a rejection threshold in Phase 03. Phase 04 must
benchmark a promotion policy and account for feature-specific disagreement.

The complete two-source command took `74.53 s`. Across unique candidates:

| Source | Median render | P95 render | Median features | P95 features |
| --- | ---: | ---: | ---: | ---: |
| DNG | about `131 ms` | about `137 ms` | about `586 ms` | about `603 ms` |
| ARW | about `31 ms` | about `38 ms` | about `525 ms` | about `537 ms` |

Feature extraction, not the 256px render, dominates this V1 measurement path.
Exact reuse avoided 108 redundant surface evaluations across the two sources.

## Required Phase 04 Benchmark Properties

Any method admitted to Phase 04 benchmarking must be tested against these
properties:

1. Lexicographic feasibility: Tier 0/1 failure cannot be traded for Tier 2/3
   improvement.
2. Feasibility restoration: an unsafe warm start must be able to reach or
   honestly fail to reach the safe domain.
3. Conditional and bounded variables: inactive/neutral Local Range dimensions
   and monotone Finish Tone graphs must remain valid.
4. Mixed surface shape: benchmarks must tolerate smooth placement responses,
   plateaus, rejected regions, direction changes, and cross-stage interactions.
5. No analytic-gradient assumption: the measured evaluator is a rendered,
   cached black-box record with failures and missing terms.
6. Exact cache use: duplicate candidate identities must not rerender or
   recompute features.
7. Budget awareness: compare candidate quality and honest failure under the
   measured render/feature cost, not iteration count alone.
8. Warm retention: the Pass 94 candidate remains available; tiny uncertain
   gains cannot silently displace a safe warm candidate.
9. Proxy/full promotion: finalists require same-recipe promotion and explicit
   feature-disagreement handling.
10. Explainability and cancellation: every step must retain candidate,
    constraint, term, identity, cost, and stale/canceled disposition.

These properties define the benchmark. They do not select coordinate search,
trust-region, Bayesian, evolutionary, gradient-free, or any other method.

## Still Not Claimed

- No human technical-acceptability labels were collected in Phase 03.
- No halo visibility, perceptual preference, or proxy/full pass threshold is
  accepted.
- The two-source surface set is for engineering shape/cost evidence, not Phase
  07 product-quality generalization.
- The validation ARW headroom rejection is a required benchmark case, not a
  reason to tune the raw constraint away.

