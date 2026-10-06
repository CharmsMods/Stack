"""Small real DNG burst for the Bracketing import/RAW/Queue regression."""
import argparse
from pathlib import Path
import numpy as np
from generate_color_warp_test_dng import write_dng, mosaic_rggb

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
args = parser.parse_args()
y, x = np.mgrid[:192, :256]
for index in range(3):
    dx, dy = index * .45, index * .3
    base = .22 + .06 * np.sin((x-dx)*.33) + .035*np.cos((y-dy)*.47)
    scene = np.stack((base*.8, base, base*1.1), axis=-1)
    scene += np.random.default_rng(index+317).normal(0, .003, scene.shape)
    write_dng(args.directory / f"burst-{index}.dng", mosaic_rggb(scene), {
        274: (3, 1), 33434: (5, [(1, 100)]),
        33437: (5, [(4, 1)]), 34855: (3, 400),
    })
