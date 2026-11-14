#!/usr/bin/env python3
"""Desktop responsiveness while Husarz loads a page (native QEMU/TCG).

Measures, from the host side and from guest telemetry:
  * pointer input->dispatch latency (host sends relative motion, polls the
    guest cursor variable) in three phases: idle, while Husarz loads a page,
    after loading;
  * guest [GUI PERF] / [HUSARZ UI STALL] lines (needs an ISO built with
    BURSZTYNDIAGGUIRUNTIME=1 HUSARZ_PERF=1 ...): frame interval, input->present;
  * time to HTTP request / until the page is quiet.

The result is written to <out>/responsiveness.json. Never reports success from
a boot alone: every number comes from probes during the scenario.
"""
import argparse, json, re, statistics, sys, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
from desktop_qemu_lib import Guest  # noqa: E402


class SerialClock:
    """Maps serial log offsets to host time (log lines carry no timestamps)."""

    def __init__(self, path):
        self.path, self.samples, self.stop = path, [], threading.Event()
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        while not self.stop.is_set():
            try:
                self.samples.append((time.monotonic(), self.path.stat().st_size))
            except OSError:
                pass
            time.sleep(0.05)

    def when(self, offset):
        for t, size in self.samples:
            if size >= offset:
                return t
        return None


def pointer_probe(g, count, step=3, label=''):
    """Latency (ms) from sending a relative move to the guest cursor moving."""
    lat = []
    # Park the pointer mid-screen so +/-step never clamps at an edge (a clamped
    # move leaves the cursor variable unchanged and would read as a stall).
    x, y = g.cursor()
    if not (100 < x < g.w - 100 and 100 < y < g.h - 100):
        g.move(g.w // 2, g.h // 2)
    direction = 1
    for _ in range(count):
        old = g.cursor()
        t0 = time.monotonic()
        g.hmp(f'mouse_move {step * direction} 0')
        direction = -direction
        deadline = t0 + 20
        while time.monotonic() < deadline:
            if g.cursor() != old:
                break
        else:
            lat.append(20000.0)
            continue
        lat.append((time.monotonic() - t0) * 1000)
    return lat


def quiet_motion(g, seconds, period=0.05, step=3):
    """Gentle pointer motion through QMP only (no guest memory reads), so the
    host does not load QEMU's main loop while the guest telemetry is sampled."""
    end = time.monotonic() + seconds
    sign = 1
    n = 0
    while time.monotonic() < end:
        g.q.call('input-send-event', {'events': [{'type': 'rel', 'data': {'axis': 'x', 'value': step * sign}}]})
        sign = -sign
        n += 1
        time.sleep(period)
    return n


def guest_window(text_slice):
    """Aggregate guest-side metrics (PID 0 compositor lines) over a serial slice."""
    rows = []
    for l in text_slice.splitlines():
        if '[GUI PERF]' in l and 'pid=0' in l:
            rows.append({k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', l)})
    def col(k):
        return sorted(r.get(k, 0) for r in rows)
    def pct(v, q):
        return v[min(len(v) - 1, int(len(v) * q))] if v else 0
    ip, di, dr, fi = col('input_present_max_ns'), col('deferred_ticks'), col('deferred_run_max_ns'), col('frame_interval_max_ns')
    return {'seconds': len(rows),
            'input_present_max_ms': {'p50': round(pct(ip, .5) / 1e6, 1), 'p90': round(pct(ip, .9) / 1e6, 1), 'max': round((ip[-1] if ip else 0) / 1e6, 1)},
            'deferred_ticks_total': sum(di),
            'deferred_run_max_ms': {'p50': round(pct(dr, .5) / 1e6, 1), 'p90': round(pct(dr, .9) / 1e6, 1), 'max': round((dr[-1] if dr else 0) / 1e6, 1)},
            'gpu_wait_s': round(sum(r.get('gpu_ns', 0) for r in rows) / 1e9, 3),
            'gpu_max_ms': round(max([r.get('gpu_max_ns', 0) for r in rows] or [0]) / 1e6, 1),
            'frames': sum(r.get('frame_present', 0) for r in rows),
            'torn_compose': sum(r.get('torn_compose', 0) for r in rows),
            'shadow_compose': sum(r.get('shadow_compose', 0) for r in rows)}


def summarize(lat):
    if not lat:
        return {}
    s = sorted(lat)
    return {'n': len(s), 'median_ms': round(statistics.median(s), 1), 'p90_ms': round(s[int(len(s) * .9) - 1], 1),
            'max_ms': round(s[-1], 1), 'mean_ms': round(statistics.mean(s), 1)}


def gui_perf(lines):
    out = []
    for l in lines:
        if '[GUI PERF]' in l and 'pid=0' in l:
            f = dict(re.findall(r'(\w+)=(\d+)', l))
            out.append({k: int(f[k]) for k in ('frame_present', 'frame_interval_max_ns', 'input_present_max_ns',
                                               'idle_ns', 'cpu_ns', 'lock_wait_max_ns', 'gpu_max_ns') if k in f})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iso', type=Path, default=ROOT / '.bsf-test/instrumented.iso')
    ap.add_argument('--pages', type=Path, required=True)
    ap.add_argument('--page', default='wordpress.html')
    ap.add_argument('--out', type=Path, default=ROOT / '.bsf-test/responsiveness')
    ap.add_argument('--resolution', default='1280x720')
    ap.add_argument('--probes', type=int, default=40)
    ap.add_argument('--timeout', type=int, default=1800)
    ap.add_argument('--bios', action='store_true')
    ap.add_argument('--build', type=Path, default=ROOT, help='dir with the matching system_operacyjny.bin and *.elf')
    ap.add_argument('--load-seconds', type=int, default=60)
    ap.add_argument('--https', action='store_true', help='load the page over the local TLS fixture (10.0.2.100:443)')
    a = ap.parse_args()
    g = Guest(a.iso, a.out, a.build, a.bios, a.resolution, a.pages, a.timeout, https=a.https)
    result = {'iso': str(a.iso), 'page': a.page, 'resolution': a.resolution, 'accel': 'tcg'}
    clock = SerialClock(g.serial_path)
    try:
        g.boot_to_desktop()
        result['desktop_seconds'] = g.desktop_seconds
        time.sleep(5)
        mark = lambda: g.serial_path.stat().st_size
        read_from = lambda off: g.serial_path.read_text(errors='replace')[off:]
        off = mark()
        quiet_motion(g, 8)
        result['desktop_idle'] = guest_window(read_from(off))
        g.launch('husarz')
        browser = g.observer('przegladarka.elf', 'Husarz')
        g.wait(browser.pid, 'Husarz window', 300, 1)
        time.sleep(8)
        off = mark()
        quiet_motion(g, 8)
        result['husarz_idle'] = guest_window(read_from(off))
        # navigate (ctrl+L, URL, Enter); pointer keeps moving while the page loads
        g.key('ctrl', 'l')
        time.sleep(.3)
        g.type_text(('https://' if a.https else 'http://') + '10.0.2.100/' + a.page)
        t_enter = time.monotonic()
        off = mark()
        g.key('ret')
        quiet_motion(g, a.load_seconds)
        t_first = [t for t, p in g.requests if a.page in p]
        result['request_seen_after_s'] = round(t_first[0] - t_enter, 1) if t_first else None
        result['requests'] = [p for _, p in g.requests]
        result['loading'] = guest_window(read_from(off))
        text = g.serial_path.read_text(errors='replace')
        lines = text[off:].splitlines()
        result['ui_stall_lines'] = [l for l in lines if 'HUSARZ UI STALL' in l][:20]
        result['ui_stalls_over_50ms'] = sum(1 for l in lines if 'HUSARZ UI STALL' in l)
        off = mark()
        time.sleep(3)
        quiet_motion(g, 8)
        result['loaded'] = guest_window(read_from(off))
        g.screenshot('after-load')
        g.screenshot('end')
    finally:
        clock.stop.set()
        (a.out / 'responsiveness.json').write_text(json.dumps(result, indent=2, ensure_ascii=False))
        g.close()
    print(json.dumps({k: v for k, v in result.items() if k in ('desktop_idle', 'husarz_idle', 'loading', 'loaded',
                                                              'request_seen_after_s', 'ui_stalls_over_50ms')}, indent=1))


if __name__ == '__main__':
    main()
