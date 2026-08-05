# Technical Contracts

These files contain the exact engineering rules that implemented work relies
on. They are intentionally more technical than the plain-language goals and
channel-design summary.

A contract governs only its named scope. Later accepted product direction may
require a new contract without making the earlier implemented behavior false.
Current code and reproducible tests remain the final evidence of what the
application does now.

| Area | Contract | Status |
| --- | --- | --- |
| Program goals, boundaries, and proof rules | [Program Goals And Rules](program-goals-and-rules.md) | Durable program direction; activation wording updated for the current stage |
| Node definitions, values, descriptors, identities, and forward projects | [Node Data And Definition Contract v1](node-and-data/node-data-and-definition-contract-v1.md) | Implemented Phase 1 scope |
| Ordered pointwise math, fusion, barriers, and resources | [Pointwise Math Execution Contract v1](execution-and-performance/pointwise-math-execution-contract-v1.md) | Implemented bounded Phase 4 scope |
| Exact compound definitions, instances, storage, and unpacking | [Compound Node Contract v1](compound-nodes/compound-node-contract-v1.md) | Implemented Phase 5 scope |
| Compound output and connection presentation | [Compound Output And Connection UI Contract](graph-connections/compound-output-and-connection-ui-contract.md) | Implemented Phase 5B/5B-C scope; future channel vocabulary is separate |
| Image regions, ROI, halo, tiling, and cancellation | [Image Region And Tiling Contract v1](regions-reductions-and-specialized-processing/image-region-and-tiling-contract-v1.md) | Implemented Phase 6A scope |
| Field Mean whole-image reduction | [Field Mean Reduction Contract v1](regions-reductions-and-specialized-processing/field-mean-reduction-contract-v1.md) | Implemented Phase 6B scope |
| Reformat and specialized processing boundaries | [Reformat And Specialized Processing Contract v1](regions-reductions-and-specialized-processing/reformat-and-specialized-processing-contract-v1.md) | Implemented Phase 6C scope |
| Channel-first frequency nodes, resources, and analysis | [Channel-First Frequency Contract v1](regions-reductions-and-specialized-processing/channel-first-frequency-contract-v1.md) | Implemented Phase 7A scope |
| Partial Images, Output Channel inspection, and visible opaque Alpha | [Partial Image, Output Inspection, And Constant Alpha Contract v1](channel-system/partial-image-output-and-constant-alpha-contract-v1.md) | NMR-142; Phase 7B1/7B2 complete, Phase 7B3 active |

Future C1–C8 channel-system contracts will be added by subject after their
product choices are approved. C1-C3 are now owned by the accepted
[Partial Image, Output Inspection, And Constant Alpha Contract v1](channel-system/partial-image-output-and-constant-alpha-contract-v1.md),
with Phase 7B1 and Phase 7B2 complete and Phase 7B3 active under NMR-142.
Future C4-C8 contracts remain queued.
An exact contract governs implementation detail;
any product-level difference must update the accepted product direction and
decision log in the same pass.
