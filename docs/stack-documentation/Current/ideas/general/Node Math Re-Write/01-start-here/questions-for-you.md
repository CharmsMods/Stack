# Questions For You

This is the plain-language answer sheet for the current channel-based design.
The suggested answers come from the accepted direction and current technical
recommendations. You can accept them, change them, or answer in your own words.

Nothing here activates code work. After you answer, an agent should update
[Work Still To Define](../02-channel-based-design/work-still-to-define.md), the
accepted direction when necessary, and the decision log.

## Product And Contract Questions

### Q1. How should Stack remember a partial Image?

Suggested answer: save exactly which of R, G, B, and A are present. A missing
channel stays different from a connected channel whose value is zero.

**Your answer:** Accepted 2026-07-28. Save the exact R/G/B/A presence set;
absence remains distinct from a present zero Channel.

### Q2. What should Output do with one standalone Channel?

Suggested answer: let Output inspect it as neutral grayscale or as one chosen
display component, save the normal inspection choice, and disable normal
RGB/RGBA export until the user explicitly constructs an Image.

**Your answer:** Accepted 2026-07-28. Output inspects one Channel with a saved
Neutral/Red/Green/Blue mode and blocks ordinary RGB/RGBA export until an Image
is explicitly constructed.

### Q3. When should Image Combine create opaque Alpha?

Suggested answer: when a used Image Combine first has color but no Alpha, add
one visible, editable Constant Channel set to 1.0 in the same undoable action.
If the user deletes it, do not immediately recreate it.

**Your answer:** Accepted 2026-07-28. The first used color-only Image Combine
creates one visible editable Constant Channel at 1.0, with stable Match Extent,
in the same undoable transaction. Deletion suppresses immediate recreation.

### Q4. Which Channel roles should exist first?

Suggested answer: Neutral, Mask, Alpha, Luminance, and EV. Keep purpose,
image-component assignment, units/range, spatial information, and preview
choice as separate facts even if the UI summarizes them together.

**Your answer:**

### Q5. Which controls should accept either one Value or a changing Channel?

Suggested answer: begin with Channel Add Amount, Channel Multiply Amount, and
Exposure EV. Leave changing blur radius and other neighborhood controls for a
later exact design.

**Your answer:**

### Q6. How should a node choose whether math affects RGB, Alpha, or both?

Suggested answer: store the choice on each relevant node. A preference only
sets the default for newly created nodes; it never changes existing nodes.
Friendly image edits begin with RGB and preserve Alpha.

**Your answer:**

### Q7. What should “Dissolve Into Channels” do first?

Suggested answer: support imported Source nodes first. In one undoable action,
insert normal Split and Combine nodes, keep the source, preserve all existing
consumers, and connect Alpha only when the source really contains it.

**Your answer:**

### Q8. How should people inspect or unpack high-level nodes?

Suggested answer: **Inspect** opens a read-only canonical graph, **Make Unique**
creates a local editable definition, and **Dissolve/Unpack** replaces the node
with its exact graph in one undoable action. Warn about very large graphs but
do not forbid them.

**Your answer:**

## Research And Product-Selection Questions

### Q9. Should Stack eventually apply a monitor-profile presentation step?

Suggested answer: research a clearly labeled, bypassable, non-exporting
presentation step. Adopt it only through an explicit revision of the current
direct-viewport decision; do not let it become hidden creative graph math.

**Your answer:**

### Q10. How should a pin say it accepts either a Value or a Channel?

Research result: keep the main words Value, Channel, Image, Data, and
Specialized. Keep Channel Set and Constant Channel.

Suggested answer: use `Amount · Value or Channel` in normal UI and accessible
text. Keep `Value/Channel` only as compact technical shorthand where space is
genuinely limited.

Research: [Naming And Industry Terms](../05-research/channel-system-findings/2026-07-21-naming-and-industry-terms.md)

**Your answer:** Accepted 2026-07-22. Use `Value or Channel` in normal UI and
accessible text; retain `Value/Channel` only as compact technical shorthand.

### Q11. Which established node systems should guide compound behavior?

Suggested answer: compare official interaction and persistence behavior, not
surface appearance, and judge each system against Stack's Inspect, Make
Unique, Unpack, stable-port, and truthful-specialization goals.

**Your answer:**

### Q12. Which nodes should represent the first future library slice?

Suggested answer: use Multiply, Saturation, Gaussian Blur, Reformat, and RAW
Development as research candidates. Candidate comparison may start after Q1–Q6
are stable; implementation should wait until the later dependency work says
the set is genuinely ready.

**Your answer:**
