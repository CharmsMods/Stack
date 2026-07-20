# Color Management And Display Pipeline

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: question
- Topic: color-management
- Verification: partially-verified against current Technical Image, source metadata, viewport, and output contracts

## The User's Core Question

Why does non-RAW editing need color metadata at all if a shader can simply do
math on the numbers? Does color space belong on every node, or is it primarily
how Stack prepares data before and after editing?

The user's desired balance is best quality, full flexibility, an easy default
workflow, and professional honesty.

## Current Best Explanation

The shader does not need color metadata to execute arithmetic. `a + b` and
`a * 2` can run on any numeric channels.

Metadata matters when Stack or the user claims that an operation has a
particular physical or perceptual meaning:

- `Multiply by 2` is generic arithmetic.
- `Exposure +1 EV` conventionally means doubling linear-light RGB.
- Saturation and luminance formulas assume a relationship among color
  channels.
- A matrix conversion needs to know the source and destination primaries.
- Export and display need to know how numeric values should be encoded and
  interpreted.

The math itself is not morally right or wrong. The mismatch is between the
operation's promised meaning and the meaning of its inputs. Stack should allow
deliberate experiments while explaining when a familiar editing name is being
used outside its declared domain.

Color state therefore does not need to be “tacked onto” every arithmetic node
as a required setting. It can travel with an Image value, while each node
declares whether it is agnostic, informative, or structurally dependent on
that state.

## Source Preparation Options To Discuss

1. **Manual explicit graph.** Preserve the current direction: source state is
   shown and users add assignment, decode, conversion, and encoding nodes.
2. **Visible automatic source compound.** Import creates a compact,
   inspectable source-preparation compound based on known source metadata. It
   can be opened or bypassed; no hidden conversion occurs.
3. **Import template choice.** Offer “direct numeric,” “standard photo,” and
   later other workflows. The chosen template authors visible nodes.
4. **Suggested repair only.** Stack detects encoded input feeding a
   linear-light operation and offers one-click insertion of explicit nodes.
5. **Implicit working conversion.** Convert automatically without authored
   nodes. This conflicts most strongly with the existing no-hidden-repair
   direction and is not currently favored.

Known metadata and untagged/Unknown input need different behavior. Stack must
not invent a source color identity merely because most consumer images happen
to be sRGB.

## Current View Transform Problem

The current View Transform tone-maps and compresses values into display range,
but it does not perform sRGB transfer encoding or a general monitor/profile
conversion. Its name therefore promises more than it presently does.

Candidate correction direction:

- Rename or redefine the existing operation as a precise **Tone Map** or
  **Display Range Compression** node.
- Provide an explicit **Output Encoding** operation for sRGB and later other
  output transfer functions.
- Offer a friendly, inspectable compound such as **Scene Linear To sRGB
  Output** that contains the exact tone-map and encode stages.
- Treat operating-system/monitor ICC presentation as a separate researched
  boundary. Decide explicitly whether it is part of the authored graph or a
  non-destructive application presentation layer.

This proposal preserves expert access to the stages while giving ordinary users
one understandable finishing node. It is not yet a final contract.

## Questions For The Next Conversation

- What should a newly imported ordinary photo look like in the graph by
  default?
- Should known sRGB input be decoded automatically through a visible compound,
  or only when the user selects a linear-light workflow?
- Which familiar nodes promise linear-light behavior, and which are deliberately
  code-value or perceptual operations?
- Should Stack support several named methods for Brightness, Contrast, and
  Saturation rather than pretending there is one universal formula?
- Is the main viewport an unmodified numeric graph viewer, a color-managed
  application view, or both through clearly separate modes?
- Where does monitor ICC conversion belong if it must not alter exported graph
  pixels?
- What is the smallest professional SDR workflow that does not preclude future
  HDR and wide-gamut output?
- Should source-preparation and output-finishing compounds be created by
  templates, defaults, or one-click diagnostics?

## Research Needed

See `../research-backlog/2026-07-17-color-management-workflow-research.md`.

## Related Docs

- `../../2026-07-15-forward-only-rewrite-decisions.md`
- `../../decision-register.md` — NMR-018, NMR-102, NMR-105, NMR-128, NMR-129
- `../../program-contract.md`
