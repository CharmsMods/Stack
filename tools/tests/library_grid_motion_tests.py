"""Compile the actual Library target-card motion loop and an owned-key oracle."""
from pathlib import Path
import argparse
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--source', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
s = (a.source or root / 'src/Library/LibraryModuleGrid.cpp').read_text()
start = s.index('    for (const LibraryCachedPackedCard& targetCard : m_CachedPackedCards) {')
end = s.index('    // Cards rejected by the current query', start)
loop = s[start:end]
# The prior iteration-local owned string is a semantic oracle, not a cache.
reference = loop.replace('const std::string* keyPointer = nullptr;', 'std::string key;').replace(
    'keyPointer = &assets[targetCard.index]->fileName;', 'key = assets[targetCard.index]->fileName;').replace(
    'keyPointer = &projects[targetCard.index]->fileName;', 'key = projects[targetCard.index]->fileName;').replace(
    '        const std::string& key = *keyPointer;\n', '')
def function(path, marker):
    source = (root / path).read_text()
    start = source.index(marker)
    return source[start:source.index('\n}', start)+2]
fixture = (root / 'tools/tests/library_grid_motion_tests.cpp').read_text()
for marker, body in {
    '// PRODUCTION_LOOP': loop,
    '// OWNED_KEY_LOOP': reference,
    '// PRODUCTION_SIZE': function('src/Library/Internal/LibraryModuleUIHelpers.cpp', 'ImVec2 ComputeLibraryCardSize('),
    '// PRODUCTION_ANIMATE': function('src/Utils/ImGuiExtras.cpp', 'float AnimateTowards('),
}.items():
    fixture = fixture.replace(marker, body)
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(fixture)
