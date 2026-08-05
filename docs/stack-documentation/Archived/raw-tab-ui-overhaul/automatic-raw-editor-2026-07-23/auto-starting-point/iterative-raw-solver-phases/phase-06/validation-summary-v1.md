# Phase 06 Validation Summary

- Record: `phase-06-engineering-subset-v1`
- Report schema: `raw-precise-integration-report-v1`
- Integration/runtime: `raw-precise-integration-v1` / `raw-precise-native-runtime-v1`
- Solver: frozen `raw-precise-dry-run-v1`
- Sources: four
- Status: complete
- Human review claimed: no

## Renderer-Backed Results

| Source | Partition | Solve result | Visible groups changed | Full output |
| --- | --- | --- | --- | --- |
| `IMG_260608_203425.dng` | development | warm start retained | RAW Exposure, Local Range, Display Fit | `4080x3060` |
| `IMG_260608_203701.dng` | development | safe improvement; budget exhausted | all four groups | `4080x3060` |
| `IMG_260608_204713.dng` | development | safe improvement; budget exhausted | all four groups | `2736x3648` |
| `DSC00650.ARW` | validation | safe improvement; budget exhausted | RAW Exposure, Finish Tone | `4024x6024` |

Every source used 45 actual proxy renders, four exact cache hits, all five
required stages, and one accepted true-resolution finalist.

## Per-Source Gates

All four records passed:

- search did not mutate live state;
- atomic apply was authorized only after full verification;
- selected recipe exactly equaled the applied recipe;
- at most one apply occurred;
- one Undo restored the original recipe exactly;
- persistence round-trip was exact and did not rerun the solver;
- failed apply was atomic;
- source switch, same-key source identity change, and recipe edit rejected stale
  candidates;
- cancellation was safe;
- fields outside solver ownership were preserved;
- progress was reported;
- Fast mode remained available; and
- no automatic rerun occurred.

## Report Integrity

The report contains no source paths and makes no human-review claim. It records
changed and unchanged visible groups, selected 13-field parameters, solve and
full-verification disposition, render/cache counts, and every integration gate.

SHA-256:

```text
FED2E76C26C178904DAAABB5A42A8D6768EFA6D8C806309F7B746ED6490B4D19
```

## Interpretation Boundary

This record proves native renderer, identity, projection, apply, persistence,
cancellation, failure, and Undo mechanics on the versioned engineering subset.
It does not prove that the edits are preferred, strong enough, natural enough,
or artifact-free across the intended product population. Those judgments are
reserved for the locked Phase 07 corpus and structured native human review.
