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

list_start = source.index('bool GalleryListSelectable(')
list_helper = source[list_start:source.index('\n}', list_start) + 2]
assert 'GalleryListSelectable(row, listProjectSelected)' in source
assert 'GalleryListSelectable(row, selected)' in source

def scope(expression):
    return next(line.strip() for line in source.splitlines()
                if line.strip() == f'PushGalleryKeyID({expression});')

# Keep the permanent-stack inner identity in sync with drawThumbnail.
inner = 'ImGui::PushID("RawGalleryTile");\n        ImGui::PushID(itemId);'
assert inner in source
# Source-only cost guard: cards must not own unused metadata before row culling.
item = source[source.index('    struct AnimatedGalleryItem {'):source.index('    const auto gridStackSource =')]
assert 'GallerySourceView' not in item, 'animated cards copy unused gallery metadata'
thumbnail = source[source.index('    auto drawThumbnail ='):source.index('    const auto sourceMultiSelected =')]
assert 'const Stack::RawWorkspace::GallerySourceView&' not in thumbnail, 'thumbnail has unused metadata argument'
# Non-stack timelines append source pointers directly; only a stack rebuild needs the map.
timeline_setup = source[source.index('    const bool stackFilmstrip ='):source.index('    const auto addTimelineSourceTime =')]
assert 'timelineSourcesByKey.' not in timeline_setup, 'non-stack frames build an unused timeline source map'
stack_rebuild_start = source.index('            if (!m_RawWorkspaceLabFilmstripStackTimelineActive) {')
stack_rebuild = source[stack_rebuild_start:source.index('            centerFilmstripItems(filmstripStacks.size());', stack_rebuild_start)]
for required in ('timelineSourcesByKey.reserve(', 'timelineSourcesByKey.emplace(',
                 'timelineSourcesByKey.insert_or_assign(', 'appendFilmstripTimelineEntry('):
    assert required in stack_rebuild, 'stack timeline still needs its source map and pinned override'
fit_start = source.index('            const auto fitGalleryText =')
fit_text = source[fit_start:source.index('            const std::string ordinalPrefix', fit_start)]
fixture = (root / 'tools/tests/gallery_widget_identity_tests.cpp').read_text()
for marker, body in {
    '// PRODUCTION_KEY_HELPER': helper,
    '// PRODUCTION_LIST_HELPER': list_helper,
    '// PRODUCTION_FIT_TEXT': fit_text,
    '// PRODUCTION_STACK_SCOPE': scope('item.animationKey'),
    '// PRODUCTION_SOURCE_SCOPE': scope('source->relativePathKey'),
    '// PRODUCTION_GROUP_SCOPE': scope('groupKey'),
    '// PRODUCTION_THUMBNAIL_SCOPE': inner,
}.items():
    fixture = fixture.replace(marker, body)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(fixture)
