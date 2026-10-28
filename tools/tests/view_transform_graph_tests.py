#!/usr/bin/env python3
"""Extract complete production graph and its exact math/JSON dependencies."""
import argparse
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--output',required=True)
p.add_argument('--source',type=Path,default=ROOT/'src/Editor/Internal/RawLab/EditorModuleRawLabView.cpp')
a=p.parse_args()
def function(source,signature):
    start=source.index(signature); at=source.index('{',start)+1; depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}'); at+=1
    return source[start:at]
s=a.source.read_text()
# The shipping caller retains its disabled scope and the helper's changed result.
assert 'ImGui::BeginDisabled(!controlsEnabled);' in s
assert 'if (DrawViewTransformGraph(' in s
host=(ROOT/'src/Editor/Internal/EditorModuleRawWorkspaceLab.cpp').read_text()
assert 'ImGui::IsAnyItemActive() ||' in host
assert 'CommitRawWorkspaceEditContext(context, changed, interactionActive);' in host
assert 'ResolveRawWorkspaceInteractionDraft(true);' in host
assert 'if (!interactionActive) m_Project->rawLayers.EndGesture();' in host
parts=[function(s,name) for name in ('float JsonFloat(', 'bool JsonBoolean(', 'float EncodeViewGraphOutput(', 'ImVec2 GraphToScreen(', 'bool DrawViewTransformGraph(')]
recipe=(ROOT/'src/Raw/RawDevelopmentRecipe.cpp').read_text()
math='namespace Stack::RawRecipe {\n'+function(recipe,'float EvaluateViewTransformDisplayLuma(')+'\n}\n'
fixture=(ROOT/'tools/tests/view_transform_graph_tests.cpp').read_text()
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(fixture.replace('// INSERT_PRODUCTION_HELPERS',math+'\n'.join(parts)))
