# Persistence and sources

Read when changing saving, loading, import, export, disk caches, discovery or deletion. Distinguish original inputs, authoritative project data and rebuildable derived data before choosing an operation or destination.

## Keep original inputs separate

Original camera RAW files remain read-only. Import may create project-owned copies and derived assets through the established storage path. A source path or provenance record does not grant permission to overwrite or remove that source. Export must not resolve to an original camera file either.

For destructive operations, establish what the application owns and resolve the actual target. Use the existing path and store helpers where suitable. On Windows, aliases, case differences and path normalization matter. Check affected open projects and pending users before removal; recheck at execution if a queued action can outlive the original decision. Do not infer ownership just from an extension or a folder's display name.

## Save a deliberate model

The saved representation describes enough data to reconstruct the project. It is not a dump of C++ objects, widget fields, pointers, GPU handles or worker state. Decide whether a new value is authored data, a preference, transient state or a cache before adding it to serialization.

Capture a consistent project revision through the established save path. Writers should consume that captured data rather than query whichever editor happens to be active later. Include the assets and metadata required by that revision. Preserve edits and renames made after capture when acknowledging completion.

For Save As, inspect how current live edits, source assets, project identity and destination are captured. Verify that switching the saved baseline to the copy does not overwrite the original or discard a later edit. Do not reuse an old saved snapshot as if it necessarily contains today's graph or timeline.

Use the storage transaction and revision rules already in place. Preserve the last valid project if a write fails, and distinguish commit failure from a failed rebuildable preview or index update. Atomic replacement and power-loss durability are different guarantees; do not claim either without inspecting the relevant platform operations.

## Load and publish deliberately

Decode and validate a candidate before replacing live state. Keep asynchronous load completion attached to the receiving project and operation. Define what happens if another load starts, the project closes or the candidate fails after partial application. Rejection must not silently discard another operation's completion. Recovery should retain the old document's identity, dirty state and storage relationship.

Keep catalog discovery and filtering separate from authoritative document storage. Publish catalog changes after successful writes and invalidate derived listings through the established mechanism. A Gallery refresh must not mutate open project data to make its display agree.

A format change needs an explicit compatibility decision based on the current task. The earlier restructuring's permission to break old project formats is not a permanent waiver for future updates. Avoid compatibility machinery with no supported user need, and ask when the choice would affect existing projects and the request leaves it unresolved.

Design paths, file access and capability handling for the supported Windows audience. Use application configuration and platform helpers rather than this machine's folders, hardware or developer-installed dependencies.
