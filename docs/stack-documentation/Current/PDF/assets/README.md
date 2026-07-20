# Manual Asset Library

This folder contains every asset that may be baked into the Stack manual PDF.
Keep originals here, keep derived PDF-ready files beside their originals, and
record each asset in `ASSET_MANIFEST.tsv` before it appears in the document.

## Folders

| Folder | Use |
| --- | --- |
| `images/` | Render comparisons, photos, and other still raster images. |
| `screenshots/` | Stack UI captures. |
| `diagrams/` | SVG diagrams and other constructed visual explanations. |
| `animations/` | Original GIFs and their PDF-ready poster frames or frame sequences. |
| `_archive/` | Retired assets preserved for reference; do not delete them casually. |

Keep the layout shallow. Do not add a new category unless the existing four
active folders are genuinely ambiguous.

## One Manifest, One Stable ID

`ASSET_MANIFEST.tsv` is the source of truth for asset identity and use. Every
manual asset gets one stable `asset_id`, such as `img-node-browser-search` or
`diag-render-pipeline`. The ID stays the same if the asset is moved in the PDF
or its filename receives a new dated capture.

Add a manifest row before or in the same change as its first PDF use. Record:

- the original source file;
- the exact PDF-ready file referenced by Typst;
- the section label that uses it, such as `ch-introduction`;
- concise alt text suitable for the final figure;
- provenance or rights information; and
- the current status: `candidate`, `ready`, `used`, `replace`, or `archived`.

An asset may use the same path for `source_path` and `pdf_path` when no derived
file is needed.

## Naming and Derivatives

Use lowercase kebab-case with a clear subject and purpose:

```text
assets/images/glass-sphere-before-tone-map.png
assets/screenshots/node-browser-search-open-2026-07-12.png
assets/diagrams/render-pipeline-overview.svg
assets/animations/solver-convergence-loop.gif
assets/animations/solver-convergence-loop-poster.png
```

When an original needs cropping, annotation, color conversion, or resolution
reduction for the PDF, preserve the original and create a sibling derivative:

```text
subject-source.png
subject-print.png
subject-poster.png
subject-frame-sequence.svg
```

Never overwrite a meaningful source asset with a PDF derivative. If a product
state changes, make a new state-specific or dated file and keep the earlier
asset intact. Move retired material to `_archive/` and change its manifest
status to `archived` rather than deleting it.

## Format Rules

- Prefer SVG for diagrams and plots. Keep text native in Typst where possible;
  do not bake ordinary document text into an SVG.
- Use PNG for UI screenshots, comparisons, transparency, and sharp raster
  graphics. Use JPEG only when a photographic image needs a smaller file.
- Keep screenshots and comparison pairs at the same crop and pixel dimensions.
- Capture raster media at roughly twice its intended printed dimensions when
  practical. Use a PDF-ready derivative if the original is unnecessarily large.
- Add a meaningful `alt` description when placing the asset in Typst. The
  manifest's `alt_text` column is the reusable source for that description.

## GIF and Motion Policy

Keep GIFs in `animations/` as source records, but do not rely on the PDF to
animate them. Typst can load GIF files, but they become static in PDF output.
For every motion asset, create and manifest one intentional PDF representation:

- a `-poster.png` for the representative frame; or
- a `-frame-sequence.svg` or labelled still panel when movement is part of the
  explanation.

Use the poster or frame sequence in the Typst source, not the original GIF.
Record both paths in the manifest so the motion source remains recoverable.

## Using an Asset in Typst

Use a project-root path and copy the manifest alt text into the image call:

```typst
#figure(
  image(
    "/assets/diagrams/render-pipeline-overview.svg",
    width: 100%,
    alt: "Overview of the Stack render pipeline from source image to display output.",
  ),
  caption: [Render pipeline overview.],
) <fig-render-pipeline-overview>
```

Before marking an asset `used`, build the PDF and inspect the rendered PNG
pages for crop, sharpness, legibility, contrast, and caption placement.
