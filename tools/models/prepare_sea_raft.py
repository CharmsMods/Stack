"""Prepare the optional, pinned SEA-RAFT package beside StackBurst.exe.

Use an existing source pack with --source-pack, or download the pinned public
artifacts. Python dependencies are supplied explicitly, never installed globally.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import urllib.request

HERE = Path(__file__).resolve().parent
PROFILE = 'sea-raft-m-cb8cfbf14c5e-4iters-fp32-rgb-area-p95-v1'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--deployment', type=Path, required=True, help='Directory containing StackBurst.exe')
    parser.add_argument('--python', type=Path, required=True, help='Python with torch, numpy and safetensors')
    parser.add_argument('--source-pack', type=Path)
    parser.add_argument('--packages', type=Path, help='Optional existing safetensors wheel extraction')
    options = parser.parse_args()
    root = options.deployment.resolve() / 'Stack/Models/SeaRaft'
    root.mkdir(parents=True, exist_ok=True)
    pins = json.loads((HERE / 'sea_raft/artifacts.json').read_text())
    for artifact in pins['files']:
        target = root / artifact['target']
        target.parent.mkdir(parents=True, exist_ok=True)
        if options.source_pack:
            contents = (options.source_pack / artifact['source']).read_bytes()
        else:
            with urllib.request.urlopen(artifact['url'], timeout=120) as response:
                contents = response.read()
        if hashlib.sha256(contents).hexdigest() != artifact['sha256']:
            raise ValueError('Artifact checksum differs: ' + artifact['source'])
        target.write_bytes(contents)
    # These inference-only changes are the same ones used in the research:
    # remove online Hub loading and overwritten ResNet initialization, and
    # defer an unused training helper's scipy import. No network layer changes.
    raft = root / 'core/raft.py'
    content = raft.read_text()
    assert content.count('init_weight=True') == 2
    content = content.replace('init_weight=True', 'init_weight=False')
    content = content.replace('from huggingface_hub import PyTorchModelHubMixin', '')
    content, count = re.subn(r'class RAFT\(.*?\n\):', 'class RAFT(nn.Module):', content, count=1, flags=re.S)
    assert count == 1
    raft.write_text(content, encoding='utf-8')
    utils = root / 'core/utils/utils.py'
    content = utils.read_text()
    assert content.count('from scipy import interpolate') == 1
    content = content.replace('from scipy import interpolate\n', '')
    content = content.replace('def forward_interpolate(flow):\n', 'def forward_interpolate(flow):\n    from scipy import interpolate\n')
    utils.write_text(content, encoding='utf-8')
    shutil.copy2(HERE / 'sea_raft/worker.py', root / 'worker.py')
    shutil.copy2(HERE / 'sea_raft/NOTICE.md', root / 'NOTICE.md')
    shutil.copy2(HERE / 'sea_raft/artifacts.json', root / 'artifacts.json')
    if options.packages:
        # Copy only the needed package and its distribution metadata/licenses.
        for source in options.packages.glob('safetensors*'):
            if source.is_dir():
                shutil.copytree(source, root / 'packages' / source.name, dirs_exist_ok=True)
    python = options.python.resolve()
    probe = "import sys;sys.path.insert(0,sys.argv[1]);import torch,numpy,safetensors;print(torch.__version__)"
    subprocess.run([str(python), '-I', '-c', probe, str(root / 'packages')], check=True,
                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    try:
        interpreter = python.relative_to(root).as_posix()
    except ValueError:
        interpreter = str(python)
    # Publish capability only after artifacts and interpreter have been checked.
    manifest = root / 'runtime.json'
    temporary = manifest.with_suffix('.tmp')
    temporary.write_text(json.dumps(dict(version=1, profile=PROFILE, python=interpreter), indent=2), encoding='utf-8')
    temporary.replace(manifest)
    print(root)


if __name__ == '__main__':
    main()
