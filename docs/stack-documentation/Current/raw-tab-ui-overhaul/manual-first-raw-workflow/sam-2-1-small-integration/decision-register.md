# SAM 2.1 Small Decision Register

Statuses:

- `accepted` - current program rule;
- `provisional` - working direction that still requires evidence;
- `open` - implementation must not guess;
- `rejected` - intentionally excluded.

| ID | Status | Decision | Reason or gate |
|---|---|---|---|
| SAM-001 | provisional | Use SAM 2.1 Small as the lead general promptable-selection candidate | Strong capability/license fit; performance and conversion remain unproven |
| SAM-002 | accepted | Keep the broad AI model research and SAM implementation records in sibling folders | Preserves alternatives and licensing evidence independently of implementation |
| SAM-003 | accepted | Ship neural assets only as an optional package | Keeps the base installer smaller and core editing independent of the model |
| SAM-004 | accepted | Run inference outside `Stack.exe` through a Stack-owned helper | Isolates crashes, memory, GPU state, updates, and untrusted model parsing |
| SAM-005 | accepted | Allow only signed, hash-verified, approved packages | A manifest claim alone is not a security or licensing gate |
| SAM-006 | accepted | Do not ship Python or execute downloaded code | Packs must not become a remote-code/plugin mechanism |
| SAM-007 | accepted | Bake an accepted mask into Stack-owned project data | Existing projects must remain usable without inference or the original pack |
| SAM-008 | accepted | Keep the existing deterministic qualifier available and composable | Semantic selection complements rather than replaces photographic precision |
| SAM-009 | provisional | Feed a versioned neutral analysis proxy after demosaic/WB/camera color conversion and before the authored Local Exposure edit | Matches normal-photo model expectations and prevents recursive mask changes |
| SAM-010 | open | Use one ONNX graph or split encoder/decoder graphs | Decide from conversion parity, cache design, provider compatibility, and latency |
| SAM-011 | open | Use ONNX Runtime DirectML, WinML, CPU, or a provider order | Requires operator and hardware benchmarking |
| SAM-012 | open | Offer SAM 2.1 Tiny as a low-memory fallback | Decide only after Small/Tiny quality and hardware measurements |
| SAM-013 | open | Store a full-resolution mask, tiled mask, proxy mask plus deterministic refinement, or a combination | Must satisfy reload/export fidelity and project-size limits |
| SAM-014 | rejected | Invoke neural inference continuously during ordinary hover | Too expensive and visually unstable; explicit prompting is clearer |
| SAM-015 | rejected | Add segmentation models as another `NeuralDenoiseManager` model type | Denoising and interactive selection have different lifecycles and security contracts |

## Change Rule

Every decision change must record:

- the evidence that changed;
- affected phase gates and documents;
- compatibility or migration impact;
- whether legal review must be repeated.
