"""Generate real-GL Queue lifetime checks from the owner methods and complete view."""
from pathlib import Path
import argparse
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--source', type=Path)
p.add_argument('--view-source', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
s = (a.source or root / 'src/Queue/QueueModule.cpp').read_text()
def method(marker):
    start = s.index(marker)
    return s[start:s.index('\n}', start) + 2].replace('QueueModule::', 'Owner::')
markers = ['QueueModule::TextureEntry* QueueModule::EnsureTexture(',
           'void QueueModule::ReleaseTexture(', 'const std::vector<Item>& QueueModule::FrameItems(',
           'void QueueModule::Shutdown()']
methods = '\n'.join(method(marker) for marker in markers)
for marker in ['void QueueModule::RetireTexture(', 'void QueueModule::ReleaseRetiredTextures()']:
    if marker in s:
        methods += '\n' + method(marker)
    else:
        # Old-source negative still compiles; its immediate deletion is tested.
        methods += '\nvoid Owner::' + ('RetireTexture(unsigned int) {}' if 'RetireTexture(' in marker else 'ReleaseRetiredTextures() {}')
fixture = (root / 'tools/tests/queue_texture_lifetime_tests.cpp').read_text().replace('// PRODUCTION_METHODS', methods)
if a.view_source:
    fixture = fixture.replace('#include "Queue/QueueView.cpp"', '#include "' + a.view_source.resolve().as_posix() + '"')
# Frame pump must run outside Queue's visibility/consumer branches.
if not a.source:
    shell = (root / 'src/App/AppShell.cpp').read_text()
    assert shell.index('m_Queue.ReleaseRetiredTextures();') < shell.index('m_QueueRenderer.Tick();') < shell.index('RenderNavigationRail(', shell.index('m_QueueRenderer.Tick();'))
    assert s.count('glDeleteTextures') == 3, 'all non-shutdown deletion must use the retirement pump'
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(fixture)
