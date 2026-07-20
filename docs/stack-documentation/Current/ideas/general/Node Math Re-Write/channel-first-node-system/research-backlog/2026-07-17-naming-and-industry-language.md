# Naming And Industry Language Research

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: research
- Topic: channel-language
- Verification: unverified; research not yet performed

## Research Goal

Find professional terms that are mathematically accurate, familiar enough to
technical users, and teachable to non-specialists without forcing the UI to use
implementation jargon.

## Terms To Compare

- scalar
- uniform
- constant
- value
- parameter
- field
- scalar field
- channel
- image plane
- band
- component
- texture
- map
- mask
- image
- channel bundle
- resource
- data

## Questions

- Which terms are common in compositing, color grading, shader graphs, computer
  vision, scientific imaging, and game material tools?
- When does `channel` imply a component of an image rather than an arbitrary
  one-value-per-pixel map?
- Does `map` communicate depth, normals, masks, exposure, and auxiliary data
  more naturally than `field`?
- Can the UI say `Value` and `Channel` while technical inspection reports
  `uniform scalar` and `per-pixel scalar field`?
- Should Mask be a role/preset of Channel or remain a distinct compatibility
  family?
- How do professional tools explain broadcasting one Value across every pixel?

## Candidate Sources For A Later Research Pass

- Official documentation for established compositing and node applications.
- OpenGL/GLSL and shading-language terminology for uniforms, components, and
  textures.
- MaterialX and OpenUSD node/value terminology.
- Image-processing and computer-vision references for scalar images, bands,
  masks, and fields.

## Deliverable

A comparison table with technical term, proposed friendly label, precise Stack
meaning, ambiguity risks, and example pin/wire text. Research should inform a
later naming decision; it should not rename code by itself.

