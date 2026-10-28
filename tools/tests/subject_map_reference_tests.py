#!/usr/bin/env python3
"""Extract the actual read-only solved-map parse, excluding fallback interpretation."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
s=(root/'src/Editor/Internal/EditorModuleDevelopSubjectImportance.cpp').read_text()
start=s.index('        auto appendRefinedMapCellFromJson =')
end=s.index('        if (!outState.refinedMapActive)',start)
block=s[start:end].replace('node->rawDevelop.integratedToneLayerJson','tone')
old='''        const nlohmann::json solvedRefinedMap = tone.value(
            "autoSubjectSceneRefinedMap", nlohmann::json::object());
        const nlohmann::json solvedRefinedCells = solvedRefinedMap.value("cells", nlohmann::json::array());
'''
lookup=block.index('        // Borrow only') if '        // Borrow only' in block else block.index('        const nlohmann::json solvedRefinedMap =')
check=block.index('        if (solvedRefinedMap.value')
reference=block[:lookup]+old+block[check:]
types=(root/'src/Editor/EditorModuleTypes.h').read_text();start=types.index('struct DevelopSubjectViewportMapCell {');end=types.index('\n};',start)+3
fixture=(root/'tools/tests/subject_map_reference_tests.cpp').read_text().replace('// ACTUAL_CELL_TYPE',types[start:end]).replace('// ACTUAL_PARSE',block).replace('// ORIGINAL_PARSE',reference)
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(fixture)
