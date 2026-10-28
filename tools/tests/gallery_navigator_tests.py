#!/usr/bin/env python3
"""Compile the complete navigator; record native calls without replacing behavior."""
import argparse
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--output', required=True)
p.add_argument('--source', type=Path, default=ROOT/'src/Editor/Internal/RawLab/RawLabGalleryNavigator.cpp')
a = p.parse_args()
s = a.source.read_text()
assert s.count('ImGui::Button(id, size)') == 1
s = s.replace('ImGui::Button(id, size)', 'ImGui::RecordedButton(id, size)')
s = s.replace('ImGui::RenderTextEllipsis(', 'ImGui::RecordedTextEllipsis(')
fixture = (ROOT/'tools/tests/gallery_navigator_tests.cpp').read_text()
out = Path(a.output)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(fixture.replace('// INSERT_COMPLETE_NAVIGATOR', s))
