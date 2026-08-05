# Source Ledger

Status: first source pass complete.

## Adobe

### Adobe Camera Raw overview

- Publisher: Adobe
- URL: <https://helpx.adobe.com/camera-raw/using/introduction-camera-raw.html>
- Type: official current product documentation; updated July 8, 2026
- Visual usefulness: high; includes the Camera Raw workspace, histogram,
  filmstrip, hand/zoom, and panel screenshots
- Supports: large preview/right panels/header/filmstrip layout; panel family
  inventory; clipping indicators; before/after; filmstrip orientation
- Limitation: describes interface and workflow, not the pixel-pipeline order

### Lightroom Classic Develop module tools

- Publisher: Adobe
- URL:
  <https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/develop-module-tools.html>
- Type: official product documentation
- Visual usefulness: high; workspace and comparison/reference examples
- Supports: left/center/right/bottom roles; histogram and tool strip; panel
  customization; before/after; reference view; edit and eye indicators
- Limitation: Lightroom Classic's accordion-heavy shell conflicts with the
  requested Stack direction

### Lightroom desktop editing

- Publisher: Adobe
- URL: <https://helpx.adobe.com/lightroom-cc/using/edit-photos.html>
- Type: official current product documentation; updated June 18, 2026
- Visual usefulness: medium to high; contains current edit-panel imagery
- Supports: Detail view; upper-right Edit entry; filmstrip; clipping indicators;
  contextual adjustment families
- Limitation: long page spans feature behavior more than spatial layout

### Lightroom desktop masking

- Publisher: Adobe
- URL: <https://helpx.adobe.com/lightroom/desktop/edit-photos/masking.html>
- Type: official current product documentation; updated June 18, 2026
- Visual usefulness: high; current right-toolbar and masking panel screenshots
- Supports: right-hand tool selection; contextual mask panel; overlays; local
  controls attached to selected mask
- Limitation: AI mask inventory is outside Stack's present scope

### Camera Raw masking

- Publisher: Adobe
- URL: <https://helpx.adobe.com/camera-raw/using/masking.html>
- Type: official current product documentation
- Visual usefulness: high; shows a specialized right-panel workspace
- Supports: direct-image work plus a contextual tool panel and overlay state
- Limitation: feature depth is greater than Stack currently needs

### Camera Raw tone and curve

- Publisher: Adobe
- URL:
  <https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html>
- Type: official product documentation
- Visual usefulness: medium; curve and adjustment screenshots
- Supports: broad Basic adjustment followed by Curve fine-tuning; parametric and
  point/channel curve concepts
- Limitation: does not disclose rendering implementation

## Blackmagic Design / DaVinci Resolve

### DaVinci Resolve Color product page

- Publisher: Blackmagic Design
- URL: <https://www.blackmagicdesign.com/products/davinciresolve/color>
- Type: official current product and feature documentation
- Visual usefulness: very high; numerous current palette, curve, scope, RAW,
  viewer, and node screenshots
- Supports: primary tools at bottom left; curves in central palette; scopes at
  bottom right; tool selection from palette icons; targeted viewer interaction;
  Camera Raw palette; image comparison
- Limitation: marketing-oriented and describes features more than adaptive
  layout rules

### The Colorist Guide to DaVinci Resolve 20

- Publisher: Blackmagic Design
- URL:
  <https://documents.blackmagicdesign.com/UserManuals/DaVinci-Resolve-20-Colorist-Guide.pdf>
- Type: official current training manual
- Visual usefulness: very high; pages xx through xxiv label the default Color
  page and palette rows
- Supports: fixed default regions; region hide/show; low-resolution palette
  merging; palette taxonomy; active name and hover labels; viewer comparison
  controls; default Primaries plus Curves relationship
- Limitation: a video-grading workspace is intentionally denser than Stack's
  proposed still-photo RAW surface

### DaVinci Resolve training hub

- Publisher: Blackmagic Design
- URL: <https://www.blackmagicdesign.com/products/davinciresolve/training>
- Type: official training index
- Visual usefulness: high; links to current videos, manuals, and lesson files
- Supports: color-page learning path and access to the current Colorist Guide
- Limitation: the hub itself contains little layout analysis

### Resolve color panels and palette organization

- Publisher: Blackmagic Design
- URL: <https://www.blackmagicdesign.com/products/davinciresolve/panels>
- Type: official hardware/product documentation
- Visual usefulness: medium to high
- Supports: explicit statement that intensive toolkits are organized as
  individual palettes; quick access to RAW, primaries, curves, qualifiers,
  windows, and tracker
- Limitation: hardware-focused; use only for the palette-navigation principle

## Interaction and Perceptual Design

### Direct Manipulation: A Step Beyond Programming Languages

- Author: Ben Shneiderman
- Index and citation:
  <https://www.cs.umd.edu/~ben/publications.html>
- Type: foundational peer-reviewed HCI paper, IEEE Computer 16(8), 1983
- Supports: continuous representation; physical interaction; rapid,
  incremental, reversible, immediately visible results
- Stack implication: graphs and image targets should be primary interactions;
  preview latency directly weakens the interaction model

### Progressive disclosure

- Publisher: Interaction Design Foundation; summarizes Nielsen-derived
  guidelines
- URL:
  <https://www.interaction-design.org/literature/book/the-glossary-of-human-computer-interaction/progressive-disclosure>
- Type: HCI reference
- Supports: primary/secondary feature split; clear progression path; avoid
  multiple inconsistent paths to secondary features
- Stack implication: tool switching and one secondary sheet can replace
  stacked accordions

### Apple Human Interface Guidelines: Toolbars

- Publisher: Apple
- URL:
  <https://developer.apple.com/design/human-interface-guidelines/toolbars>
- Type: official platform design guidance
- Supports: deliberate item selection; logical grouping; orientation; temporary
  hiding for distraction-free work; distinction between actions and navigation
- Stack implication: separate global actions from tool-family navigation

### Material tooltips

- Publisher: Google
- URL: <https://m1.material.io/components/tooltips.html>
- Type: official design-system guidance
- Supports: concise labels for interactive imagery on hover/focus
- Stack implication: every bare icon requires a direct plain-language name

### WCAG 2.2 target size

- Publisher: W3C Web Accessibility Initiative
- URL:
  <https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum>
- Type: official accessibility guidance
- Supports: 24 by 24 CSS-pixel minimum pointer target with defined exceptions
- Stack implication: visually bare artwork may and should retain an invisible
  usable pointer area

### Common region and perceptual grouping

- Author: Stephen E. Palmer
- URL:
  <https://www.sciencedirect.com/science/article/pii/001002859290014S>
- Type: peer-reviewed perception research
- Supports: enclosure/common region as a strong grouping cue distinct from
  proximity
- Stack implication: frames strongly imply grouping and should not be spent on
  every widget

### Gestalt grouping review

- Authors: Johan Wagemans et al.
- URL: <https://pmc.ncbi.nlm.nih.gov/articles/PMC3482144/>
- Type: peer-reviewed open review
- Supports: broader evidence on proximity, similarity, connectedness, common
  region, and figure-ground organization
- Stack implication: spacing/alignment/similarity can establish hierarchy before
  lines and boxes
