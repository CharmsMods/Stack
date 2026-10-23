# Original replacement UI icons

The six PNGs below were replaced with newly authored geometric drawings on
October 23, 2026, after maintainer visual approval. No third-party vector paths,
images or icon fonts were imported or traced for these replacements. Existing
images were inspected only for dimensions, transparency, tint handling and UI
roles. This records provenance, not a guarantee of copyright eligibility.
The project's GPL-3.0-only grant covers rights held in these original assets.

| Editable source in `Assets/Icons/Original/` | PNG path in `Assets/Icons/` |
| --- | --- |
| `visibility-on.svg` | `eye-svgrepo-com.png` |
| `visibility-off.svg` | `eye-closed-svgrepo-com.png` |
| `view-transform.svg` | `view transform.png` |
| `gallery.svg` | `Raw Tab/gallery.png` |
| `custom-mask.svg` | `custom mask.png` |
| `pop-out.svg` | `popout canvas window.png` |

The historical eye filenames remain for existing code references; the replacement
artwork is not from SVG Repo. This does not relicense the previous artwork or
change earlier releases. `Raw Tab Side Bar/view transform.png` is a different
asset and is not included in this replacement set. The separately retained
`arrow-right-fill.svg` has Phosphor MIT attribution in `third_party/`.

## Re-export and embed

Run `python tools/assets/generate_original_icons.py --no-preview` from the
repository root. It requires Python 3, Pillow and Inkscape on PATH. It writes
to `_workspace/original-icon-exports/`, never directly over source assets by
default. Review the exports before copying their six `Assets/Icons/` paths
into the repository. The script also writes a SHA-256 manifest; `--output`
selects a different isolated output directory.

The five 256-by-256 PNGs use white RGB with transparency. The 980-by-980 pop-out
source uses black RGB because the existing `bake_tab_icons.py` applies `invert`
to it. Keep both sides of that contract together. SVG files use self-contained
geometry without external images, scripts or fonts. Export bytes can change
with tool versions; the validated toolchain was Python 3.12.14, Pillow 12.3.0
and Inkscape 1.4.

The normal build runs `tools/assets/bake_tab_icons.py` to regenerate the ignored
`src/App/Resources/EmbeddedTabIcons.h`. Do not commit that generated header or
copy a stale one into a build. The six approved PNGs were re-exported and
byte-compared, and a local bake changed exactly their six embedding symbols.
This is not a Windows application build or runtime visual test.
