# Codex Goal Rules for Stack MFSR

## Purpose
Rules for using Codex goal mode on this feature without letting the implementation drift or balloon.

## Required first action
Before writing feature code, Codex must read these files in this MFSR folder:
- `00_MFSR_README.md`
- `01_MFSR_PRODUCT_UI_SPEC.md`
- `02_MFSR_PIPELINE_SPEC.md`
- `03_MFSR_IMPLEMENTATION_PLAN.md`
- `04_CODEX_GOAL_RULES.md`

Then Codex must inspect the repo and create/update:
- `MFSR_REPO_NOTES.md`
- `MFSR_STATUS.md`

## Status file format
`MFSR_STATUS.md` must stay short. Do not write a diary.

Template:

```md
# MFSR Status

Current phase: Phase N - name
Last verified command: <command or N/A>
Build/test status: pass/fail/not run

Done:
- ...

Next:
- ...

Blocked:
- none / blocker + what input is needed
```

Rules:
- Update status after each meaningful checkpoint.
- Keep each section to five bullets or fewer.
- Do not paste long logs into the status file.
- Reference log file paths instead of copying logs.

## Implementation boundaries
Codex must not:
- Rewrite the entire render engine.
- Replace LibRaw.
- Add AI/ML hallucination upscaling.
- Bake display transforms into the internal node output.
- Mix RAW and raster burst inputs in one MFSR node.
- Implement all phases in one patch.
- Add dozens of exposed UI settings before the automatic path works.

Codex should:
- Prefer small buildable checkpoints.
- Add tests for validation/data contracts before heavy UI/algorithm work.
- Keep CPU reference implementations for correctness even after GPU acceleration.
- Use existing Stack conventions discovered during repo inventory.
- Stop and report if the existing architecture contradicts these docs.

## Suggested goal prompt
Use a prompt shaped like this:

```text
/goal Implement Stack MFSR through Phase <N> of `docs/engineering/mfsr/03_MFSR_IMPLEMENTATION_PLAN.md`, verified by successful build/tests and the acceptance criteria for that phase. First read all docs in `docs/engineering/mfsr`, inspect the actual repo structure, update `MFSR_REPO_NOTES.md` and `MFSR_STATUS.md`, then work in small checkpoints. Preserve existing RAW develop behavior, node graph behavior, and project loading. Do not start later phases until the current phase passes or is clearly blocked. If blocked, stop with the exact blocker, attempted paths, and the smallest next input needed.
```

## Review checklist before marking a phase complete
- Does the project build?
- Were relevant tests added or updated?
- Is the MFSR status file short and current?
- Did Codex avoid unrelated refactors?
- Does the code follow existing Stack naming/style?
- Is the feature still compatible with later RAW CFA-aware fusion?
- Are internal image outputs high-bit-depth/linear where required?
- Are UI display transforms preview/export-only unless explicitly exporting?

