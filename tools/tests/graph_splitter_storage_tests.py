#!/usr/bin/env python3
"""Use actual graph channel wiring with real ImGui and representative geometry.
This measures splitter storage only, not the full node renderer or UI FPS.
"""
import argparse
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
s=(root/'src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp').read_text()
h=(root/'src/Editor/NodeGraph/EditorNodeGraphUI.h').read_text()
start=s.index('    if (!m_GraphDrawSplitter)') if '    if (!m_GraphDrawSplitter)' in s else s.index('    ImDrawListSplitter graphSplitter;')
end=s.index('    graphSplitter.Split(',start)
member=next(line for line in h.splitlines() if 'std::unique_ptr<ImDrawListSplitter> m_GraphDrawSplitter;' in line)
fixture=(root/'tools/tests/graph_splitter_storage_tests.cpp').read_text()
fixture=fixture.replace('// PRODUCTION_MEMBER',member).replace('// PRODUCTION_BINDING',s[start:end])
for marker,statement in [('SPLIT','graphSplitter.Split(drawList, 2);'),('FRONT','graphSplitter.SetCurrentChannel(drawList, 1);'),('BACK','graphSplitter.SetCurrentChannel(drawList, 0);'),('MERGE','graphSplitter.Merge(drawList);')]:
    assert s.count(statement)==1,statement
    fixture=fixture.replace('// PRODUCTION_'+marker,statement)
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(fixture)
