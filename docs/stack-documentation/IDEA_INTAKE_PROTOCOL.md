# Idea Intake Protocol

Use this protocol for ongoing note intake inside
`docs/stack-documentation/`. This workflow is separate from the one-time
documentation reorganization tracker in `docs/documentation-reorg-tracker/`.

Treat this file as the entry file for conversations about sorting user note
dumps, idea captures, documentation updates, research notes, or fixes into the
docs home.

## Purpose

This protocol exists so a user can hand an agent a thinking-session dump and
the agent can:

- preserve the original source text
- split the session into separate routed notes by topic and note type
- place each routed note in the real documentation tree when a clear
  workstream home exists
- avoid silent merges into existing docs
- keep a simple ledger of what happened

## When To Use This

Use this protocol when the user gives:

- direct pasted notes from a thinking session
- a note dump that mixes ideas, updates, fixes, questions, and research
- a document or file that should be routed into the documentation home

Do not use this protocol for:

- the completed first-pass documentation reorganization
- normal code implementation tracking already owned by a path-coupled bundle
- product website assets
- repo instruction files such as `AGENTS.md` or `AGENT RULES/`

## Cold-Start Checklist

Before sorting a new note dump:

1. Read `docs/stack-documentation/README.md`.
2. Read this protocol.
3. Read `docs/stack-documentation/IDEA_INTAKE_LEDGER.tsv`.
4. Inspect the likely destination folders under `Current/` or `Info/`.
5. If the new note appears to overlap an existing doc, read that doc before
   deciding whether user input is needed.
6. Do not use `docs/documentation-reorg-tracker/` for ongoing note intake.

## Folder Map

Choose destinations in this order.

### Primary Destinations

Prefer the real documentation tree first:

- the closest matching existing workstream or topic folder under
  `docs/stack-documentation/Current/`
- the closest matching existing workstream or topic folder under
  `docs/stack-documentation/Info/`
- a new sibling note inside an existing workstream folder when the workstream
  is clear but no exact file exists yet

Examples:

- `docs/stack-documentation/Current/raw-tab-ui-overhaul/manual-first-raw-workflow/`
- `docs/stack-documentation/Current/engineering/develop/`
- `docs/stack-documentation/Current/engineering/mfsr/`
- `docs/stack-documentation/Info/engineering/`

### Fallback Destinations

Use these only when no clear real workstream home exists yet:

- `docs/stack-documentation/Current/ideas/<topic>/`
- `docs/stack-documentation/Current/updates/<topic>/`
- `docs/stack-documentation/Current/fixes/<topic>/`
- `docs/stack-documentation/Info/research/<topic>/`
- `docs/stack-documentation/Info/questions/<topic>/`
- `docs/stack-documentation/Archived/source-notes/YYYY/`

Rules:

- Prefer the real workstream tree over the fallback type buckets.
- Use note type to shape the note and its review flow, but not as the first
  routing rule when a clear workstream home already exists.
- Use lowercase kebab-case for topic folder names.
- If no clear topic exists, use `general/`.
- Reuse an existing workstream or topic folder when the fit is obvious.
- Create a new sibling note in that workstream when the workstream is clear but
  no matching file exists yet.
- Use the fallback type buckets only when the note is genuinely cross-cutting,
  early-stage, or not yet tied to one established documentation area.

## Classification Rubric

Use these note types:

- `idea`: future implementation thought, design direction, or feature concept
  that is not yet an explicit fix or doc update
- `update`: a requested change to existing docs, behavior notes, guidance, or
  current implementation direction
- `fix`: a correction to something wrong, misleading, stale, or broken in the
  docs or the program
- `research`: a topic that needs deeper investigation, references, math, or
  technical exploration
- `question`: an unresolved user question, design question, or comparison that
  should be preserved as reference

Type controls how the note should be framed, verified, and merged. It does not
automatically decide the folder if the note clearly belongs inside an existing
documentation workstream.

If one source session contains multiple distinct items, split them into
multiple routed notes. Do not keep one combined session file as the working
destination.

If one paragraph mixes several purposes:

- split it again if the parts are clearly separable
- otherwise classify by the dominant purpose and preserve the secondary point in
  the body or optional sections

## Intake Workflow

Process each new intake in this order:

1. Assign a session id using
   `idea-YYYYMMDD-HHMM-<short-session-title>`.
2. Determine the source kind:
   `pasted-text`, `manual-source-file`, `attached-document`, or
   `repo-file-source`.
3. Archive the original source under `Archived/source-notes/`.
4. Add or update the session row in `IDEA_INTAKE_LEDGER.tsv`.
5. Split the source into routed note candidates by topic and type.
6. Decide whether each candidate has a clear home in the real documentation
   tree.
7. Check for obvious overlaps with existing docs in that likely destination
   area.
8. If an obvious existing home exists, ask the user before merging.
9. If the workstream is clear but no exact file exists, create a new sibling
   note in that workstream.
10. Use the fallback type buckets only when no clear real workstream home
    exists yet.
11. Update the ledger status through completion.

## Source Archive Rules

### Direct Pasted Text

If the user provides direct pasted text and it is about `2500` words or less:

- create a source note automatically under
  `docs/stack-documentation/Archived/source-notes/YYYY/`
- use the filename format
  `YYYY-MM-DD-HHMM-<short-session-title>.md`
- preserve the original text verbatim apart from normal line-ending cleanup

Use this source-note structure:

```md
# Source Note: <short session title>

- Session ID: idea-YYYYMMDD-HHMM-<short-session-title>
- Received: YYYY-MM-DD HH:MM
- Source Kind: pasted-text

## Original Text

<verbatim user text>
```

### Large Pasted Text Or Attached Documents

If the user provides:

- a pasted note longer than about `2500` words, or
- an attached document that is not already inside the repo

then do not try to recreate the original automatically.

Instead:

1. ask the user to place the original file under
   `docs/stack-documentation/Archived/source-notes/YYYY/`
2. wait for the exact path or confirmation that the file is there
3. record that path in the ledger
4. continue routing from that source file

## Existing-Doc Merge Rules

Before updating an existing routed note or another existing documentation file:

- inspect the most likely destination folder first
- inspect nearby docs in the same workstream if the match looks obvious
- do not silently merge into an existing doc
- prefer a new sibling note in the same workstream over routing into a fallback
  type bucket when the workstream is already clear

If a clear existing home exists, return to the user conversationally with:

- the existing doc path
- why it looks like the best match
- these choices:
  `merge into the existing doc`, `create a sibling doc`, or `keep it separate`

If no clear existing home exists, create a new routed note without asking.

If the workstream is clear but no existing note is the right merge target,
create a new sibling note in that workstream.

## Routed Note Rules

Use this filename format for routed notes:

- `YYYY-MM-DD-<short-title>.md`

If a same-day filename already exists in the same folder, append `-02`, `-03`,
and so on.

Choose the routed note destination in this order:

1. existing matching doc after explicit user merge approval
2. new sibling note inside a clear established workstream folder
3. fallback type bucket only when no clear workstream home exists yet

Use this note template:

```md
# <Title>

- Captured: YYYY-MM-DD HH:MM
- Source: docs/stack-documentation/Archived/source-notes/YYYY/YYYY-MM-DD-HHMM-<short-session-title>.md
- Type: idea | update | fix | research | question
- Topic: <topic-folder-name>
- Verification: unverified | partially-verified | verified

<lightly edited note body>

## Research Needed

<only if needed>

## Follow-Up

<only if needed>

## Related Docs

<only if needed>
```

Editing rules for routed notes:

- preserve the original meaning and rough structure
- fix grammar, punctuation, and obvious wording issues
- correct clear factual mistakes only when the agent actually verified them
- do not turn the note into a heavy rewrite
- do not discard uncertainty; preserve it plainly when it matters

## Verification Rules

Default verification is opportunistic.

Use these values:

- `unverified`: no code/doc inspection was needed yet
- `partially-verified`: some concrete claims were checked, but not all
- `verified`: the note's concrete claims that materially affect placement or
  interpretation were checked and matched current code/docs

Verify only when:

- a concrete claim affects where the note belongs
- a claim changes whether the note is current, stale, or fix-oriented
- the user explicitly asks for verification

Do not over-verify:

- speculative ideas
- open questions
- broad research prompts
- secondary details that do not affect routing

## Ledger Rules

Track every intake session in
`docs/stack-documentation/IDEA_INTAKE_LEDGER.tsv`.

Columns:

- `session_id`: `idea-YYYYMMDD-HHMM-<short-session-title>`
- `received_on`: `YYYY-MM-DD HH:MM`
- `source_path`: archived source file path or manually placed source file path
- `source_kind`: `pasted-text`, `manual-source-file`,
  `attached-document`, or `repo-file-source`
- `status`: `received`, `source-archived`, `split`,
  `needs-user-decision`, `routed`, `complete`, or `blocked`
- `topics_created`: semicolon-separated `type/topic` entries such as
  `idea/raw-workspace; research/local-range`
- `needs_user_decision`: `yes` or `no`
- `notes`: short structured notes such as
  `routed_paths=...; verification=partial; merge_candidate=...`

Update the row as the session progresses. Do not create a second row for the
same session.

## End State

An intake session is complete only when:

- the original source is safely archived or manually placed and recorded
- the source has been split into routed notes where needed
- any merge decision was resolved with the user
- routed files exist in their final destinations
- the ledger row is marked `complete`

If work stops early, keep the ledger accurate and leave the status at the
furthest honest state.
