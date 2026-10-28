#!/usr/bin/env python3
"""Exact production readout draw helper and magnifier placement, real ImGui.
Viewport geometry is supplied headlessly; this is not a desktop/OS monitor test.
"""
import argparse
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
s=(root/'src/Editor/UI/EditorViewport.cpp').read_text()
def block(marker):
    start=s.index(marker);end=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
start=s.index('                ImVec2 magPos =')
end=s.index('                ImDrawList* fg =',start)
fixture=(root/'tools/tests/viewport_cursor_overlay_tests.cpp').read_text()
fixture=fixture.replace('// PRODUCTION_READOUT',block('ImU32 ApplyAlpha(')+'\n'+block('void DrawPixelInspectionCursorReadout('))
fixture=fixture.replace('// PRODUCTION_MAGNIFIER_PLACEMENT',s[start:end])
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(fixture)
