"""Extract the browser's actual table setup, card item and clipper stride."""
from pathlib import Path
import argparse

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = (root / 'src/Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp').read_text()
card_start = source.index('    constexpr float kCardWidth =', source.index('bool RenderBrowserCard('))
card = source[card_start:source.index('    const bool activated =', card_start)]
table_start = source.index('        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,')
table = source[table_start:source.index('            ImGuiListClipper clipper;', table_start)]
# Exercise additional border flags without changing the production default.
table = table.replace('ImGuiTableFlags_SizingFixedFit |', '(borders ? ImGuiTableFlags_Borders : ImGuiTableFlags_None) | ImGuiTableFlags_SizingFixedFit |')
stride = next(line.strip() for line in source.splitlines() if 'clipper.Begin(rowCount,' in line)
fixture = (root / 'tools/tests/node_browser_layout_tests.cpp').read_text()
for marker, text in {'// PRODUCTION_CARD': card, '// PRODUCTION_TABLE': table,
                     '// PRODUCTION_STRIDE': stride}.items():
    fixture = fixture.replace(marker, text)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(fixture)
