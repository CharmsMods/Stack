#!/usr/bin/env python3
"""Real ImGui opacity lifecycle with the production widget/gesture block.

Layer commits use a narrow probe, not the real document/history model.
--surface accepts an older production file for the negative control.
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
    source = (args.surface or root/'src/Editor/Internal/RawLab/EditorModuleRawLayerPanel.cpp').read_text()
    start = source.index('    const bool opacityChanged = ImGui::SliderFloat("Opacity"')
    end = source.index('    ImGui::EndDisabled();', start)
    block = source[start:end]
    if 'm_Project->rawLayers.BeginGesture();' not in block or 'm_Project->rawLayers.EndGesture();' not in block:
        raise ValueError('Opacity gesture extraction anchors changed')
    generated = (root/'tools/tests/layer_opacity_gesture_tests.cpp').read_text()
    generated += '\nvoid Fixture::Draw() {\n auto* selected = &project.rawLayers.layer;\n float opacity = selected->opacity * 100;\n ImGui::SetNextItemWidth(300);\n' + block + '}\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    if not args.imgui:
        parser.error('--imgui required for standalone compilation (or use --output)')
    with tempfile.TemporaryDirectory(prefix='stack-opacity-gesture-') as temp:
        cpp, exe = Path(temp)/'test.cpp', Path(temp)/'test'
        cpp.write_text(generated)
        subprocess.run([args.cxx, '-std=c++17', '-O2', '-DNDEBUG', '-I', str(args.imgui), str(cpp),
            *[str(args.imgui/name) for name in ['imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp']],
            '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
