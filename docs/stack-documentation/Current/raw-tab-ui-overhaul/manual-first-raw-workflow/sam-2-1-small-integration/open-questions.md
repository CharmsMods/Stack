# SAM 2.1 Small Open Questions

Do not resolve these by convenience during implementation. Record evidence and
update the decision register.

## Artifact and Legal

- Which exact upstream commit/tag and checkpoint URL will be frozen?
- What notices accompany the frozen repository and checkpoint?
- Does the final legal review accept the official Apache-2.0 checkpoint grant
  and documented training provenance for commercial distribution?
- Which conversion code is used, and is it entirely covered by approved
  licenses?
- Will Stack distribute converted weights or produce them during package
  publication?

## Model and Runtime

- Can Small be exported with encoder and prompt decoder separated cleanly?
- Which ONNX opset and tensor shapes produce the best provider compatibility?
- Does DirectML support the frozen graph without CPU fallbacks or incorrect
  results?
- Is WinML preferable for the first supported Windows path?
- Is CPU performance useful as a fallback or only for diagnostics?
- Is FP16 accurate and broadly supported enough, or is FP32 required?
- Does Tiny provide a meaningful low-resource tier?

## Analysis Proxy

- What fixed resolution and aspect-ratio padding should be used?
- What deterministic neutral view best matches the model's expected ordinary
  RGB photographs without depending on mutable creative edits?
- Which source changes invalidate the embedding?
- How are crop, rotation, lens correction, and future geometry changes mapped
  to prompts and stored masks?

## Mask Persistence

- Is the accepted mask stored full-resolution, tiled/compressed, or
  deterministically refined from a proxy?
- Does a semantic mask become a general reusable mask object or remain inside
  Local Exposure initially?
- How are Add/Subtract/Intersect operations serialized?
- Are prompts retained for optional recompute, and how is recompute prevented
  from silently changing a baked mask?
- How is pack/model provenance displayed without making the saved project
  depend on that model?

## Interaction

- Is AI refinement a modifier gesture in Target mode or an explicit action?
- How does the cursor distinguish deterministic hover from neural prompting?
- When should the user see candidate masks if SAM returns several?
- What outline/refinement language remains readable without covering the
  photograph?
- How are positive and negative prompts edited or removed?
- What is the exact accept/cancel/undo transaction?

## Package Manager

- Where are optional packages installed per user or per machine?
- What signing key and rotation/revocation scheme will Stack use?
- How are manifests fetched securely and pinned to compatible Stack versions?
- Can runtime/provider files be shared safely between packs?
- How are interrupted installs, rollback, uninstall, and disk cleanup handled?
- How are licenses and notices presented before and after installation?

## Release Scope

- Is the first release experimental and explicitly opt-in?
- What minimum hardware/provider combinations are supported?
- What quality and latency thresholds must be met?
- Which failures require holding the feature rather than falling back?
