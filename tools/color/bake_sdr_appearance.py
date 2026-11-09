"""Bake Stack's display-HDR to SDR appearance table (no runtime Python/OCIO).

Reproduce with numpy and opencolorio==2.6.0. The ACES 2 JMh and gamut
functions supply perceptual hue handling; the neutral shoulder and highlight
color retention are Stack policy, not the ACES Output Transform/look.
See RESEARCH/windows-hdr/README.md for real-photo acceptance evidence.
"""
from pathlib import Path
import numpy as np
import PyOpenColorIO as ocio

EDGE = 129
DOMAIN = 64.0  # relative to 203 nits; covers the 10,000-nit PQ domain
SHAPE_LOG = np.log(33.0)
TO_709 = np.array([[1.660491, -.587641, -.072850],
                   [-.124551, 1.132900, -.008349],
                   [-.018151, -.100579, 1.118730]])
TO_2020 = np.array([[.627404, .329283, .043313],
                    [.069097, .919540, .011363],
                    [.016391, .088013, .895596]])


def shoulder(y):
    headroom, white, peak = 1000 / 203, .55, .98
    offset = headroom * (peak - white) / (white * headroom - peak)
    scale = peak * (headroom + offset) / headroom
    slope = scale * offset / (headroom + offset) ** 2
    low = scale * y / (y + offset)
    high = 1 - (1 - peak) / (1 + np.maximum(y-headroom, 0) * slope / (1-peak))
    return np.where(y <= headroom, low, high)


class Reference:
    def __init__(self):
        if ocio.__version__ != '2.6.0':
            raise RuntimeError('Use the pinned OpenColorIO 2.6.0 reference.')
        config = ocio.Config.CreateRaw()
        primaries = [.64, .33, .3, .6, .15, .06, .3127, .329]
        def processor(style, params, direction=ocio.TRANSFORM_DIR_FORWARD):
            return config.getProcessor(ocio.FixedFunctionTransform(style, params, direction)).getDefaultCPUProcessor()
        self.to_jmh = processor(ocio.FIXED_FUNCTION_ACES_RGB_TO_JMH_20, primaries)
        self.to_rgb = processor(ocio.FIXED_FUNCTION_ACES_RGB_TO_JMH_20, primaries, ocio.TRANSFORM_DIR_INVERSE)
        self.gamut = processor(ocio.FIXED_FUNCTION_ACES_GAMUT_COMPRESS_20, [100] + primaries)

    @staticmethod
    def apply(processor, values):
        result = np.array(values, dtype=np.float32, order='C', copy=True)
        processor.applyRGB(result)
        return result

    def __call__(self, hdr709):
        jmh = self.apply(self.to_jmh, hdr709 * 2.03)  # absolute HDR / 100 nits
        neutral = jmh.copy()
        neutral[..., 1:] = 0
        equivalent_y = np.maximum(self.apply(self.to_rgb, neutral)[..., 0], 0) / 2.03
        mapped = shoulder(equivalent_y)
        new_j = self.apply(self.to_jmh, np.repeat(mapped[..., None], 3, axis=-1))[..., 0]
        highlight = np.clip((equivalent_y - .25) / 1.75, 0, 1)
        highlight = highlight * highlight * (3 - 2 * highlight)
        # Leave dark colorfulness rescaling alone. Retain slightly more color
        # progressively into HDR highlights, before hue-preserving gamut fitting.
        exponent = .88 - .33 * highlight
        ratio = np.maximum(new_j / np.maximum(jmh[..., 0], 1e-6), 0)
        jmh[..., 1] *= ratio ** exponent
        # Reserve a little more depth for colorful highlights. Neutral
        # reflections keep the neutral peak; dark colors receive no subtraction.
        color_weight = np.clip(jmh[..., 1] / np.maximum(.6 * new_j, 1e-6), 0, 1)
        new_j -= 4.0 * highlight * color_weight
        jmh[..., 0] = new_j
        fitted = self.apply(self.gamut, jmh)
        rgb = self.apply(self.to_rgb, fitted)
        outside = (rgb.min(axis=-1) < 0) | (rgb.max(axis=-1) > 1)
        # ACES's analytical gamut boundary is approximate. RGB clipping here
        # would change hue (the car exposed excursions up to 1.13). Finish the
        # fit along a constant-hue lightness/colorfulness line, only where needed.
        if np.any(outside):
            q = fitted[outside]
            focus = np.minimum(q[:, 0], 50.0)
            low, high = np.zeros(len(q)), np.ones(len(q))
            candidate = q.copy()
            for _ in range(16):
                t = (low + high) * .5
                candidate[:, 0] = focus + (q[:, 0] - focus) * t
                candidate[:, 1] = q[:, 1] * t
                test = self.apply(self.to_rgb, candidate)
                inside = (test.min(axis=-1) >= 0) & (test.max(axis=-1) <= 1)
                low, high = np.where(inside, t, low), np.where(inside, high, t)
            candidate[:, 0] = focus + (q[:, 0] - focus) * low
            candidate[:, 1] = q[:, 1] * low
            rgb[outside] = self.apply(self.to_rgb, candidate)
        return np.clip(rgb, 0, 1)  # floating point guard after the perceptual fit


def bake():
    axis = ((np.exp(np.linspace(0, 1, EDGE) * SHAPE_LOG) - 1) / 4) ** 2
    b, g, r = np.meshgrid(axis, axis, axis, indexing='ij')
    rgb2020 = np.stack((r, g, b), axis=-1)
    result = Reference()(rgb2020 @ TO_709.T)
    # A neutral axis is exact, including black, independently of reference
    # floating point noise. Tetrahedral interpolation preserves this diagonal.
    for i, x in enumerate(axis):
        result[i, i, i] = shoulder(x)
    packed = np.round(np.sqrt(result) * 65535).astype(np.uint16)
    destination = Path(__file__).resolve().parents[2] / 'src/Hdr/Generated/SdrAppearanceLut.inc'
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('w', newline='\n') as f:
        f.write('// Generated by tools/color/bake_sdr_appearance.py with OpenColorIO 2.6.0.\n')
        f.write('// 129^3 RGB, red fastest, sqrt(linear SDR Rec.709), UNORM16.\n')
        f.write('// ACES 2 JMh/gamut reference; Stack shoulder and highlight policy.\n')
        flat = packed.ravel()
        for start in range(0, len(flat), 24):
            f.write(','.join(str(int(x)) for x in flat[start:start+24]) + ',\n')
    print(destination)


if __name__ == '__main__':
    bake()
