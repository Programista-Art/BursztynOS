#!/usr/bin/env python3
"""A/B the intermittent virtio-gpu control-command timeout between builds.

Each round boots every variant *concurrently* (same host load for all) with
narzedzia/bsf2_native_qemu.py --workload apps (identical Settings/Notepad/
Shell sequence, no BSF 2.0-specific checks), UEFI + virtio-vga, and records
whether the guest logged `Timeout polecenia kontrolnego` and at which stage.

Usage:
  virtio_gpu_ab_qemu.py --rounds 10 --variant base=<iso>:<build-tree> \
      --variant bsf2=<iso>:<build-tree> [--resolution 1280x720] --out DIR
Writes DIR/summary.json and per-run JSON/serial logs.
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--variant', action='append', required=True, help='name=ISO:BUILD_TREE')
    ap.add_argument('--rounds', type=int, default=10)
    ap.add_argument('--resolution', default='1280x720')
    ap.add_argument('--workload', default='apps', choices=('apps', 'bsf2'))
    ap.add_argument('--out', type=Path, default=ROOT / '.bsf-test/virtio-gpu-ab')
    ap.add_argument('--hmp-stress', action='store_true')
    ap.add_argument('--cpu-burners', type=int, default=0,
                    help='busy-loop host processes running during every round (defined host load)')
    a = ap.parse_args()
    variants = []
    for v in a.variant:
        name, rest = v.split('=', 1)
        iso, tree = rest.split(':', 1)
        variants.append((name, Path(iso).resolve(), Path(tree).resolve()))
    a.out.mkdir(parents=True, exist_ok=True)
    summary_path = a.out / 'summary.json'
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {'runs': []}
    summary['config'] = {'resolution': a.resolution, 'workload': a.workload, 'hmp_stress': a.hmp_stress, 'cpu_burners': a.cpu_burners,
                         'host_cpus': __import__('os').cpu_count(),
                         'variants': {n: {'iso': str(i), 'tree': str(t),
                                          'sha256': subprocess.check_output(['sha256sum', str(i)], text=True).split()[0]}
                                      for n, i, t in variants},
                         'qemu': subprocess.check_output(['qemu-system-x86_64', '--version'], text=True).splitlines()[0]}
    start_round = 1 + max((r['round'] for r in summary['runs']), default=0)
    for rnd in range(start_round, start_round + a.rounds):
        burners = [subprocess.Popen([sys.executable, '-c', 'while True: pass'])
                   for _ in range(a.cpu_burners)]
        procs = []
        for name, iso, tree in variants:
            tag = f'{name}-r{rnd:02d}'
            cmd = [sys.executable, str(ROOT / 'narzedzia/bsf2_native_qemu.py'), '--iso', str(iso),
                   '--root', str(tree), '--workload', a.workload, '--resolution', a.resolution,
                   '--out', str(a.out), '--tag', tag, '--timeout', '1500'] + \
                  (['--hmp-stress'] if a.hmp_stress else [])
            procs.append((name, tag, time.monotonic(), subprocess.Popen(
                cmd, stdout=(a.out / f'{tag}.log').open('w'), stderr=subprocess.STDOUT)))
        ended = {}
        for name, tag, t0, p in procs:
            p.wait()
            ended[tag] = time.monotonic()
        for b in burners:
            b.kill()
            b.wait()
        for name, tag, t0, p in procs:
            res_file = a.out / f'uefi-{a.resolution}-{tag}' / 'bsf2-native.json'
            res = json.loads(res_file.read_text()) if res_file.exists() else {}
            run = {'round': rnd, 'variant': name, 'ok': bool(res.get('ok')),
                   'gpu_timeout': bool(res.get('gpu_timeout')), 'stage': res.get('stage'),
                   'seconds': round(ended[tag] - t0), 'error': (res.get('error') or '')[:200]}
            summary['runs'].append(run)
            print(json.dumps(run), flush=True)
        totals = {}
        for r in summary['runs']:
            t = totals.setdefault(r['variant'], {'runs': 0, 'pass': 0, 'gpu_timeout': 0, 'other_fail': 0})
            t['runs'] += 1
            t['pass'] += r['ok']
            t['gpu_timeout'] += r['gpu_timeout']
            t['other_fail'] += (not r['ok']) and not r['gpu_timeout']
        summary['totals'] = totals
        summary_path.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary['totals'], indent=2))


if __name__ == '__main__':
    main()
