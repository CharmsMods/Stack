#!/usr/bin/env python3
"""Extract the actual visible-stack folder predicate for the native scope test.

Only the editor-owned permanent-gallery flag/filter are supplied by the test;
production predicate and GalleryFolderTree containment run unchanged.
"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--stacks', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.stacks or root / 'src/Editor/Internal/RawLab/EditorModuleRawLabGalleryStacks.cpp').read_text()
    start = source.index('    const auto folderVisible =')
    end = source.index('    auto& views =', start)
    predicate = source[start:end]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('''// Generated from the production visible-stack resolver; do not edit.
#pragma once
#include "Editor/Internal/RawLab/RawLabGalleryFolderTree.h"
#include <optional>
#include <string>
inline bool ProductionStackFolderVisible(bool m_PermanentGalleryWorkspace,
    const std::optional<std::string>& m_RawGalleryFolderFilter, const std::string& candidate) {
''' + predicate + '''    return folderVisible(candidate);
}
''')


if __name__ == '__main__':
    main()
