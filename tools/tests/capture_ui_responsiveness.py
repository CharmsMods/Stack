"""Bounded native application + PresentMon capture. Does not automate desktop UI."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import ctypes

p = argparse.ArgumentParser()
p.add_argument('--exe', type=Path, required=True)
p.add_argument('--input', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--presentmon', type=Path)
p.add_argument('--workload', choices=('idle', 'loading', 'processing', 'combined'), default='idle')
p.add_argument('--scenario', default='gallery')
p.add_argument('--hidden', action='store_true')
a = p.parse_args()
exe, source, out = a.exe.resolve(), a.input.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
command = [str(exe), '--diagnose-ui-performance', str(source), str(out), a.scenario]
if not a.hidden:
    command.append('--visible')
if a.scenario in ('gallery', 'gallery_resize'):
    command += ['--workload', a.workload]
metadata = {'command': command, 'binary_sha256': hashlib.file_digest(exe.open('rb'), 'sha256').hexdigest(),
            'started_unix': time.time(), 'input': str(source), 'visible': not a.hidden}
frequency = ctypes.c_longlong()
ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(frequency))
metadata['qpc_frequency'] = frequency.value
if source.is_dir():
    metadata['input_files'] = [{'name': f.name, 'bytes': f.stat().st_size, 'mtime_ns': f.stat().st_mtime_ns}
                               for f in sorted(source.iterdir()) if f.is_file()]
trace = None
with (out/'application.log').open('wb') as log, (out/'presentmon.log').open('wb') as trace_log:
    app = subprocess.Popen(command, cwd=exe.parent, stdout=log, stderr=subprocess.STDOUT)
    metadata['pid'] = app.pid
    try:
        if a.presentmon and not a.hidden:
            trace_command = [str(a.presentmon.resolve()), '--process_id', str(app.pid), '--output_file',
                             str(out/'display.csv'), '--qpc_time', '--no_console_stats', '--timed', '245',
                             '--terminate_after_timed', '--terminate_on_proc_exit',
                             '--session_name', f'StackUi-{app.pid}']
            metadata['presentmon_command'] = trace_command
            trace = subprocess.Popen(trace_command, stdout=trace_log, stderr=subprocess.STDOUT,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
        metadata['exit_code'] = app.wait(timeout=270)
    except subprocess.TimeoutExpired:
        metadata['deadline_exceeded'] = True
        app.terminate()
        metadata['exit_code'] = app.wait(timeout=15)
    finally:
        if trace:
            try:
                metadata['presentmon_exit_code'] = trace.wait(timeout=12)
            except subprocess.TimeoutExpired:
                trace.terminate()
                metadata['presentmon_exit_code'] = trace.wait(timeout=10)
        metadata['finished_unix'] = time.time()
        (out/'provenance.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
print(json.dumps({'output': str(out), 'exit_code': metadata['exit_code'],
                  'display_csv': (out/'display.csv').exists()}))
raise SystemExit(metadata['exit_code'])
