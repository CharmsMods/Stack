# State and lifetimes

Read when changing state, history, project tabs, asynchronous work, shared services or shutdown. Start by identifying who owns each value, who may change it, and how long its users can outlive the initiating call.

## Distinguish project ownership from shared infrastructure

Each project owns its mutable authored data, history, dirty and save state, in-flight work, progress and accepted results. This can be organized into cohesive objects within that project instance. It does not require adding every field to one enormous session class. Presentation state belongs to the editor or view that presents that project.

Application settings, GPU devices, thread pools and immutable caches can be shared. Shared catalog notifications can also be legitimate application state. The test is whether a value represents one project's work. If it does, shared infrastructure must keep that value scoped to its owner. A new global or function-static variable needs this ownership check; neither every static nor every shared service is inherently wrong.

Use the active tab to route a new foreground action or decide which view to draw. Once work starts, retain the intended owner and required inputs. Completion must not look up the newly active editor and treat it as the recipient.

Selecting another project should not implicitly replace the outgoing document, clear its history or cancel its work. Explicit close, replacement and user-requested cancellation are different operations. If a feature needs a new cross-project restriction, understand and explain the product consequence.

## Check the whole asynchronous lifetime

For an affected job or callback, trace these points:

- What owns the request, its inputs and any resources after submission?
- Which thread changes state, and how does completion return to the proper owner?
- What identity or revision proves that the result is still applicable?
- What happens after an edit, replacement, cancellation, close or shutdown?
- Who releases the work and its retained resources on success, rejection and failure?

Use the existing project task scope, generation checks and publication rules where they fit. Choose the identities relevant to the operation rather than mechanically adding every possible ID. A generation check prevents stale adoption; it does not make a dangling pointer safe. A lifetime lease protects existence; it does not prove that a result still matches the document. Review both.

Cancellation may require cooperative work to drain. Define when the owner is safe to destroy and prevent callbacks from reviving cancelled work. Release external callback leases on all terminal paths. Avoid a cycle in which the owner waits for a callback that only its own destruction can release.

Shared GPU execution may serialize access for correctness or memory limits. That does not justify sharing one project's mutable continuation or busy flag with another. Closing A should wait for A's necessary cleanup, not unrelated work in B, unless a documented shared-resource dependency makes that unavoidable.

## Review isolation concretely

Use an A/B scenario appropriate to the change: start work in A, interact with B, then let A finish or close A. Check the affected data, result, dirty state, history, progress or focus. A hidden animation must not take focus from B; a save completion must not clear edits made after its capture.

These scenarios guide source review and focused validation. They do not require a full application test suite for a local change. Reuse existing checks when useful and report unverified lifetime assumptions.
