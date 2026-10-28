#!/usr/bin/env python3
"""Compile the actual backdrop coverage helpers, without an editor/GL context."""
import argparse
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
s = (root / 'src/Editor/Internal/RawLab/EditorModuleRawImageBackdrop.cpp').read_text()
helpers = s[s.index('BackdropPatch ClipBackdropPatch('):s.index('\nvoid ApplyBackdropBlend(')]
fixture = (root / 'tools/tests/backdrop_patch_scratch_tests.cpp').read_text()
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(fixture.replace('// PRODUCTION_HELPERS', helpers))
