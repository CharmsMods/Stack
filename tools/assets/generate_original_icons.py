#!/usr/bin/env python3
"""Export the six original SVG drawings without modifying a Stack checkout.

Requires Python 3, Pillow, and Inkscape on PATH. No network or fonts are used
for icon exports. Preview labels optionally use a local DejaVu Sans font.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SPECS = [
    ("visibility-on", "Visibility on", "Assets/Icons/eye-svgrepo-com.png", 256, 255),
    ("visibility-off", "Visibility off", "Assets/Icons/eye-closed-svgrepo-com.png", 256, 255),
    ("view-transform", "View transform", "Assets/Icons/view transform.png", 256, 255),
    ("gallery", "Gallery", "Assets/Icons/Raw Tab/gallery.png", 256, 255),
    ("custom-mask", "Custom mask", "Assets/Icons/custom mask.png", 256, 255),
    ("pop-out", "Pop out canvas", "Assets/Icons/popout canvas window.png", 980, 0),
]


def verify_svg(path):
    """Reject external assets, scripts, font/text dependencies and unsafe URLs."""
    tree = ET.parse(path)
    allowed = {"svg", "title", "g", "path", "rect", "circle", "defs", "mask"}
    for node in tree.iter():
        tag = node.tag.rsplit("}", 1)[-1]
        if tag not in allowed:
            raise ValueError(f"Unexpected SVG element: {path}: {tag}")
        for key, value in node.attrib.items():
            if "href" in key or key.startswith("on") or "font" in key:
                raise ValueError(f"External/active/font attribute: {path}: {key}")
            if "url(" in value and not value.startswith("url(#"):
                raise ValueError(f"External SVG URL: {path}")
    return tree.getroot()


def export_icons(output):
    inkscape = shutil.which("inkscape")
    if not inkscape:
        raise SystemExit("Install Inkscape and put its executable on PATH.")
    results = []
    with tempfile.TemporaryDirectory(prefix="stack-original-icons-") as tmp:
        temp = Path(tmp)
        env = dict(os.environ, INKSCAPE_PROFILE_DIR=str(temp / "profile"))
        for stem, label, target, size, value in SPECS:
            source = ROOT / "Assets" / "Icons" / "Original" / (stem + ".svg")
            svg = verify_svg(source)
            assert svg.attrib["viewBox"] == "0 0 24 24"
            assert int(svg.attrib["width"]) == size == int(svg.attrib["height"])
            raw = temp / (stem + ".png")
            subprocess.run([
                inkscape, str(source), "--export-type=png",
                f"--export-filename={raw}", f"--export-width={size}",
                f"--export-height={size}", "--export-background-opacity=0",
            ], env=env, check=True, capture_output=True)
            with Image.open(raw) as rendered:
                alpha = rendered.convert("RGBA").getchannel("A")
                # A uniform RGB plane preserves the app's multiplicative tint.
                # Pop-out remains black because bake_tab_icons.py inverts it.
                rgba = Image.new("RGBA", (size, size), (value, value, value, 0))
                rgba.putalpha(alpha)
                destination = output / target
                destination.parent.mkdir(parents=True, exist_ok=True)
                rgba.save(destination, format="PNG", compress_level=9)
                assert alpha.getextrema() == (0, 255)
                box = alpha.getbbox()
                assert box and box[0] > 0 and box[1] > 0 and box[2] < size and box[3] < size
            results.append({
                "source": "Assets/Icons/Original/" + source.name, "target": target,
                "dimensions": [size, size], "rgb": [value] * 3,
                "alpha": "transparent background with antialiased geometry",
                "bake_transform": "invert" if value == 0 else "none",
                "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            })
    return results


def font(size, bold=False):
    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    try:
        return ImageFont.truetype(name, size)
    except OSError:
        return ImageFont.load_default(size=size)


def preview(output):
    canvas = Image.new("RGB", (1380, 910), "#eef0f4")
    draw = ImageDraw.Draw(canvas)
    draw.text((46, 30), "STACK / ORIGINAL UI ICONS", fill="#18202c", font=font(30, True))
    draw.text((46, 77), "Six original geometric replacements. Original geometry retained with the project source.",
              fill="#586273", font=font(18))
    draw.text((46, 108), "Large samples + actual 16 / 24 / 32 px sizes, on light and dark backgrounds.",
              fill="#586273", font=font(16))
    for i, (stem, label, target, size, value) in enumerate(SPECS):
        x = 42 + (i % 3) * 448
        y = 156 + (i // 3) * 348
        draw.rounded_rectangle((x, y, x + 420, y + 322), radius=16, fill="white")
        draw.text((x + 20, y + 18), label, fill="#18202c", font=font(21, True))
        draw.text((x + 20, y + 51), f"{size} x {size} PNG  |  transparent", fill="#667080", font=font(13))
        alpha = Image.open(output / target).getchannel("A")
        for j, (bg, fg) in enumerate([("#f3f5f8", "#263140"), ("#202631", "#edf2fa")]):
            px, py = x + 16 + j * 196, y + 84
            draw.rounded_rectangle((px, py, px + 192, py + 224), radius=10, fill=bg)
            draw.text((px + 14, py + 12), "LIGHT" if j == 0 else "DARK", fill=fg, font=font(11, True))
            def paste_icon(left, top, width):
                a = alpha.resize((width, width), Image.Resampling.LANCZOS)
                icon = Image.new("RGBA", (width, width), fg)
                icon.putalpha(a)
                canvas.paste(icon, (left, top), icon)
            paste_icon(px + 60, py + 45, 72)
            draw.line((px + 14, py + 141, px + 178, py + 141), fill="#d4dce7" if j == 0 else "#3a4555")
            for center, pixel_size in [(38, 16), (96, 24), (154, 32)]:
                paste_icon(px + center - pixel_size // 2, py + 170 - pixel_size // 2, pixel_size)
                text = str(pixel_size)
                width = draw.textlength(text, font=font(11))
                draw.text((px + center - width / 2, py + 195), text, fill=fg, font=font(11))
    draw.text((46, 872), "White source pixels for themed tinting; pop-out stays black to preserve Stack's existing invert-at-bake step.",
              fill="#586273", font=font(14))
    canvas.save(output / "Stack-original-icons-preview.png", compress_level=9)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "_workspace" / "original-icon-exports",
                        help="Export directory, not a repository root (default: _workspace/original-icon-exports)")
    parser.add_argument("--no-preview", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = export_icons(output)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if not args.no_preview:
        preview(output)
    print(json.dumps({"output": str(output), "icons": len(manifest), "verified": True}))


if __name__ == "__main__":
    main()
