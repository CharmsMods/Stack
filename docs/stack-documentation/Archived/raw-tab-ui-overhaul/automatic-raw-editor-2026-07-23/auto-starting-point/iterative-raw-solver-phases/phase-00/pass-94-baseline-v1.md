# Pass 94 Baseline v1

## Frozen Identity

| Field | Value |
| --- | --- |
| Date | 2026-07-09 |
| Branch | `main` |
| HEAD | `152ebe4d206f4f436a1d826bf750dc0ae8a7a886` |
| Tracked diff hash | `922c717866779b3122fc09e61c412cc5aff05212` |
| Untracked-list hash | `420ed7205368ce2ab0129c34fab16670fdfa799c` |
| Tracked worktree entries at freeze | 160 |
| Untracked entries at freeze | 15 |
| Baseline behavior label | Pass 94 heuristic solver and stable RAW panel layout |
| Validation record schema | `stack.raw-starting-point.validation-records` v22 |
| Stack executable SHA-256 | `E26E9C80813AFC1F47620A24D29CAECD1FDA00C9621D1BA209C6FD18B0404739` |
| Graph test executable SHA-256 | `99C556F1E03BD308729F100CB36F529B01188210E28E4CD7F407B693A7153181` |

This is a dirty-worktree baseline by design. The hashes identify the exact
reviewed state without discarding or claiming ownership of existing changes.

## Reproduction Commands

From the repository root:

```powershell
.\build.cmd
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-raw-starting-point-editor-state
.\build\Stack.exe --validate-raw-starting-point-records `
  'C:\Users\<user>\Downloads\Tennis' `
  --max-sources 8 --load-raw-safety `
  --max-raw-safety-samples 200000 --tag phase00-baseline
```

Results at freeze:

- build succeeded;
- graph behavior tests passed;
- RAW Starting Point editor-state validation passed;
- eight Tennis records loaded raw safety successfully;
- the record command reported no recipe mutation for every record.

## Current Behavior

Pass 94 is a heuristic closed loop:

```text
measure current stage
-> run fixed decision formulas and gates
-> apply a safe visible subset
-> render again
-> continue for at most four upstream passes
```

It can change RAW Exposure, conditional Suggested WB, at most two Local Range
points, Finish Tone points, and Display Fit when each staged evidence gate is
satisfied. It does not render a continuous neighborhood, backtrack, estimate a
local response model, or stop on an objective tolerance. Its four passes solve
stage dependencies, not numerical convergence.

## Native Interaction Review

The RAW tab opened with:

```text
RAW: all in extract | 356 files | DSC00351.ARW
```

On `DSC00351.ARW`, RAW Exposure was exercised at approximately `+3.47 EV`,
`-3.47 EV`, and `0.00 EV`. Each change was allowed to settle through native
rerenders. Starting Point, status rows, RAW Exposure, and Display Fit remained
vertically stable. The exposure was restored to zero. No project was saved;
the later build closed the test instance.

The review confirms the Pass 94 layout baseline only. It does not claim that
the current solver is strong enough or that its graph coverage is complete.

## Initial Tennis Sensor Records

The eight records below were sampled with at most 200,000 raw mosaic samples.
Clip counts are CFA-plane sample counts in reported R/G/B order; they are not a
channel-aligned all-channel-clipping measure.

| Source key | ISO | Orientation | Bits | Samples | Clip R/G/B | Near R/G/B |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| `IMG_260608_203425.dng` | 43 | 1 | 12 | 198172 | 0/0/0 | 0/0/0 |
| `IMG_260608_203532.dng` | 334 | 1 | 12 | 198172 | 0/67/5 | 0/76/5 |
| `IMG_260608_203701.dng` | 353 | 1 | 12 | 198172 | 0/354/121 | 0/357/125 |
| `IMG_260608_204709.dng` | 377 | 6 | 10 | 199619 | 1/4/0 | 1/4/0 |
| `IMG_260608_204710.dng` | 377 | 6 | 10 | 199619 | 0/6/0 | 0/6/0 |
| `IMG_260608_204712.dng` | 377 | 6 | 10 | 199619 | 200/327/0 | 201/330/0 |
| `IMG_260608_204713 (1).dng` | 377 | 6 | 10 | 199619 | 315/503/0 | 320/508/0 |
| `IMG_260608_204713.dng` | 377 | 6 | 10 | 199619 | 300/468/0 | 301/473/0 |

All eight report an ActiveArea and LinearResponseLimit in the existing
metadata summary, no MaskedAreas, and no validation-command recipe mutation.
The first three are 4080x3060; the last five are 3648x2736.

## Comparison Record Required Later

Every future solver comparison must retain:

- this baseline identity or a newly frozen baseline version;
- source content hash and recipe fingerprint;
- controls before and after, including graph points;
- exact render count and runtime;
- raw, scene, local, tone, and display stage diagnostics;
- convergence/fallback status;
- structured human technical-acceptability labels;
- full-resolution verification result.

A future result beats the baseline only when it improves accepted outcomes
without increasing critical failures. A brighter image alone is not a win.
