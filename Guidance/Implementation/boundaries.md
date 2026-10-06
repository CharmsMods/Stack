# Subsystem boundaries and files

Read before every code change, including small fixes. The useful unit of organization is a responsibility with a clear owner and contract. A directory or class name is evidence to inspect, not proof that the code belongs there.

## Program-wide architecture direction

Build Stack as one application made of cooperating subsystems or frameworks. The operating-system parallel is that each subsystem performs its own work behind a supported interface. Other parts of Stack ask for that work through defined calls without needing to understand its internal implementation. This direction applies to the whole program.

Each subsystem must have a clear responsibility and own its internal rules and implementation state. Callers must use its supported functions and types rather than reach into private state, reproduce its internal rules or depend on its implementation layout. Shared infrastructure must still preserve the project and operation ownership defined in [State and lifetimes](state-and-lifetimes.md).

The application connects subsystems and coordinates workflows through those interfaces. Keep subsystem decisions with their owner as Stack grows. A coordinating component needs a specific workflow responsibility; it should not become the home for every system's internal logic.

This is the intended architecture to preserve and work toward. Inspect current code to establish which boundaries already exist and where the affected path needs improvement.

## Let subsystems evolve independently

A subsystem must be able to gain capabilities or change its internal implementation without requiring unrelated systems to be rewritten. Preserve the supported behavior of existing calls when making a compatible upgrade. Add new capabilities through the owning subsystem's interface so only callers that use them need to change.

Before changing an interface, identify its consumers and the behavior they rely on. Compatibility includes inputs, results, failures, ownership, lifetime and any thread or cancellation rules, not only function signatures. An intentional change to those expectations needs a deliberate migration of affected callers. Keep that migration within the task's authorized scope.

For each code task, identify the subsystem that owns the change, the supported calls used by its consumers and any internal detail the proposal would expose. A short working conclusion is enough for a small fix. Reuse or extend an appropriate interface; create a cohesive owner when a genuinely new responsibility needs one.

## Choose a home by responsibility

Look for the subsystem that already owns the behavior and the decisions likely to change with it. Keep its private helpers nearby. Put reusable file discovery or project classification below the view that displays it. A view's local display order or text formatting can remain in the view.

Separate concerns along the dimensions that matter to the change: product responsibility, state ownership, execution thread, data format and implementation language. These dimensions need not produce separate directory trees. A feature's C++ code and shader assets can live together while remaining distinct sources with an explicit binding contract.

Prefer the repository's established source and asset pipeline. Where substantial shader, script or markup code is embedded in unrelated application code, consider a dedicated source or owner-specific asset file. Small inline fragments, templates and deliberately embedded shaders may be appropriate. Extraction must preserve compilation, resource lookup and packaging; external files are not automatically an improvement.

## Recognize a useful split

A file deserves attention when unrelated reasons to change accumulate, callers need to understand private details, or a local fix repeatedly reaches across many sections. Line count is a signal, not a threshold. A cohesive implementation may be long. Several tiny files can also obscure one operation.

Splitting methods into files while leaving every method dependent on a large object's private state does not resolve an ownership problem. When that is the actual issue, extract the decision or operation with the inputs it needs. Conversely, a small local helper does not need a new public class or interface.

New `Utils`, `Common` or `Manager` code needs a specific responsibility. Similar-looking code is not necessarily the same rule. Share a helper when callers need the same semantics and future fixes should apply to all of them. Avoid speculative frameworks and wrappers that merely forward every call.

## Make collaboration between modules legible

Ordinary C++ functions and types can form a public interface within Stack. Use direct calls when they express the relationship well. Choose interface classes, callbacks, adapters or versioning when a concrete dependency or compatibility need justifies them. Use names or types to distinguish units, image representations and identities that callers could confuse.

Check dependency direction in includes and call sites. An engine should not import an editor to read its current selection. A neutral model type may currently live in an editor-named folder; inspect what it depends on before moving it. If a new cycle appears, reconsider which component owns the shared contract before adding callbacks or an event bus to conceal the cycle.

For a move or extraction, check callers, declarations, test targets, generated resources and packaging inputs. App-wide source discovery does not guarantee that focused targets include a moved file. Use includes and namespaces that make the relationship explicit.

Keep cleanup connected to the task. A small fix in a large file may be the right change. A new subsystem may need several files. Respecting the subsystem direction does not require redesigning unrelated code during a local fix. Explain when necessary restructuring grows beyond the original local change rather than quietly expanding it.
