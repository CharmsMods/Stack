# Source Note: Connection-Label Information System

- Intake ID: `idea-20260801-0000-connection-label-information-system`
- Captured: 2026-08-01
- Source kind: pasted user text
- Routing status: implemented presentation slice NMR-144

## Verbatim User Text

PLEASE IMPLEMENT THIS PLAN:
# Connection-Label Information System

## Summary

Replace the current fixed two-line formatter with a universal, source-oriented readout system. Preserve `F` hold-to-reveal, current text geometry settings, and the detail card; do not change active Phase 7B3 code in this design pass.

## Label Rules

| Carried family | Upper line | Healthy lower line |
|---|---|---|
| Image | `Image · R, G, B[, A]` | color identity · reference/transfer · alpha |
| Channel / field | `Channel · Alpha` or `Field · Vector 3` | units · declared range; extent only when numeric context is unavailable |
| Uniform Value | `Value · Scalar`, `Value · Vector 3`, etc. | compact finite payload · units |
| Data | `Histogram`, `LUT`, `Statistics`, etc. | type-specific summary such as bins/domain or measure count |
| Specialized | `Spectrum · Complex`, `RAW`, etc. | domain-specific structural state |
| Failure / unknown | truthful carried-family fallback | concise source failure or unknown state |

- The upper line is always a stable description of what flows from the source.
- The lower line uses one deterministic, type-specific summary—never rotating text, provenance, user-authored text, or destination compatibility.
- A source-side error or actionable warning replaces the normal lower line and is severity-styled. Unknown or not-applicable metadata remains explicit but calm.
- Mask is presented as `Channel · Mask`; a role appears only when it is unambiguous.
- Compact Value payloads use consistent finite-number formatting; matrices and overlong vectors fall back to type/unit wording.

## Future Implementation Contract

- Introduce a non-persisted `WireReadoutInput` resolved per link: source descriptor, optional known uniform output payload, and only diagnostics attributable to that exact source output.
- Format it through a pure typed-template formatter returning primary text, secondary text, severity, accessible text, and detail facts. Presentation must remain excluded from semantic hashes and project serialization.
- Add a read-only graph snapshot path for computed uniform outputs. If no payload is available, never force evaluation; use the semantic fallback instead.
- Do not derive visible alerts from target-only diagnostics. Keep destination compatibility in pin/drop feedback and inspection.
- Keep provenance, extent/sampling/precision, full range details, and non-compact payloads in the delayed detail card.

## Documentation and Validation

- Rewrite the high-level connection-feedback design guide to adopt this system, while retaining completed Phase 5 technical records as historical implementation evidence. Record a superseding design decision for the changed label contract.
- Archive this conversation and update the documentation intake ledger per the repository intake protocol.
- Test every template with known, unknown, and failure states; payload formatting and truncation; warning precedence; source-only diagnostic filtering; `F` visibility; collision/break-line behavior; accessibility; and save/reload proof that no manual labels or per-wire presentation data persist.

## Assumptions

- Existing layout, text-size, sizing, outline, and break-line preferences remain unchanged.
- No custom or user-editable wire labels are added.
- This is a design/documentation slice only until a separate Node Math implementation slice is explicitly activated.

