#!/usr/bin/env python3
"""Test the production Color Warp deletion gate with real sibling ImGui controls.

Only the shortcut predicate is extracted, not the renderer or recipe mutation.
--surface accepts an older source file for the regression's negative control.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--surface', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--imgui', type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.surface or root/'src/Editor/Internal/RawLab/RawLabColorSurface.cpp').read_text()
    key = source.index('ImGui::IsKeyPressed(ImGuiKey_Delete, false)')
    start = source.rfind('    if (', 0, key) + len('    if (')
    end = source.index(') {', key)
    if start < len('    if (') or source[end + 3:].lstrip().splitlines()[0] != 'deleteSelected = true;':
        raise ValueError('Color Warp shortcut extraction anchor changed')
    condition = source[start:end].replace('args.ui.selectedColorWarpPin', 'selected')
    generated = '#include <imgui.h>\nbool DeleteShortcut(int selected) {\n const auto& io = ImGui::GetIO();\n return ' + condition + ';\n}\n'
    generated += (root/'tools/tests/color_warp_shortcut_tests.cpp').read_text()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    if not args.imgui:
        parser.error('--imgui required for standalone compilation (or use --output)')
    with tempfile.TemporaryDirectory(prefix='stack-color-shortcut-') as temp:
        cpp, exe = Path(temp)/'test.cpp', Path(temp)/'test'
        cpp.write_text(generated)
        subprocess.run([args.cxx, '-std=c++17', '-O2', '-DNDEBUG', '-I', str(args.imgui), str(cpp),
            *[str(args.imgui/name) for name in ['imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp']],
            '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
