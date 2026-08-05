# Node Math Rewrite

This folder is the planning home for making Stack's node graph more reliable,
more channel-based, and much more flexible without hiding what the math is
doing.

Phase 7B3 Constant Alpha transaction code work is active under NMR-142.
Implementation Phases 0 through 6, the bounded Phase 7A frequency family, and
Phase 7B1 partial Images plus Phase 7B2 Output inspection are complete. The
broader channel-based product direction is accepted; C1-C3 have one exact
vertical contract, while most later behavior is not implemented yet.

## Start With What You Need

| If you want to... | Start here |
| --- | --- |
| Understand the goal in ordinary language | [What We Are Building](01-start-here/what-we-are-building.md) |
| See where the work stands now | [Current Status](01-start-here/current-status.md) |
| Answer or revise the remaining choices | [Questions For You](01-start-here/questions-for-you.md) |
| Work on the channel-based design | [Channel-Based Design](02-channel-based-design/README.md) |
| Read the exact engineering rules | [Technical Contracts](03-technical-contracts/README.md) |
| Plan or verify code work | [Implementation](04-implementation/README.md) |
| Read formulas, science, standards, and architecture research | [Research Library](05-research/README.md) |
| See what earlier implementation phases proved | [Completed Work](06-completed-work/README.md) |
| Trace old prompts, audits, or moved files | [History](07-history/README.md) |

You can work on any one of these areas without reading the entire folder. A
plain-language conversation, a documentation edit, or a research pass does
not activate implementation.

## How The Two Documentation Layers Work

The plain-language files are the conversation layer. They describe what you
want, what feels right, and what still needs an answer. You may edit them in
ordinary language.

The technical files are the implementation layer. They translate accepted
direction into exact data rules, formulas, failure behavior, persistence,
tests, and version consequences. An agent should keep the two layers aligned
instead of expecting the plain-language files to contain engineering jargon.

## What Has Authority

Use the smallest relevant source:

1. Current code and reproducible tests establish what Stack does now.
2. [Accepted Channel-Based Product Direction](02-channel-based-design/accepted-product-direction.md)
   and the [Decision Log](01-start-here/decision-log.md) establish accepted
   product direction.
3. [Technical Contracts](03-technical-contracts/README.md) establish exact
   behavior for implemented areas.
4. [Detailed Progress Log](04-implementation/detailed-progress-log.md) owns
   implementation activation and the detailed work ledger.
5. Research is supporting evidence and design rationale, not automatic product
   policy or an implementation backlog.
6. Completed records, dated audits, conversations, and prompts are history.
   Their old commands and next-step statements do not control current work.

If a future exact contract needs to change the accepted product experience,
update the accepted direction and decision log in the same pass. Do not let an
implementation detail silently redefine the product.

## Implementation Boundary

Code work starts only when the detailed progress log explicitly names an
active slice. That rule applies to source changes, not to discussing an idea,
organizing documentation, answering questions, or doing research.

The complete 2026-07-21 old-to-new path record is in the
[File Move Map](07-history/file-move-map-2026-07-21.md).
