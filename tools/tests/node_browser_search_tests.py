"""Extract current-frame browser search/filter/activation code for real ImGui input."""
from pathlib import Path
import argparse

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--source', type=Path)
a = p.parse_args()
s = (a.source or root / 'src/Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp').read_text()
def block(marker):
    start = s.index(marker)
    opening = s.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]
start = s.index('    const std::string search = m_NodeBrowserSearchBuffer;')
end_marker = 'm_NodeBrowserEntryAnimationsSettled = settled;'
end = s.index(end_marker, start) + len(end_marker)
end = s.index('}', end) + 1
filter_code = s[start:end]
input_code = next(line.strip() for line in s.splitlines() if 'ImGui::InputTextWithHint("##NodesPanelSearch"' in line)
# Preserve both input/filter and closed-return ordering for old-source negatives.
closed_return = block('if (!isOpen && alpha <= 0.01f)')
filter_before_input = start < s.index(input_code)
before_window = (filter_code + '\n' + closed_return if filter_before_input else closed_return)
ordered = (input_code if filter_before_input else input_code + '\n' + filter_code)
submit_start = s.index('        const bool submitPressed =')
submit_end = s.index('        if (isOpen && activatedEntry)', submit_start)
submit = s[submit_start:submit_end]
close = block('void EditorNodeGraphUI::CloseNodeBrowser()')
# Pushed-node rollback is unrelated to search; the fixture has no pushed nodes.
close = close[close.index('    m_PushedSourceNodeId = -1;'):]
close = 'void CloseNodeBrowser() {\n' + close
fixture = (root / 'tools/tests/node_browser_search_tests.cpp').read_text()
for marker, value in {
    '// PRODUCTION_CONTAINS': block('bool ContainsCaseInsensitive('),
    '// PRODUCTION_ORDERED_SEARCH': ordered,
    '// PRODUCTION_SUBMIT': submit,
    '// PRODUCTION_CLOSE': close,
    '// PRODUCTION_BEFORE_WINDOW': before_window,
    '// PRODUCTION_ESCAPE': block('if (interactiveOverlay && (catalogFocused || operationOpen) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))'),
}.items():
    fixture = fixture.replace(marker + '\n', value + '\n')
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(fixture)
