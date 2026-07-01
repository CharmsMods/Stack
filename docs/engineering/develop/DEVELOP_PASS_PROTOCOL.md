# Develop Pass Protocol

Last updated: 2026-06-11

Use this protocol for every future Develop node implementation pass. The goal is to preserve source-of-truth continuity across long conversations and separate sessions.

## Continuation Authority

- Treat code as the final truth for current behavior when it can be verified directly.
- Treat `docs/engineering/develop/spec_sources/DEVELOP_NODE_CONTEXT.txt` as the factual current-behavior summary.
- Treat the numbered guide files as desired direction for future behavior, not proof that behavior exists.
- Treat `DEVELOP_IMPLEMENTATION_TRACKER.md` as the pass status and requirement checklist record.
- Treat `DEVELOP_SOURCE_MAP.md` as the fastest route to relevant implementation files and important functions.
- Treat `DEVELOP_DECISIONS.md` as the durable product/architecture decision log.
- Treat `DEVELOP_DEFERRED_SCOPE.md` as the guardrail against reimplementing or overclaiming future-guide work.
- If an incoming prompt conflicts with the current tracker status, verify code and docs, preserve the current truth, and record the mismatch instead of resetting a `Partial` or `Complete` guide to `Not Started`.

## Status Reconciliation

- Before changing any guide status, compare the prompt, guide table, active requirement checklist, decisions, deferred scope, and relevant code.
- Preserve verified `Complete` and `Partial` work even when an older prompt assumes later guides are `Not Started`.
- Distinguish guide-level status in `DEVELOP_IMPLEMENTATION_TRACKER.md` from feature-level substrate status in `DEVELOP_DEFERRED_SCOPE.md`; a partial feature row does not start its owning guide unless the guide row and checklist say so.
- If a status assumption is stale, document that mismatch in the tracker or decisions file before continuing.
- Never use chat history alone as the source of truth for what has shipped.

## Start of Pass Checklist

- Read `AGENTS.md`.
- Read `docs/engineering/develop/spec_sources/DEVELOP_NODE_CONTEXT.txt`.
- Read `docs/engineering/develop/spec_sources/Stack_Develop_Node_Detailed_Guides/00_INDEX_README.txt`.
- Read `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`.
- Read `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`.
- Read `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Read `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Read the active numbered guide file before touching code.
- Check the active guide status in the tracker. If the active guide has no requirement checklist yet, add one before implementing.
- If the prompt's assumed guide status disagrees with the tracker, reconcile that before coding and keep the tracker/code truth authoritative.
- Identify the previous dated handoff note for the active guide, if one exists, and start from that source/function/doc area rather than re-discovering already documented work.
- Check `git status --short` and preserve unrelated user or prior-pass changes.

## During Implementation Checklist

- Treat the guide file as desired direction, `docs/engineering/develop/spec_sources/DEVELOP_NODE_CONTEXT.txt` as current-behavior truth, and code as final truth when conflicts appear.
- Keep edits scoped to the active guide unless the user explicitly expands scope.
- If work touches an intentionally deferred feature, check `DEVELOP_DEFERRED_SCOPE.md` first and update it if the ownership/status changes.
- Do not duplicate existing Guide 01 Auto intent/mode infrastructure. Use `DevelopAutoGuidance::intent`.
- Record mismatches between planning docs and code in the tracker or decisions file instead of silently rewriting history.

## End of Pass Checklist

- Update the active guide row in `DEVELOP_IMPLEMENTATION_TRACKER.md`.
- Update or add the active guide requirement checklist with `Complete`, `Partial`, `Deferred`, `Not Started`, or `Not Applicable` statuses.
- Update `DEVELOP_SOURCE_MAP.md` if source locations, important functions, or behavior changed.
- Update `DEVELOP_DECISIONS.md` for any durable architectural/product decision.
- Update `DEVELOP_DEFERRED_SCOPE.md` if a deferred item became implemented, partially implemented, or newly deferred.
- Run the most relevant available validation and record exact commands/results in the tracker.
- For documentation-only tracking passes, record the acceptance audit in the tracker and decisions file, and explicitly note that no code validation was required.
- Add an append-only dated handoff note in the tracker with status delta, changed locations, validation, remaining work, deferred-scope changes, and the recommended next starting point.
- If the source map or deferred scope did not need changes, say that explicitly in the tracker or final response.
- In the final response, explicitly state what was implemented, what remains incomplete, and what is intentionally deferred.

## Completion Rule

Do not mark a numbered guide `Complete` unless all of these are true:

- The implementation exists in code or the guide item is explicitly marked `Not Applicable`.
- The requirement checklist for that guide is updated.
- The tracker row lists changed code locations and validation.
- Any new or changed source areas are reflected in the source map.
- Durable decisions and non-goals are captured in the decisions/deferred docs.

If any of those are missing, mark the guide `Partial` or `In Progress`.


