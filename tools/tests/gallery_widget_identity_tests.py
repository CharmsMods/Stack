"""Generate real-ImGui checks using the main gallery's exact key scopes."""
from pathlib import Path
import argparse

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = (root / 'src/Editor/Internal/RawLab/EditorModuleRawLabGallery.cpp').read_text()
start = source.index('void PushGalleryKeyID(')
helper = source[start:source.index('\n}', start) + 2]

def scope(expression):
    return next(line.strip() for line in source.splitlines()
                if line.strip() == f'PushGalleryKeyID({expression});')

# Keep the permanent-stack inner identity in sync with drawThumbnail.
inner = 'ImGui::PushID("RawGalleryTile");\n        ImGui::PushID(itemId);'
assert inner in source
fixture = (root / 'tools/tests/gallery_widget_identity_tests.cpp').read_text()
for marker, body in {
    '// PRODUCTION_KEY_HELPER': helper,
    '// PRODUCTION_STACK_SCOPE': scope('item.animationKey'),
    '// PRODUCTION_SOURCE_SCOPE': scope('source->relativePathKey'),
    '// PRODUCTION_GROUP_SCOPE': scope('groupKey'),
    '// PRODUCTION_THUMBNAIL_SCOPE': inner,
}.items():
    fixture = fixture.replace(marker, body)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(fixture)
