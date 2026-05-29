# Local Tone Mapping Plan

Local tone mapping is intentionally deferred from the first RAW dynamic-range pass.

The future node should be a normal graph layer, not part of the RAW node. It should work on RAW output, PNG/JPEG sources, generated images, and any upstream graph texture.

Expected controls:

- Strength
- Radius
- Detail / local contrast
- Halo suppression

Implementation direction:

- Build a luminance buffer from the input image.
- Generate one or more blurred luminance buffers at the requested radius.
- Compress local contrast by comparing source luminance against blurred/base luminance.
- Re-apply the mapped luminance to RGB using a safe hue-preserving scale.
- Use separate FBOs/textures for the blur chain; do not overload the single-pass global tone mapper.

Known risks:

- Large radii need efficient separable or mip/pyramid blur.
- Aggressive settings can create halos around sky/subject edges.
- The node should ship only after visual testing on high-contrast RAW scenes.
