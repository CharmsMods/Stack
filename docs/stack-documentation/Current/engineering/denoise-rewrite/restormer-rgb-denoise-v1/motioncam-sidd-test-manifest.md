# MotionCam and SIDD Test Manifest

Private MotionCam files are referenced by local path/hash in generated reports;
they are not committed to the repository.

Freeze at least one 100% crop for each category:

| Group | Required content |
|---|---|
| Skin | low-light face/skin with smooth gradients and real texture |
| Foliage | leaves/branches against sky or dark background |
| Fabric | repeating weave, seams, and colored cloth |
| Text | fine high-contrast lettering and low-contrast lettering |
| Shadows | chroma blotches and luminance grain in lifted dark regions |
| Highlights | scene-linear values above 1.0 and colored light sources |
| Demosaic stress | zipper/false-color-prone diagonals and fine patterns |

For every frozen crop, record source SHA-256, RAW recipe, crop coordinates,
proxy/adapter version, package/model hashes, quality/tile policy, GPU/runtime,
and outputs for:

- disabled;
- Classical Multiscale;
- Restormer Real Photo / Scene-linear Safe;
- Restormer Real Photo / Processed RGB Match;
- Restormer Gaussian Blind / Scene-linear Safe; and
- Restormer Gaussian Blind / Processed RGB Match.

SIDD validation patches are used for reproducibility and official-domain
comparison. MotionCam S24 images remain the product-domain acceptance set.
