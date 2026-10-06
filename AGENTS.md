# Stack repository instructions

## Repo Guidance

- For every repository task, read [Guidance/Implementation/README.md](Guidance/Implementation/README.md).
  It establishes the program-wide subsystem architecture direction and required
  topic reading for code changes, including small fixes. Follow its distinction
  between requirements and defaults that need engineering judgment.
- For general work, inspect the relevant code first and follow existing local
  patterns. Prefer `rg` for search.
- For Stack build verification on Windows, prefer `.\build.cmd` from the repo
  root unless a task-specific document says otherwise or it's less efficient.

This is the behavioral file for the repo, it contains some guidelines and things to follow behaviorally and output-wise.

When implementing a new feature, making a bug fix, or changing code, always attempt to stay within the guide of abstaining from monolithic files and putting code together that doesn't match. The goal is to keep this programs repo code organized and easy to locate, so it's easier to update and bugsearch, just like professional code repos.
It is absolutely extremely important that it is never assumed, “This repo is for personal or this PC use only.” This program is meant for a public audience as new versions roll out. The goal is to have it work on as many Windows-specific PCs as possible. In other words don't build it to just work on one PC, For example this one.
Use the code as the primary source for implementation behavior. Keep lasting repository guidance in `Guidance/`. Task-specific findings, decisions, and checklists follow the research-folder rule below; do not scatter working documents across the repository.
during building, use parallel builds like 14, and closing running stack instances is always ok.
Never use "Computer use" to test things.
The git diff will probably always be dirty since i go a long time between commits, so dont worry about that as concern.
For your responses and the way you write documentation, always use `Guidance/Un-slop.md`. You must always read this file at the beginning of a conversation. This file should, however, not affect the quality or integrity of code.

Reference material is in `Guidance/`. The files in `Guidance/AGENT RULES/` are a general native-software preset; consult only the relevant topic file when it helps the task. For recurring RAW startup or render-cache failures, consult `Guidance/AGENT RULES/08-debugging-watchlist.txt`.

Keep `Guidance/` accurate as you work. Current repository code and structure take precedence over factual claims in its documents. When you find a clear mismatch, correct the affected Guidance file (if any). If the right change depends on intent or a difficult judgment, ask the user before changing it if needed in order to preserve the user's tone of the Guidance folder. Keep only the latest, singly defined instructions and commands in Guidance; update them in place instead of retaining obsolete versions or command histories.

At the start of every chat, and throughout investigation and implementation, assess whether a compact research record would help the current work. Make this decision case by case, including when the scope changes. Look for and reuse an applicable folder under root `RESEARCH/`. When a new folder would help, ask the user before creating it unless they have already authorized it. Keep one small task record there with verified findings, decisions, and the remaining checklist. Update that record in place, avoid duplicate instructions or long transcripts, and skip a research folder when it adds no useful context.

Keep testing proportional and bounded. For small, localized fixes, UI adjustments, and minor implementations, do not add or run automated tests by default. Review the affected code and build when needed. For substantial features, architectural changes, or changes with meaningful risk to saved data or image correctness, ask the user how much testing they want before starting extensive validation. Recommend a small set of focused checks and estimate the time involved. Do not independently expand the task into synthetic-data generators, input or failure injection, private-state access hooks, screenshot matrices, repeated benchmarks, or full import-to-export suites. Clarify unusually expensive or open-ended requirements before proceeding. Stop when the agreed checks pass. Repeat or broaden them only for relevant failures or subsequent changes, and report unrelated failures separately. State clearly what was and was not verified.
