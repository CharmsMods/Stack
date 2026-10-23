"""Optional SEA-RAFT motion proposer. RAW reconstruction stays in StackBurst.

No network access or model downloads. One bounded job, then process exit.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parent
PROFILE = 'sea-raft-m-cb8cfbf14c5e-4iters-fp32-rgb-area-p95-v1'
MODEL_SHA = 'cb8cfbf14c5e0f6734b64add383708b7ff68cc6089a0007c67165d4761346102'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def execute(job_path):
    os.environ['HF_HUB_OFFLINE'] = '1'
    os.environ['HF_HUB_DISABLE_TELEMETRY'] = '1'
    sys.path[:0] = [str(ROOT / 'packages'), str(ROOT / 'core')]
    import numpy as np
    import torch
    from safetensors.torch import load_file
    from raft import RAFT

    job = json.loads(job_path.read_text(encoding='utf-8'))
    if job['version'] != 1 or job['profile'] != PROFILE or job['modelSha256'] != MODEL_SHA:
        raise ValueError('Unsupported model/preprocessing contract')
    w, h = job['width'], job['height']
    if min(w, h) < 64 or max(w, h) > 1152 or w*h > 884736:
        raise ValueError('AI proxy exceeds the bounded geometry')
    checkpoint = ROOT / 'model.safetensors'
    if sha(checkpoint) != MODEL_SHA:
        raise ValueError('SEA-RAFT checkpoint checksum does not match')
    config = argparse.Namespace(**json.loads((ROOT / 'config.json').read_text()))
    if config.iters != 4:
        raise ValueError('Unexpected model iteration count')
    torch.manual_seed(0)
    torch.set_num_threads(4)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.backends.cudnn.deterministic = True
    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    model = RAFT(config)
    weights = load_file(str(checkpoint), device='cpu')
    # Safetensors omits duplicate BN tensor aliases. Restore only aliases
    # demonstrated by this model's own storage, then demand a strict load.
    aliases = {}
    for key, tensor in model.state_dict().items():
        identity = (tensor.untyped_storage().data_ptr(), tensor.storage_offset(),
                    tuple(tensor.shape), tuple(tensor.stride()), tensor.dtype)
        aliases.setdefault(identity, []).append(key)
    for keys in aliases.values():
        present = [key for key in keys if key in weights]
        if present:
            for key in present[1:]:
                if not torch.equal(weights[key], weights[present[0]]):
                    raise ValueError('Conflicting checkpoint aliases')
            for key in keys:
                if key not in weights:
                    weights[key] = weights[present[0]]
    model.load_state_dict(weights, strict=True)
    del weights
    model = model.eval().to(device)
    directory = job_path.parent

    def proxy(source):
        name = Path(source['proxy'])
        if name.name != str(name):
            raise ValueError('Proxy must belong to this job')
        path = directory / name
        if path.stat().st_size != w*h*12:
            raise ValueError('Incomplete AI proxy')
        rgb = np.fromfile(path, dtype='<f4').reshape(h, w, 3)
        if not np.isfinite(rgb).all():
            raise ValueError('Nonfinite AI proxy')
        return rgb

    sources = job['sources']
    reference = next(s for s in sources if s['frameId'] == job['reference'])
    reference_rgb = proxy(reference)
    # A common exposure lift, derived once from the fixed reference, changes
    # network guides only. Sensor samples and final brightness are untouched.
    percentile = float(np.percentile(np.max(np.maximum(reference_rgb, 0), axis=2), 95))
    gain = min(32., max(1., .65 / max(1e-6, percentile)))

    def tensor(rgb):
        rgb = np.clip(rgb * np.float32(gain), 0, 1)
        rgb = np.where(rgb <= .0031308, 12.92*rgb, 1.055*rgb**(1/2.4)-.055)
        return torch.from_numpy(np.ascontiguousarray(rgb*255, dtype=np.float32)).permute(2, 0, 1).unsqueeze(0).to(device)

    reference_tensor = tensor(reference_rgb)
    del reference_rgb
    proposals = []
    inference_seconds = 0.
    if device == 'cuda':
        torch.cuda.reset_peak_memory_stats()
    for index, source in enumerate(sources):
        if source['frameId'] == reference['frameId']:
            continue
        alternate = tensor(proxy(source))
        fields = []
        for label, a, b in [('forward', reference_tensor, alternate), ('reverse', alternate, reference_tensor)]:
            started = time.perf_counter()
            with torch.inference_mode():
                output = model(a, b, iters=4, test_mode=True)
            flow = output['final'][0].permute(1, 2, 0).float().cpu().numpy()
            inference_seconds += time.perf_counter() - started
            if flow.shape != (h, w, 2) or not np.isfinite(flow).all():
                raise ValueError('Model returned invalid motion')
            path = directory / f'pair-{index}-{label}.flow'
            flow.astype('<f4').tofile(path)
            fields.append((path.name, sha(path)))
            del flow, output
        del alternate
        proposals.append(dict(referenceFrameId=reference['frameId'], sourceFrameId=source['frameId'],
            referenceSha256=reference['sha256'], sourceSha256=source['sha256'],
            forwardFile=fields[0][0], forwardSha256=fields[0][1],
            reverseFile=fields[1][0], reverseSha256=fields[1][1]))
    return dict(success=True, profile=PROFILE, modelSha256=MODEL_SHA, device=device,
                commonGain=gain, torchVersion=torch.__version__, proposals=proposals,
                inferenceSeconds=inference_seconds, calls=2*len(proposals),
                peakAllocatedMiB=torch.cuda.max_memory_allocated()/1024**2 if device=='cuda' else None)


if __name__ == '__main__':
    job_path = Path(sys.argv[1]).resolve()
    try:
        response = execute(job_path)
    except Exception as error:
        response = dict(success=False, error=str(error)[:4096])
    destination = job_path.parent / 'result.json'
    temporary = destination.with_suffix('.tmp')
    temporary.write_text(json.dumps(response), encoding='utf-8')
    temporary.replace(destination)
    sys.exit(0 if response['success'] else 1)
