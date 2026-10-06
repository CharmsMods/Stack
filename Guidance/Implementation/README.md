# Implementation guidance

Read this entry for every repository task. Before any code change, including small fixes, also read [Subsystem boundaries and files](boundaries.md). It defines Stack's architecture direction of independently upgradable subsystems that cooperate through supported interfaces. For reviews, plans and documentation, use that direction when assessing or describing a design.

Then read only the other topic files that match the change. Expand that selection if inspection reveals another affected responsibility. This folder guides engineering decisions; it is not a complete description of Stack.

## Establish what is true

The user's request defines the intended outcome. Current code, callers, build configuration and observed behavior establish how Stack works today. Documentation and earlier chat summaries are leads to verify. Existing behavior can contain bugs, and a dependency can be misplaced. Neither is automatically a pattern to copy.

Trace the affected entry point far enough to identify its data owner, operation and consumers. For asynchronous work, include completion and cleanup. Check whether a comparable implementation's assumptions fit this case. A spacing fix usually needs the widget and shared styling; a processing control also needs its project action and engine contract. Do not read the whole repository by default.

Before editing, identify where the change belongs, what crosses that boundary and what behavior must remain intact. A brief working conclusion is enough for a small change. Write a compact plan when several systems or a migration are involved.

## Requirements and judgment

Preserve these safeguards unless the user explicitly changes the relevant requirement:

- A project tab represents an independent project instance. Its mutable document, edits, history, jobs and results must stay associated with that instance.
- Original camera RAW files are read-only inputs. Saving, processing, cleanup and project deletion must not overwrite, move, rename or delete them.
- Processing operations must be callable with explicit project inputs without requiring an editor window or active-tab lookup. Required GPU or platform services may still be supplied.

The topic files distinguish requirements from defaults and questions to consider. The subsystem boundaries are part of the established architecture direction. Implementation choices impose no fixed file sizes, mandatory design pattern or rewrite of nearby imperfections. Choose the simplest arrangement that preserves the contracts. Explain meaningful departures from defaults briefly where a maintainer can find the reason.

Resolve ordinary implementation choices from code and evidence. Ask when an unresolved choice changes product behavior, risks existing data, substantially expands scope or changes a safeguard. Explain the consequence and continue independent work while waiting when possible. Routine refactoring needs no extra approval under this guide; consequential product decisions still need the user's direction when the request leaves them open.

## Read by change

| Change touches | Read |
| --- | --- |
| Every code change, including small fixes, new files, APIs, dependencies and build integration | [Subsystem boundaries and files](boundaries.md) |
| Controls, views, rendering, image processing, shaders or performance | [UI and processing](ui-and-processing.md) |
| Project state, history, tabs, jobs, callbacks, shared services or cancellation | [State and lifetimes](state-and-lifetimes.md) |
| Saving, loading, import, export, caches on disk, discovery or deletion | [Persistence and sources](persistence.md) |
| Editing or extending this guidance | [Maintaining the guidance](maintenance.md) |

[Research notes](sources.md) are optional background. Following a topic does not require opening every document or external link it references.

## Finish proportionally

Review the changed call path, ownership and error handling. Verify affected behavior, including another project or delayed completion when relevant. Check that reused tests still describe supported behavior. Follow the testing policy in [AGENTS.md](../../AGENTS.md) and build instructions in [BUILDING.md](../BUILDING.md). Report what was verified and any material gap. A successful build alone does not establish correct behavior.

Keep any factual guidance you touch accurate. Update an existing statement rather than appending a conflicting rule. Do not create permanent guidance for every bug or implementation detail.
