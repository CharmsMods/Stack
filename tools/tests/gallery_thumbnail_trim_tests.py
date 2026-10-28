#!/usr/bin/env python3
"""Exercise the complete production trim method and thumbnail record declaration.
Only enum declarations, ImGui frame clock, layout protection and delete queue are
boundary doubles; no decoder, texture allocation or GL context is involved.
"""
import argparse
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
s = (root / 'src/Editor/Internal/RawWorkspace/EditorModuleRawWorkspaceThumbnails.cpp').read_text()
h = (root / 'src/Editor/EditorModule.h').read_text()
record = h[h.index('    struct RawWorkspaceThumbnailTexture {'):h.index('    struct RawWorkspaceRecipePreviewCacheEntry')]
constants = s[s.index('constexpr std::size_t kRawWorkspaceThumbnailTextureMaxResidentCount'):s.index('\n} // namespace')]
method = s[s.index('void EditorModule::TrimRawWorkspaceThumbnailTextureCache()'):s.index('\nvoid EditorModule::ClearRawWorkspaceThumbnailTextures')]
fixture = (root / 'tools/tests/gallery_thumbnail_trim_tests.cpp').read_text()
for marker, content in [('RECORD', record), ('CONSTANTS', constants), ('METHOD', method)]:
    fixture = fixture.replace('// PRODUCTION_' + marker, content)
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(fixture)
