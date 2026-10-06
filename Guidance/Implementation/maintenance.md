# Maintaining the guidance

Read when changing these files. Their purpose is to prompt sound decisions with a small reading cost. Preserve that purpose as the program grows.

Keep the common entry limited to widely applicable safeguards, the working approach and topic routing. Put specialized detail in a topic with a clear read condition. Add a topic when a distinct recurring kind of work needs guidance; do not add one for every feature. No task needs to read this folder recursively by default.

Before adding a rule, identify the recurring mistake it would prevent and the evidence for that concern. Say what to consider, why it matters and what a reasonable implementation looks like. Use firm wording for real correctness or data-safety requirements. Use defaults and examples for choices that depend on context. Avoid turning one successful fix into a universal prescription.

Keep each instruction defined in one place. Link to testing policy, build instructions and conversation guidance instead of copying them here. Update or replace outdated advice in place. Remove duplicate or obsolete text rather than appending a newer paragraph that competes with it. Preserve useful reasoning about a tradeoff without retaining a chronology of patches.

Use code references only when they help an agent find the relevant boundary. Verify them when editing the guidance and describe them as current examples. Long subsystem inventories, fixed line numbers, copied APIs and detailed UI descriptions age quickly. The code remains the authority for current behavior. Finding a discrepancy does not authorize changing the product or weakening a safeguard to match an accidental implementation.

Check a proposed rule against several plausible changes. A color adjustment should not trigger an engine redesign. A new processing tool should not end up entirely in a widget. A lifetime fix should investigate both owner validity and stale results. A save-format decision should preserve the user's ability to choose its consequences. If the wording sends any of these tasks in the wrong direction, narrow it.

Review links, duplication, conflicting instructions and reading cost after a change. Keep sources optional and record what they informed. External recommendations support reasoning; they do not override Stack's code, user requirements or platform constraints. No build is needed for a prose-only change unless it alters something consumed by the build.
