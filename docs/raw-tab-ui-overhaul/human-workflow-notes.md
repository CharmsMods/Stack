# Human Workflow Notes

## Purpose

This file restates the RAW tab design in plain workflow language. The more technical documents define ownership, recompute timing, and layout rules; this one is for thinking about how the user actually moves through the edit.

## Canonical Human Model

Use this wording as the clearest expression of the RAW tab model. Other documents can add implementation detail, but should not drift away from this user-facing meaning.

### Auto

This is what Stack already did for you.

Example:

```text
Display Fit was set automatically so the RAW is readable.
```

It must be visible, reversible, and stop changing once you edit it.

### Manual

This is what you are steering.

Example:

```text
You moved RAW Exposure. That is now your value.
```

Manual wins. Auto Base should not fight it.

### Suggested

This is what Stack thinks might help.

Example:

```text
Try opening the shadows.
Try protecting the sky.
```

Nothing changes permanently until you apply it. Hover should preview it when possible, and clicking should apply it.

### Advisory

This is something Stack wants you to know, but cannot safely apply.

Example:

```text
This high ISO image may get noisy if shadows are lifted.
```

It should look like information, not a disabled broken button.

### Live

This follows your hand while you edit.

Example:

```text
Moving RAW Exposure.
Dragging a graph point.
Moving a Local Range target.
```

It should feel responsive, even if it is fast-preview quality.

### Settled

This updates after you pause.

Example:

```text
Full-res preview.
Highlight analysis.
Auto Base suggestions.
Noise/detail score.
```

It should be more accurate and less jumpy.

## Workflow In One Line

The RAW tab workflow should feel like this:

```text
Pick the RAW.
Stack makes it readable.
You set the base light.
You fix parts of the image.
You shape the tone.
You inspect masks/info layers when needed.
You save or move on.
```

## The Editing Story

The RAW tab should help the user do this:

1. Pick a RAW.
2. Get it into a readable state.
3. Set the overall light level.
4. Fix specific parts of the image.
5. Shape the tone/contrast.
6. Inspect masks, warnings, and computed helper views.
7. Save or move on.

The UI should not feel like the user is reading a technical report before they can edit. It should feel like the program is saying:

```text
Here is the image.
Here is what I already made readable.
Here are the safe things I can suggest.
Here are the real controls if you want to steer it yourself.
```

## Expanded Notes For The Same Buckets

The shorter canonical wording above should guide UI copy and implementation prompts. The notes below are supporting detail, not a separate vocabulary.

### Auto

What Stack already did.

Example:

```text
Display fit was set automatically so the RAW is readable.
```

User meaning:

- The program touched a visible value.
- The user can see it.
- The user can undo/revert it.
- The program should stop touching it after the user edits it.

### Manual

What the user is steering.

Example:

```text
You moved RAW Exposure.
This is now your value.
```

User meaning:

- The user is in charge.
- Auto Base should not keep fighting that control.
- The UI can show `Manual`, `Edited`, or a lock icon if useful.

### Suggested

What Stack thinks might help, but has not done.

Example:

```text
Try opening the shadows.
Try protecting the sky.
Try a different white balance.
```

User meaning:

- Nothing has changed yet.
- The user can preview it.
- The user can click to apply it.
- Once applied, it becomes a normal editable control value.

### Advisory

What Stack wants the user to know.

Example:

```text
This high ISO image may get noisy if shadows are lifted.
```

User meaning:

- It is information, not an action.
- It should not look like a disabled broken button.
- It may point the user toward a better edit.

### Live

What follows the user's hand while they drag.

Example:

```text
The preview updates while you move RAW Exposure.
The graph updates while you drag a point.
```

User meaning:

- Fast feedback.
- Not necessarily final/full quality.

### Settled

What updates after the user pauses.

Example:

```text
After you stop moving the slider, Stack refreshes the full preview and recomputes analysis.
```

User meaning:

- More accurate.
- Less jittery.
- Better for recommendations and diagnostics.

## Base Light In Human Terms

The user said their real workflow is usually:

1. Raise RAW Exposure enough to place the image.
2. Avoid clipping important bright areas.
3. Use Local Range for parts of the image.
4. Use Tone Curve / Finish Tone.
5. Mostly leave View Transform alone unless needed.

So the UI should not say:

```text
First, go manually operate View Transform.
```

It should say:

```text
Base Light
Use RAW Exposure to place the image.
Display Fit keeps the preview readable.
```

Possible compact section:

```text
Base Light
RAW Exposure +0.35 EV     Display Fit: Auto
[exposure control]         [refit] [lock] [advanced]
```

Important behavior:

- Display Fit can be set automatically at first.
- It should not constantly rewrite itself while the user moves exposure.
- If the image changes enough, it can say `Needs Refit`.
- The user can click `Refit Display`.
- If the user edits View Transform directly, it becomes manual/locked.

## Suggestions Popout / Expander

The suggestion idea is a preferred interaction pattern, not only a possible extra.

Instead of making suggestions static chips scattered through the side panel, Stack should have a suggestion popout or expander:

```text
Suggestions
Open shadows
Protect sky
Brighten foliage
Try warmer WB
Reduce highlight shoulder
```

Interaction:

- Hover a suggestion: temporarily preview it.
- Move off: preview returns to current edit.
- Click: apply it for real.
- Pin/compare: optionally keep the preview visible while deciding.

This should feel like trying on a change, not committing it.

Preferred placement model:

- The compact RAW top area should show a small suggestions badge/button, such as `3 suggestions`.
- The program header may mirror the suggestion count when useful.
- Opening the badge/button reveals the suggestions popout or expander.
- Owning sections can still show small local markers, pending states, or applied states.
- Do not duplicate long suggestion explanations in every owning section.

Rules:

- Hover preview must not dirty the project.
- Hover preview must not change the recipe permanently.
- Preview state must be visually marked as temporary.
- Click applies and creates normal editable values.
- Esc or mouse leave cancels temporary preview.
- If render is expensive, hover preview can use fast preview first, then settle if pinned.

Good visual language:

```text
Previewing suggestion: Open shadows
Click to apply
```

For Local Range suggestions:

- Hover could show a ghost graph point.
- The center view could show the affected tones/mask.
- Click would create the actual Local Range points.

For WB suggestions:

- Hover could temporarily show the alternate white balance.
- Click would switch WB to the suggested value.

For exposure suggestions:

- Hover could temporarily shift RAW Exposure.
- Highlight warnings should remain visible.

For noise/detail advisories:

- Hover should not apply hidden denoise unless real visible controls exist.
- It may show an explanatory risk overlay instead.

## Center Workspace In Human Terms

The center view should answer:

```text
What am I looking at right now?
```

Possible view choices:

- Final image.
- Before/after.
- What Local Range affects.
- Color target mask.
- Highlight risk.
- Auto Base analysis.

The point is not to create debug clutter. The point is to let the user understand the edit visually instead of reading long text.

## Shorter Names That Feel Good So Far

These names currently feel promising:

- `Base Light`
- `RAW Exposure`
- `Display Fit / View Transform`
- `Refit Display`
- `Target`
- `Stop Target`
- `Overlay`
- `Affected`
- `Delta`
- `Mask`
- `Diagnostics`

Names still open:

- `Color Target` vs `Color Limit` vs `Color Qualification`
- `Tone Graph` vs `Finish Tone`

## Resolved Defaults For First Implementation

These are now captured in `implementation-contract.md`:

1. The suggestions popout/expander opens from the compact RAW top area. The program header may mirror the count.
2. V1 implements click-to-apply suggestions first. Future image-changing hover previews should use a short delay, while graph ghost markers may appear instantly.
3. The center view becomes an explicit mode surface. V1 does not need automatic view switching during suggestion hover.
4. Applied suggestions may remain listed as `Applied` until the source or analysis changes, as long as that state is straightforward and does not conflict with normal undo.
