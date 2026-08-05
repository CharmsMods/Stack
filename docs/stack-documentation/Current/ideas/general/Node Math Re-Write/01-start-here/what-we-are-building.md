# What We Are Building

Stack should let people work with images as understandable combinations of
values, channels, images, and larger tools. It should remain friendly for
normal editing while allowing a technical user to reach the underlying pieces
when that representation is honest.

## The Main Goals

1. **A reliable graph.** Every node should have one exact meaning that the UI,
   saved project, renderer, and tests agree on.
2. **Channels are first-class.** A user can inspect, connect, split, combine,
   label, and edit channels—including alpha—without Stack pretending that
   every value is a complete RGBA image.
3. **Freedom without hidden repair.** Unusual but mathematically meaningful
   graphs stay possible. Stack explains suspicious states instead of secretly
   converting, clamping, resizing, tone-mapping, or rewiring them.
4. **Simple and advanced tools can coexist.** Familiar editing controls remain
   available. When a high-level tool really can be represented by smaller
   nodes, the user can inspect or unpack it. Specialized work stays honestly
   specialized.
5. **The visible graph describes intent, not GPU chores.** Stack may combine
   work for speed, but it must preserve the exact order and result the user
   authored.
6. **Meaning is separate from storage.** A texture containing four numbers
   does not automatically mean that all four image components exist or share
   the same color, alpha, range, or spatial meaning.
7. **Changes are provable.** New or corrected behavior needs exact formulas,
   generated tests, appropriate CPU/GPU comparisons, build checks, and human
   visual review where it matters.

## Product Boundaries Already Chosen

- The rewrite is forward-only. Projects created before the rewrite do not have
  to open in the rewritten application; the older executable remains the
  compatibility path.
- Stack does not force one hidden working color space across the graph.
- Color and alpha meaning travel with data or remain explicitly unknown.
- Pixel-changing conversions are visible authored operations.
- R, G, B, and A remain independently accessible.
- RAW, model-based, frequency-domain, and similar systems may use specialized
  stages instead of pretending to be a pile of simple nodes.
- The large operation catalog is a reference library, not a promise to place
  every operation in the node browser.

## How To Use This File

Add or revise goals here in normal language. The accepted technical translation
belongs in the [channel-based design](../02-channel-based-design/README.md),
the [decision log](decision-log.md), and—when implementation behavior is exact—
the [technical contracts](../03-technical-contracts/README.md).
