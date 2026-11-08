"""Summarize every sample, including cold phases, failures and display gaps."""
import argparse
import csv
import json
import math
from bisect import bisect_left, bisect_right
from collections import defaultdict
from pathlib import Path

def stats(values):
    values = sorted(v for v in values if math.isfinite(v) and v >= 0)
    if not values:
        return {'count': 0}
    return {'count': len(values), **{name: values[min(len(values)-1, math.ceil(p*len(values))-1)]
            for name, p in [('median', .5), ('p95', .95), ('p99', .99), ('max', 1)]}}

def read_rows(path):
    if not path.exists():
        return []
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))

def summarize(folder):
    env = {}
    if (folder/'environment.txt').exists():
        env = dict(line.split('=', 1) for line in (folder/'environment.txt').read_text().splitlines() if '=' in line)
    hz = float(env.get('refreshHz', '60'))
    budget = 1000/max(1, hz)
    frames = read_rows(folder/'frames.csv')
    grouped = defaultdict(list)
    previous_by_qpc = {}
    previous = {}
    for row in frames:
        grouped[row['scenario']].append(row)
        previous_by_qpc[row['frame_qpc']] = previous
        previous = row
    result = {'environment': env, 'budget_ms': budget, 'scenarios': {},
              'status': (folder/'status.txt').read_text() if (folder/'status.txt').exists() else 'missing'}
    for scenario, rows in grouped.items():
        metrics = {}
        metrics['frames'] = len(rows)
        for key in rows[0]:
            if key.endswith('_ms'):
                metrics[key] = stats([float(r[key]) for r in rows if r.get(key)])
        metrics['over_1_5_budget'] = sum(float(r.get('interval_ms', 0)) > budget*1.5 for r in rows)
        metrics['software_intervals_within_1_5_budget_percent'] = 100*(1-metrics['over_1_5_budget']/len(rows))
        metrics['gaps_above_50ms'] = []
        for row in rows:
            if float(row.get('interval_ms', 0)) <= 50: continue
            prior = previous_by_qpc[row['frame_qpc']]
            metrics['gaps_above_50ms'].append({'frame_qpc': row['frame_qpc'], 'interval_ms': float(row['interval_ms']),
                'preceding_frame_scenario': prior.get('scenario'),
                'preceding_frame_stages_ms': {k: float(v) for k, v in prior.items() if k.endswith('_ms')}})
        metrics['workload_overlap_frames'] = {key: sum(r.get(key) == '1' for r in rows)
            for key in ('scan_busy', 'thumbnails_busy', 'processing_busy', 'moving')}
        result['scenarios'][scenario] = metrics
    result['actual_load'] = {}
    for label, predicate in {
        'settled': lambda r: not any(r.get(k) == '1' for k in ('scan_busy', 'thumbnails_busy', 'processing_busy')),
        'loading': lambda r: any(r.get(k) == '1' for k in ('scan_busy', 'thumbnails_busy')),
        'processing': lambda r: r.get('processing_busy') == '1',
        'combined': lambda r: r.get('processing_busy') == '1' and any(r.get(k) == '1' for k in ('scan_busy', 'thumbnails_busy')),
    }.items():
        rows = [r for r in frames if r['scenario'] != 'gallery_startup' and predicate(r)]
        result['actual_load'][label] = {'interval_ms': stats([float(r['interval_ms']) for r in rows]),
            'gallery_ms': stats([float(r['gallery_ms']) for r in rows]),
            'gpu_draw_ms': stats([float(r['gpu_draw_ms']) for r in rows]),
            'main_callback_max_ms': stats([float(r['mainCallbackMax_ms']) for r in rows]),
            'software_intervals_within_1_5_budget_percent':
                100*sum(float(r['interval_ms']) <= budget*1.5 for r in rows)/len(rows) if rows else None,
            'gaps_above_50ms': sum(float(r['interval_ms']) > 50 for r in rows)}
    interactions = read_rows(folder/'interactions.csv')
    grouped = defaultdict(list)
    for row in interactions:
        grouped[(row['scenario'], row['action'], row['dispatch'])].append(row)
    result['interactions'] = {'/'.join(key): {
        'attempts': len(rows),
        'changed': sum(float(r['response_ms']) >= 0 for r in rows),
        'no_changed_frame': sum(float(r['submitted_ms']) < 0 for r in rows),
        'submitted_over_2_budget': sum(float(r['submitted_ms']) > 2*budget for r in rows),
        'responses_over_2_budget': sum(float(r['response_ms']) > 2*budget for r in rows),
        **{metric: stats([float(r[metric]) for r in rows if r.get(metric)])
           for metric in ('response_ms', 'accepted_ms', 'geometry_ms', 'settled_ms', 'submitted_ms')},
        'statuses': {status: sum(r['status'] == status for r in rows) for status in set(r['status'] for r in rows)}}
        for key, rows in grouped.items()}
    displayed = read_rows(folder/'display.csv')
    # This is an independent presentation distribution, not hardware input latency.
    result['display'] = {'available': bool(displayed)}
    if not displayed:
        result['display']['reason'] = (folder/'presentmon.log').read_text(errors='replace') if (folder/'presentmon.log').exists() else 'No display trace supplied'
    if displayed:
        result['display']['present_modes'] = sorted(set(r.get('PresentMode', '') for r in displayed))
        for key in ('MsBetweenDisplayChange', 'DisplayedTime', 'MsBetweenPresents', 'DisplayLatency'):
            vals = []
            for row in displayed:
                try: vals.append(float(row[key]))
                except (KeyError, ValueError): pass
            result['display'][key] = stats(vals)
    jobs = read_rows(folder/'processing.csv')
    completed = [r for r in jobs if r['status'] == '0']
    result['processing'] = {'jobs': jobs, 'completed': len(completed),
        'completed_seconds': stats([float(r['seconds']) for r in completed])}
    motion = read_rows(folder/'filmstrip.csv')
    motion_times = [int(r['qpc']) for r in motion]
    result['filmstrip_boundary_inputs'] = []
    for event in interactions:
        if event['action'] != 'wheel' or 'filmstrip_' not in event['scenario']:
            continue
        index = bisect_left(motion_times, int(event['input_qpc']))
        if not 0 < index < len(motion):
            continue
        before, after = motion[index-1], motion[index]
        position, maximum, direction = float(after['position']), float(after['maximum']), int(after['direction'])
        at_end = direction > 0 and abs(position-maximum) < .01 or direction < 0 and position < .01
        if (at_end and after['active'] == '0' and before['maximum'] == after['maximum']
                and abs(position-float(before['position'])) < .01):
            # Preserve the raw probe result. A later background reframe must not
            # be misreported as latency for input accepted at an existing bound.
            result['filmstrip_boundary_inputs'].append({**event, 'position': position, 'maximum': maximum})
    frame_times = [int(r['frame_qpc']) for r in frames]
    result['filmstrip_motion'] = {'available': bool(motion), 'samples': len(motion),
                                  'checked_deltas': 0, 'opposite_deltas': [], 'native_opposite_deltas': []}
    previous = None
    for row in motion:
        index = bisect_right(frame_times, int(row['qpc']))-1
        scenario = frames[index]['scenario'] if index >= 0 else ''
        row['scenario'] = scenario
        # Compare uninterrupted wheel phases with unchanged geometry only.
        # Explicit preview navigation and scrollbar dragging have their own owners.
        if previous and scenario == previous['scenario'] and any(
                phase in scenario for phase in ('filmstrip_fractional', 'filmstrip_fast', 'filmstrip_reverse')):
            direction = int(row['direction'])
            if direction and direction == int(previous['direction']) and row['maximum'] == previous['maximum']:
                result['filmstrip_motion']['checked_deltas'] += 1
                delta = float(row['position'])-float(previous['position'])
                if delta*direction < -.01:
                    result['filmstrip_motion']['opposite_deltas'].append(
                        {'qpc': row['qpc'], 'scenario': scenario, 'delta': delta, 'direction': direction})
                native_delta = float(row['native_position'])-float(previous['native_position'])
                if native_delta*direction < -1.1:
                    result['filmstrip_motion']['native_opposite_deltas'].append(
                        {'qpc': row['qpc'], 'scenario': scenario, 'delta': native_delta, 'direction': direction})
        previous = row
    return result

p = argparse.ArgumentParser()
p.add_argument('folders', nargs='+', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps({str(f): summarize(f) for f in a.folders}, indent=2), encoding='utf-8')
print(a.output)
