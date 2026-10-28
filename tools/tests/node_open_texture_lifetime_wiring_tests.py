"""Keep submitted node textures on the common owner pump and GL teardown path."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]

def body(path, declaration):
    source = (root / path).read_text()
    start = source.index(declaration)
    return source[start:source.index('\n}', start)]

pump = body('src/Editor/Internal/EditorModulePersistence.cpp',
            'void EditorModule::PumpNonRenderingWork(')
release = pump.index('m_NodeOpenTransition.ReleaseSubmittedDraw();')
assert release < pump.index('return;'), 'Inactive/duplicate-frame pump must not skip texture retirement'
assert release < pump.index('if (ImGui::GetCurrentContext())'), 'Common cleanup must not depend on foreground/context branch'
shutdown = body('src/Editor/EditorModule.cpp', 'void EditorModule::Shutdown()')
assert 'm_NodeOpenTransition.Shutdown();' in shutdown, 'GL teardown must explicitly release submitted textures'
assert 'm_NodeOpenTransition.Reset();' not in shutdown, 'Reset alone retains same-frame submitted references'
workspaces = (root / 'src/App/AppProjectWorkspaces.cpp').read_text()
pump_call = workspaces.index('workspace->editor->PumpNonRenderingWork(')
loop = workspaces.rindex('for (auto& workspace : m_ProjectWorkspaces)', 0, pump_call)
assert 'if (' not in workspaces[loop:pump_call], 'Hidden project owners must receive the common pump'
assert 'foreground ? 2.5 : 0.5, foreground' in workspaces[pump_call:pump_call + 120]
print('Node appearance lifetime wiring passed (common pump, hidden owners, explicit shutdown).')
