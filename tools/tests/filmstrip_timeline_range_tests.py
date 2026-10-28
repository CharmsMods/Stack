"""Compile the production timeline's exact local range/center lambdas."""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = (ROOT / 'src/Editor/Internal/RawLab/EditorModuleRawLabFilmstripTimeline.cpp').read_text()
start = source.index('    const auto imageCenterX =')
end = source.index('    const auto visibleEntries =', start)
fixture = (ROOT / 'tools/tests/filmstrip_timeline_range_tests.cpp').read_text()
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(fixture.replace('// PRODUCTION_TIMELINE_RANGE', source[start:end]))
