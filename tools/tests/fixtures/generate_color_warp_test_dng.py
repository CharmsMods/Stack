#!/usr/bin/env python3
"""Generate Stack's deterministic synthetic Color Warp mosaic DNG fixture.

The file is a real, uncompressed, 16-bit RGGB CFA DNG.  The scene is authored
in linear RGB and then sampled through the Bayer mosaic so normal RAW loading,
demosaicing, color conversion, and Develop processing are exercised.
"""

from __future__ import annotations

import argparse
import colorsys
import datetime as dt
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image


BLACK_LEVEL = 512
WHITE_LEVEL = 16383
DEFAULT_WIDTH = 2048
DEFAULT_HEIGHT = 1536
SEED = 0x53544143


def hsv_to_rgb(h: np.ndarray, s: np.ndarray, v: np.ndarray) -> np.ndarray:
    h6 = (h % 1.0) * 6.0
    sector = np.floor(h6).astype(np.int32)
    fraction = h6 - sector
    p = v * (1.0 - s)
    q = v * (1.0 - s * fraction)
    t = v * (1.0 - s * (1.0 - fraction))
    choices = (
        np.stack((v, t, p), axis=-1),
        np.stack((q, v, p), axis=-1),
        np.stack((p, v, t), axis=-1),
        np.stack((p, q, v), axis=-1),
        np.stack((t, p, v), axis=-1),
        np.stack((v, p, q), axis=-1),
    )
    rgb = np.empty((*h.shape, 3), dtype=np.float32)
    for index, choice in enumerate(choices):
        mask = sector == index
        rgb[mask] = choice[mask]
    return rgb


def make_pattern(width: int, height: int) -> np.ndarray:
    rng = np.random.default_rng(SEED)
    yy, xx = np.mgrid[0:height, 0:width]
    x = (xx + 0.5) / width
    y = (yy + 0.5) / height

    # A quiet neutral base makes dark-region leakage easy to see.
    scene = np.full((height, width, 3), 0.012, dtype=np.float32)
    scene += (0.018 * x + 0.010 * y)[..., None]

    def rect(x0: float, y0: float, x1: float, y1: float, color) -> np.ndarray:
        mask = (x >= x0) & (x < x1) & (y >= y0) & (y < y1)
        scene[mask] = np.asarray(color, dtype=np.float32)
        return mask

    def ellipse(cx: float, cy: float, rx: float, ry: float, color) -> np.ndarray:
        mask = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0
        scene[mask] = np.asarray(color, dtype=np.float32)
        return mask

    # Top: the same eight hue families repeated at four separated exposures.
    hues = np.asarray([0.00, 0.055, 0.13, 0.30, 0.49, 0.58, 0.72, 0.88])
    levels = np.asarray([0.025, 0.085, 0.27, 0.78])
    margin, gap = 0.018, 0.004
    cell_w = (0.964 - gap * 7) / 8
    cell_h = 0.105
    for row, level in enumerate(levels):
        for col, hue in enumerate(hues):
            x0 = margin + col * (cell_w + gap)
            y0 = 0.018 + row * (cell_h + gap)
            mask = (x >= x0) & (x < x0 + cell_w) & (y >= y0) & (y < y0 + cell_h)
            scene[mask] = colorsys.hsv_to_rgb(float(hue), 0.76, float(level))

    # Middle-left: disconnected matching red islands, holes, bridges, and intruders.
    red = (0.42, 0.035, 0.025)
    red_dark = (0.11, 0.009, 0.007)
    ellipse(0.105, 0.585, 0.075, 0.075, red)
    ellipse(0.315, 0.585, 0.075, 0.075, red)
    ellipse(0.105, 0.785, 0.075, 0.075, red_dark)
    ellipse(0.315, 0.785, 0.075, 0.075, red_dark)
    ellipse(0.105, 0.585, 0.022, 0.022, (0.015, 0.18, 0.035))
    # A weak red bridge plus a strong green barrier distinguishes reach/edge stop.
    rect(0.172, 0.574, 0.248, 0.596, (0.16, 0.014, 0.011))
    rect(0.248, 0.50, 0.260, 0.87, (0.008, 0.38, 0.04))

    # Center: continuous hue/lightness gradients and a hard-edged inset.
    grad_mask = (x >= 0.40) & (x < 0.70) & (y >= 0.47) & (y < 0.88)
    gx = np.clip((x - 0.40) / 0.30, 0.0, 1.0)
    gy = np.clip((y - 0.47) / 0.41, 0.0, 1.0)
    grad_h = 0.50 + 0.23 * gx
    grad_v = 0.025 * np.power(24.0, gy)
    grad_rgb = hsv_to_rgb(grad_h, np.full_like(x, 0.70), grad_v)
    scene[grad_mask] = grad_rgb[grad_mask]
    rect(0.505, 0.575, 0.595, 0.755, (0.72, 0.09, 0.62))

    # Right: clean/noisy twins and a smooth falloff for boundary feather tests.
    clean = rect(0.735, 0.49, 0.855, 0.655, (0.035, 0.32, 0.48))
    noisy = rect(0.865, 0.49, 0.985, 0.655, (0.035, 0.32, 0.48))
    noise = rng.normal(0.0, 1.0, (height, width, 1)).astype(np.float32)
    shot_scale = 0.010 + 0.055 * np.sqrt(np.maximum(scene, 0.0))
    scene[noisy] = np.clip(scene[noisy] + noise.repeat(3, axis=2)[noisy] * shot_scale[noisy], 0.0, 1.0)
    # A clean patch contains sparse same-family and foreign-color speckles.
    speckles = clean & (rng.random((height, width)) < 0.006)
    foreign = clean & (rng.random((height, width)) < 0.002)
    scene[speckles] = (0.015, 0.10, 0.17)
    scene[foreign] = (0.62, 0.04, 0.015)

    cx, cy = 0.86, 0.80
    radius = np.sqrt(((x - cx) / 0.15) ** 2 + ((y - cy) / 0.14) ** 2)
    falloff = np.clip(1.0 - radius, 0.0, 1.0) ** 1.7
    feather_patch = radius <= 1.0
    feather_color = np.stack((0.52 * falloff, 0.18 * falloff, 0.025 * falloff), axis=-1)
    scene[feather_patch] = feather_color[feather_patch] + 0.008

    # Bottom: tiny features, checkerboards, lines, and scrambled color fragments.
    checker = ((xx // 4 + yy // 4) & 1).astype(bool)
    checker_area = (x >= 0.018) & (x < 0.20) & (y >= 0.90) & (y < 0.982)
    scene[checker_area & checker] = (0.70, 0.70, 0.70)
    scene[checker_area & ~checker] = (0.012, 0.012, 0.012)

    stripe_area = (x >= 0.215) & (x < 0.40) & (y >= 0.90) & (y < 0.982)
    stripes = ((xx // 3) % 6) / 5.0
    stripe_rgb = hsv_to_rgb((stripes * 0.92), np.full_like(x, 0.85), np.full_like(x, 0.52))
    scene[stripe_area] = stripe_rgb[stripe_area]

    # Deterministic mini-Voronoi field: small irregular regions at mixed EVs.
    field_x0, field_x1 = int(width * 0.415), int(width * 0.985)
    field_y0, field_y1 = int(height * 0.90), int(height * 0.982)
    field_x = x[field_y0:field_y1, field_x0:field_x1]
    field_y = y[field_y0:field_y1, field_x0:field_x1]
    seed_count = 58
    sx = rng.uniform(0.415, 0.985, seed_count)
    sy = rng.uniform(0.90, 0.982, seed_count)
    sh = rng.random(seed_count)
    sv = rng.choice(np.asarray([0.025, 0.08, 0.22, 0.62]), seed_count)
    nearest_distance = np.full(field_x.shape, np.inf, dtype=np.float32)
    nearest_index = np.zeros(field_x.shape, dtype=np.int16)
    for index in range(seed_count):
        distance = (field_x - sx[index]) ** 2 + ((field_y - sy[index]) / 0.145) ** 2
        update = distance < nearest_distance
        nearest_distance[update] = distance[update]
        nearest_index[update] = index
    field_scene = scene[field_y0:field_y1, field_x0:field_x1]
    for index in range(seed_count):
        field_scene[nearest_index == index] = colorsys.hsv_to_rgb(
            float(sh[index]), 0.80, float(sv[index]))

    # Orientation fiducials: white stepped L at top-left, RGB bars at top-right.
    rect(0.004, 0.004, 0.010, 0.115, (0.92, 0.92, 0.92))
    rect(0.004, 0.004, 0.065, 0.012, (0.92, 0.92, 0.92))
    rect(0.940, 0.004, 0.956, 0.055, (0.85, 0.01, 0.01))
    rect(0.958, 0.004, 0.974, 0.055, (0.01, 0.85, 0.01))
    rect(0.976, 0.004, 0.992, 0.055, (0.01, 0.01, 0.85))

    return np.clip(scene, 0.0, 1.0)


def mosaic_rggb(scene: np.ndarray) -> np.ndarray:
    height, width, _ = scene.shape
    mosaic = np.empty((height, width), dtype=np.float32)
    mosaic[0::2, 0::2] = scene[0::2, 0::2, 0]
    mosaic[0::2, 1::2] = scene[0::2, 1::2, 1]
    mosaic[1::2, 0::2] = scene[1::2, 0::2, 1]
    mosaic[1::2, 1::2] = scene[1::2, 1::2, 2]
    sensor = BLACK_LEVEL + mosaic * (WHITE_LEVEL - BLACK_LEVEL)
    return np.rint(sensor).clip(BLACK_LEVEL, WHITE_LEVEL).astype("<u2")


TYPE_BYTE, TYPE_ASCII, TYPE_SHORT, TYPE_LONG = 1, 2, 3, 4
TYPE_RATIONAL, TYPE_SRATIONAL = 5, 10
TYPE_SIZES = {TYPE_BYTE: 1, TYPE_ASCII: 1, TYPE_SHORT: 2, TYPE_LONG: 4,
              TYPE_RATIONAL: 8, TYPE_SRATIONAL: 8}


def packed_values(type_id: int, values) -> tuple[int, bytes]:
    if type_id == TYPE_ASCII:
        payload = values.encode("ascii") + b"\0"
        return len(payload), payload
    values = tuple(values) if isinstance(values, (tuple, list)) else (values,)
    formats = {TYPE_BYTE: "B", TYPE_SHORT: "H", TYPE_LONG: "I"}
    if type_id in formats:
        return len(values), struct.pack("<" + formats[type_id] * len(values), *values)
    flat = tuple(component for pair in values for component in pair)
    fmt = "I" if type_id == TYPE_RATIONAL else "i"
    return len(values), struct.pack("<" + fmt * len(flat), *flat)


def write_dng(path: Path, mosaic: np.ndarray, extra_tags=None) -> None:
    height, width = mosaic.shape
    million = 1_000_000
    # DNG ColorMatrix maps XYZ (D50) to camera coordinates. Our authored
    # camera coordinates are linear sRGB, so this is inverse(sRGB-to-XYZ D50).
    color_matrix = [
        (3133856, million), (-1616867, million), (-490615, million),
        (-978769, million), (1916142, million), (33454, million),
        (71945, million), (-228991, million), (1405243, million),
    ]
    timestamp = dt.datetime.now().strftime("%Y:%m:%d %H:%M:%S")
    tags = {
        254: (TYPE_LONG, 0),
        256: (TYPE_LONG, width),
        257: (TYPE_LONG, height),
        258: (TYPE_SHORT, 16),
        259: (TYPE_SHORT, 1),
        262: (TYPE_SHORT, 32803),
        271: (TYPE_ASCII, "Stack Synthetic"),
        272: (TYPE_ASCII, "Color Warp Mosaic Test"),
        273: (TYPE_LONG, 0),  # Filled after metadata layout is known.
        # Stack uploads RAW rows into an OpenGL texture. Orientation 4 supplies
        # the vertical storage-to-display correction for this generated file.
        274: (TYPE_SHORT, 4),
        277: (TYPE_SHORT, 1),
        278: (TYPE_LONG, height),
        279: (TYPE_LONG, width * height * 2),
        284: (TYPE_SHORT, 1),
        305: (TYPE_ASCII, "Stack synthetic RAW generator"),
        306: (TYPE_ASCII, timestamp),
        33421: (TYPE_SHORT, (2, 2)),
        33422: (TYPE_BYTE, (0, 1, 1, 2)),
        50706: (TYPE_BYTE, (1, 4, 0, 0)),
        50707: (TYPE_BYTE, (1, 1, 0, 0)),
        50708: (TYPE_ASCII, "Stack Color Warp Synthetic RGGB"),
        50710: (TYPE_BYTE, (0, 1, 2)),
        50711: (TYPE_SHORT, 1),
        50713: (TYPE_SHORT, (2, 2)),
        50714: (TYPE_RATIONAL, [(BLACK_LEVEL, 1)] * 4),
        50717: (TYPE_LONG, WHITE_LEVEL),
        50718: (TYPE_RATIONAL, [(1, 1), (1, 1)]),
        50719: (TYPE_LONG, (0, 0)),
        50720: (TYPE_LONG, (width, height)),
        50721: (TYPE_SRATIONAL, color_matrix),
        50728: (TYPE_RATIONAL, [(1, 1), (1, 1), (1, 1)]),
        50730: (TYPE_SRATIONAL, [(0, 1)]),
        50778: (TYPE_SHORT, 23),  # D50, matching ColorMatrix1.
        50829: (TYPE_LONG, (0, 0, height, width)),
    }

    if extra_tags:
        tags.update(extra_tags)
    sorted_tags = sorted(tags)
    ifd_offset = 8
    ifd_size = 2 + 12 * len(sorted_tags) + 4
    external_offset = ifd_offset + ifd_size
    payloads: list[tuple[int, bytes]] = []
    entries: dict[int, tuple[int, int, bytes]] = {}

    # Lay out all metadata payloads first. StripOffsets itself is always inline.
    for tag in sorted_tags:
        type_id, value = tags[tag]
        count, payload = packed_values(type_id, value)
        if tag == 273:
            entries[tag] = (type_id, count, b"\0\0\0\0")
        elif len(payload) <= 4:
            entries[tag] = (type_id, count, payload.ljust(4, b"\0"))
        else:
            external_offset = (external_offset + 3) & ~3
            entries[tag] = (type_id, count, struct.pack("<I", external_offset))
            payloads.append((external_offset, payload))
            external_offset += len(payload)

    image_offset = (external_offset + 3) & ~3
    entries[273] = (TYPE_LONG, 1, struct.pack("<I", image_offset))

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(b"II")
        output.write(struct.pack("<HI", 42, ifd_offset))
        output.write(struct.pack("<H", len(sorted_tags)))
        for tag in sorted_tags:
            type_id, count, value_or_offset = entries[tag]
            output.write(struct.pack("<HHI", tag, type_id, count))
            output.write(value_or_offset)
        output.write(struct.pack("<I", 0))
        for offset, payload in payloads:
            output.write(b"\0" * (offset - output.tell()))
            output.write(payload)
        output.write(b"\0" * (image_offset - output.tell()))
        output.write(mosaic.tobytes(order="C"))


def write_preview(path: Path, scene: np.ndarray) -> None:
    srgb = np.where(scene <= 0.0031308, scene * 12.92,
                    1.055 * np.power(scene, 1.0 / 2.4) - 0.055)
    pixels = np.rint(np.clip(srgb, 0.0, 1.0) * 255.0).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels, "RGB").save(path, optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", type=Path,
                        default=Path("test-assets/color-warp/color-warp-mosaic-test.dng"))
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--preview", type=Path,
                        default=Path("test-assets/color-warp/color-warp-mosaic-reference.png"))
    args = parser.parse_args()
    if args.width < 512 or args.height < 512 or args.width % 2 or args.height % 2:
        parser.error("width and height must be even and at least 512 pixels")

    scene = make_pattern(args.width, args.height)
    mosaic = mosaic_rggb(scene)
    write_dng(args.output, mosaic)
    write_preview(args.preview, scene)
    print(f"Wrote {args.output} ({args.width}x{args.height}, RGGB, 16-bit DNG)")
    print(f"Wrote {args.preview} (linear-scene reference rendered to sRGB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
