# AI-Assisted Subject Selection Research

## Status

Research only. No model, runtime, package-manager behavior, recipe field, or
selection UI was implemented in this pass.

## Conclusion

AI-assisted selection is feasible without making Stack open source, provided
the official Stack package accepts only deliberately approved permissive model
packs and fulfills every attribution and redistribution condition.

The preferred direction is:

```text
Stack.exe (proprietary)
    |
    | versioned selection request/result protocol
    v
StackSubjectService.exe (Stack-owned, separately launched)
    |
    | loads only an approved, signed, data-only pack
    v
ONNX Runtime + model weights + license/notices
```

The first benchmark should compare Meta SAM 2.1 Tiny/Small with EfficientSAM
and MobileSAM. SAM 2 has the clearest initial licensing statement because its
official repository explicitly says its checkpoints as well as its code are
Apache-2.0. A final shipping choice still requires an artifact-by-artifact
license, provenance, patent, export/conversion, and dependency review.

AI should propose or refine a mask; it should not replace Stack's existing
tonal/color qualifier. The best product is a hybrid:

- Stack's current mask provides immediate, deterministic feedback.
- A promptable model supplies semantic understanding at difficult boundaries.
- The user can add, subtract, intersect, and refine.
- The accepted mask becomes ordinary Stack-authored mask data, so editing and
  reopening the project do not require rerunning the model.

## Provisional Lead Candidate

SAM 2.1 Small is now the provisional lead candidate for the first benchmark
and integration program. This is not production approval: the exact artifact,
license/provenance, conversion, runtime, quality, latency, and memory gates
remain open.

Candidate-specific preparation is maintained separately in:

```text
../sam-2-1-small-integration/
```

That sibling folder owns implementation status and phase gates. This research
folder remains the authority for comparisons and alternative candidates.

## Read Order

1. `2026-07-24-ai-assisted-subject-selection.md`
2. `model-and-license-matrix.md`
3. `integration-architecture.md`
4. `source-ledger.md`

## Legal Boundary

This packet is engineering research, not legal advice. Before public or
commercial distribution, counsel should review the exact frozen artifacts,
not just the upstream project names or repository badges.
