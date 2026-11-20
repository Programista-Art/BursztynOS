#!/usr/bin/env python3
"""Capture mixed PCM from a single native Bursztyn OS boot with no data disks."""
import argparse
import array
from bisect import bisect_right
import json
import hashlib
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import tempfile
import time
import wave

from test_hda_regression_qemu import Qmp, ROOT

PHASES = ("BOTH", "GAIN_A_25", "MASTER_MUTE", "MASTER_RESTORE", "ONLY_B", "SILENCE", "DONE")
PREFIX = "[HDA-PCM] "
TRANSITION_MARGIN_MS = 250


def production_test_iso(source, target, work, out):
    """Replace just the GRUB menu in a byte-for-byte copy of the production ISO."""
    original = work / "grub-original.cfg"
    guest_config = "/boot/grub/grub.cfg"
    tools = ["xorriso", "-abort_on", "FAILURE"]
    with (out / "iso-build.log").open("w") as log:
        opts = {"stdout": log, "stderr": subprocess.STDOUT, "check": True, "timeout": 45}
        subprocess.run(tools + ["-osirrox", "on", "-indev", str(source), "-extract",
                                guest_config, str(original)], **opts)
        config = original.read_bytes()
        boot = config.decode()
        kernel = "multiboot2 /boot/system_operacyjny.bin"
        if kernel not in boot:
            raise RuntimeError("The production GRUB menu lacks the normal boot entry")
        boot = re.sub(r"^set timeout=.*$", "set timeout=0", boot, count=1, flags=re.M)
        boot = boot.replace(kernel, kernel + " bursztyn.test=hda-pcm bursztyn.splash=0", 1)
        # Repacking a hybrid GRUB image can shift the appended EFI partition.
        # Remove indentation from the menu commands to make room for the token.
        updated = re.sub(r"^[ \t]+", "", boot, flags=re.M).encode()
        layout = subprocess.check_output(tools + ["-indev", str(source), "-find", guest_config,
                                                 "-exec", "report_lba", "--"], stderr=log,
                                         timeout=45, text=True)
        log.write(layout)
        extents = re.findall(r"File data lba:\s*0\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,", layout)
        if len(extents) != 1:
            raise RuntimeError("Expected one contiguous GRUB menu extent in the production ISO")
        lba, blocks, size = map(int, extents[0])
        if len(updated) > len(config) or size != len(config) or blocks * 2048 < size:
            raise RuntimeError("The test menu does not fit the production ISO extent")
        shutil.copyfile(source, target)
        with target.open("r+b") as image:
            offset = lba * 2048
            image.seek(offset)
            if image.read(size) != config:
                raise RuntimeError("The production ISO extent does not match the extracted GRUB menu")
            image.seek(offset)
            image.write(updated.ljust(size, b"\n"))
    with source.open("rb") as image:
        digest = hashlib.file_digest(image, "sha256").hexdigest()
    (out / "production-iso-sha256.txt").write_text(f"{digest}  {source.name}\n")


def amplitude(samples, frequency, rate):
    step = 2 * math.pi * frequency / rate
    factor = 2 * math.cos(step)
    p = q = 0.0
    for x in samples:
        p, q = x + factor * p - q, p
    return 2 * math.sqrt(max(0, p * p + q * q - factor * p * q)) / len(samples)


def analyze(path, windows):
    with wave.open(str(path), "rb") as wav:
        rate, channels, width, frames = (wav.getframerate(), wav.getnchannels(),
                                         wav.getsampwidth(), wav.getnframes())
        if width != 2 or channels != 2 or rate != 48000:
            raise RuntimeError(f"Unexpected WAV format: {rate=}, {channels=}, {width=}")
        pcm = array.array("h", wav.readframes(frames))
    if sys.byteorder != "little":
        pcm.byteswap()
    left = pcm[::channels]
    first = next((i for i, value in enumerate(left) if abs(value) > 10), None)
    if first is None:
        raise RuntimeError("The recorded HDA output contains no signal")
    last = max(i for i, value in enumerate(left) if abs(value) > 10)
    result = {"rate_hz": rate, "channels": channels, "sample_bytes": width,
              "frames": frames, "seconds": frames / rate, "first_signal_frame": first,
              "last_signal_frame": last, "trailing_silence_seconds": (frames - last - 1) / rate,
              "windows": {}}
    selected = {}
    for name, (start, end) in windows.items():
        samples = left[round(start * rate):round(end * rate)]
        if not samples or len(samples) != round(end * rate) - round(start * rate):
            raise RuntimeError(f"WAV does not cover {name}")
        selected[name] = samples
        result["windows"][name] = {
            "window_s": [start, end],
            "amplitude_440": amplitude(samples, 440, rate),
            "amplitude_660": amplitude(samples, 660, rate),
            "peak": max(abs(value) for value in samples),
        }
    result["dominant_leading_hz"] = [
        max(range(hz - 5, hz + 6), key=lambda f: amplitude(selected["both"], f, rate))
        for hz in (440, 660)
    ]
    chunk = rate // 10
    only_b = selected["only_b"]
    result["only_b_min_amplitude_660_100ms"] = min(
        amplitude(only_b[i:i + chunk], 660, rate)
        for i in range(0, len(only_b) - chunk + 1, chunk))
    result["near_zero_intervals_between_signals_s_and_ms"] = near_zero_intervals(
        left[first:last + 1], first, rate)
    return result


def near_zero_intervals(samples, start, rate):
    intervals = []
    zeros = 0
    for idx, sample in enumerate(samples):
        if abs(sample) <= 1:
            zeros += 1
        else:
            if zeros >= rate * 2 // 1000:
                intervals.append([(start + idx - zeros) / rate, zeros * 1000 / rate])
            zeros = 0
    if zeros >= rate * 2 // 1000:
        intervals.append([(start + len(samples) - zeros) / rate, zeros * 1000 / rate])
    return intervals


def phase_sample_ranges(timings, rate, length):
    # Allow backend buffering, serial polling and already published PCM.
    margin = rate * TRANSITION_MARGIN_MS // 1000
    for phase, following in zip(PHASES[:5], PHASES[1:6]):
        lo = max(0, (timings[phase]["wav_bytes"] - 44) // 4 + margin)
        hi = min(length, (timings[following]["wav_bytes"] - 44) // 4 - margin)
        yield phase.lower(), lo, hi


def continuity(path, timings):
    """Check continuity over all sustained samples and select separate tone windows."""
    with wave.open(str(path), "rb") as wav:
        rate = wav.getframerate()
        pcm = array.array("h", wav.readframes(wav.getnframes()))
    if sys.byteorder != "little":
        pcm.byteswap()
    left = pcm[::2]
    phases, windows = {}, {}
    for phase, lo, hi in phase_sample_ranges(timings, rate, len(left)):
        data = left[lo:hi]
        width = rate // 2
        if len(data) < width:
            raise RuntimeError(f"WAV has less than half a second of sustained PCM in {phase}")
        zeros = longest = jumps = 0
        previous = None
        for sample in data:
            if abs(sample) <= 1:
                zeros += 1
                longest = max(longest, zeros)
            else:
                zeros = 0
            if previous is not None and abs(sample - previous) > 1000:
                jumps += 1
            previous = sample
        muted = phase == "master_mute"
        phases[phase] = {
            "frames_examined": len(data),
            "longest_near_zero_ms": longest * 1000 / rate,
            "jumps_over_1000": jumps,
            "peak": max(abs(sample) for sample in data),
            "intentional_master_mute": muted,
            "near_zero_intervals_s_and_ms": near_zero_intervals(data, lo, rate),
        }
        # Measure component levels in a clean half second, independently of
        # the continuity verdict. A recovery can shift the phase of a tone.
        midpoint = (hi + lo) // 2
        start = midpoint - width // 2
        for candidate in range(lo, hi - width + 1, rate // 20):
            chunk = left[candidate:candidate + width]
            if muted or (not near_zero_intervals(chunk, candidate, rate) and
                         all(abs(b - a) <= 1000 for a, b in zip(chunk, chunk[1:]))):
                start = candidate
                break
        windows[phase] = (start / rate, (start + width) / rate)
    return phases, windows


def sample_host_vcpu(pid, done, samples, vcpu, audio):
    """Low-rate Linux-host scheduling evidence, without pausing QEMU."""
    task_root = Path(f"/proc/{pid}/task")
    schedstat = None
    while not done.wait(0.010):
        try:
            if schedstat is None and vcpu:
                schedstat = task_root / str(vcpu[0]) / "schedstat"
            if schedstat is None:
                for task in task_root.iterdir():
                    try:
                        name = task.joinpath("comm").read_text()
                    except FileNotFoundError:
                        continue
                    if name.startswith("CPU 0/"):
                        schedstat = task / "schedstat"
                        break
            if schedstat is None:
                continue
            counts = list(map(int, schedstat.read_text().split()))
            state = schedstat.with_name("stat").read_text().rsplit(")", 1)[1].split()[0]
            try:
                wav_bytes = audio.stat().st_size
            except FileNotFoundError:
                wav_bytes = 0
            samples.append([time.monotonic_ns(), *counts, state, wav_bytes])
            if len(samples) == 8192:
                return
        except OSError:
            return


def irq_host_context(gaps, timings, samples, ui_events):
    """Approximate alignment with Linux monotonic time, via serial phase markers."""
    anchors = [(v["guest_hpet_ns"], round(v["host_monotonic_s"] * 1e9))
               for v in timings.values() if v["guest_hpet_ns"] is not None]
    guests = [a[0] for a in anchors]
    host_times = [sample[0] for sample in samples]
    if len(anchors) < 2 or len(samples) < 2:
        return []

    def host_time(guest_ns):
        i = min(max(0, bisect_right(guests, guest_ns) - 1), len(anchors) - 2)
        ga, ha = anchors[i]
        gb, hb = anchors[i + 1]
        return ha + (guest_ns - ga) * (hb - ha) // (gb - ga)

    result = []
    for gap in gaps:
        begin, end = host_time(gap["begin_ns"]), host_time(gap["end_ns"])
        i = max(0, bisect_right(host_times, begin) - 1)
        j = min(bisect_right(host_times, end), len(samples) - 1)
        if j <= i:
            continue
        window = samples[i:j + 1]
        result.append({
            "guest_begin_ns": gap["begin_ns"], "guest_end_ns": gap["end_ns"],
            "estimated_host_begin_ns": begin, "estimated_host_end_ns": end,
            "first_sample_ns": window[0][0], "last_sample_ns": window[-1][0],
            "max_sampling_interval_ns": max(b[0] - a[0] for a, b in zip(window, window[1:])),
            "vcpu_cpu_ns": window[-1][1] - window[0][1],
            "vcpu_runqueue_wait_ns": window[-1][2] - window[0][2],
            "vcpu_states": [row[4] for row in window],
            "overlapping_screendumps": [event["name"] for event in ui_events
                                        if event["begin_ns"] <= end and event["end_ns"] >= begin],
        })
    return result


def verify_signals(result):
    w = result["windows"]
    a, b = w["both"]["amplitude_440"], w["both"]["amplitude_660"]
    assert min(a, b) > 100, "Two signals were not found in the same window"
    assert result["dominant_leading_hz"] == [440, 660], "Unexpected spectral peaks"
    for name in ("gain_a_25", "master_restore"):
        assert 0.20 < w[name]["amplitude_440"] / a < 0.30, f"Incorrect stream gain in {name}"
        assert 0.85 < w[name]["amplitude_660"] / b < 1.15, f"The other stream changed in {name}"
    assert result["continuity"]["master_mute"]["peak"] <= 1, "Master mute left audible PCM"
    assert w["only_b"]["amplitude_440"] < 0.02 * a, "A is still present after close"
    assert result["only_b_min_amplitude_660_100ms"] > 0.70 * b, "B did not continue"
    assert result["trailing_silence_seconds"] > 0.10, "No sustained silence at the end"


def verify_continuity(result):
    assert result["guest"]["starts"] == 1, (
        f"The shared engine started {result['guest']['starts']} times; "
        f"{result['guest']['delays']} refill delays")
    assert result["guest"]["dma_errors"] == result["guest"]["recoveries"] == 0
    assert result["guest"]["starved_frames"] == 0, "A producer starved the mixer"
    assert result["guest_timing"]["missed_periods"] == 0, "The driver left periods silent"
    for name, metrics in result["continuity"].items():
        if name == "master_mute":
            continue
        assert metrics["frames_examined"] > 0, f"No continuity window in {name}"
        assert metrics["longest_near_zero_ms"] < 2, f"Unexpected silence in {name}"
        assert metrics["jumps_over_1000"] == 0, f"Discontinuous sample in {name}"


def cpu_seconds(pid):
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def click(qmp, x, y):
    events = [{"type": "abs", "data": {"axis": axis, "value": value * 32767 // (size - 1)}}
              for axis, value, size in (("x", x, 1280), ("y", y, 720))]
    qmp.call("input-send-event", {"events": events})
    time.sleep(0.10)
    for down in (True, False):
        qmp.call("input-send-event", {"events": [{"type": "btn", "data": {"button": "left", "down": down}}]})
        time.sleep(0.07)


def screenshot(qmp, path, events):
    # Avoid PNG encoding inside the QEMU monitor. Collect host wall time
    # even though the screenshot itself is reduced to a plain framebuffer dump.
    begin = time.monotonic_ns()
    answer = qmp.call("screendump", {"filename": str(path), "format": "ppm"})
    events.append({"name": path.name, "begin_ns": begin, "end_ns": time.monotonic_ns(),
                   "ok": "return" in answer})


def ui_probe(sock, out, phase, events, vcpu):
    qmp = Qmp(sock)
    try:
        if phase == "BOTH":
            reply = qmp.call("query-cpus-fast")
            if reply.get("return"):
                vcpu.append(reply["return"][0]["thread-id"])
            screenshot(qmp, out / "desktop.ppm", events)
            click(qmp, 1220, 705)
            time.sleep(0.35)
            screenshot(qmp, out / "calendar.ppm", events)
        elif phase == "GAIN_A_25":
            qmp.call("human-monitor-command", {"command-line": "sendkey esc 55"})
            time.sleep(0.15)
            click(qmp, 1043, 705)
            time.sleep(0.35)
            screenshot(qmp, out / "volume.ppm", events)
        elif phase == "MASTER_MUTE":
            time.sleep(0.35)
            screenshot(qmp, out / "muted.ppm", events)
            qmp.call("human-monitor-command", {"command-line": "sendkey esc 55"})
            time.sleep(0.15)
            click(qmp, 36, 705)
            time.sleep(0.35)
            for key in (*"procesy", "ret"):
                qmp.call("human-monitor-command", {"command-line": f"sendkey {key} 55"})
                time.sleep(0.11)
        elif phase == "ONLY_B":
            screenshot(qmp, out / "only-b.ppm", events)
    finally:
        qmp.close()


def capture(out, args):
    out.mkdir()
    serial = out / "serial.log"
    audio = out / "mixed.wav"
    with tempfile.TemporaryDirectory(prefix="bursztyn-pcm-") as temp:
        work = Path(temp)
        iso = work / "pcm.iso"
        sock = work / "qmp.sock"
        production_test_iso(args.iso.resolve(), iso, work, out)
        cmd = ["qemu-system-x86_64", "-machine", "pc,hpet=on", "-cpu", "max",
               "-accel", "tcg,thread=single", "-smp", "1", "-m", "2G",
               "-name", "bursztyn-pcm,debug-threads=on",
               "-bios", str(args.ovmf), "-cdrom", str(iso), "-boot", "d",
               "-audiodev", f"wav,id=snd,path={audio},out.frequency=48000,out.channels=2,out.format=s16",
               "-device", "intel-hda", "-device", "hda-output,audiodev=snd",
               "-netdev", "user,id=n1", "-device", "virtio-net-pci,netdev=n1,disable-legacy=on",
               "-object", "rng-builtin,id=rng0", "-device", "virtio-rng-pci,rng=rng0,disable-legacy=on",
               "-vga", "none", "-device", "virtio-vga,disable-legacy=on,xres=1280,yres=720",
               "-device", "virtio-keyboard-pci", "-device", "virtio-tablet-pci",
               "-serial", f"file:{serial}", "-display", "none", "-no-reboot",
               "-qmp", f"unix:{sock},server=on,wait=off"]
        (out / "qemu-argv.json").write_text(json.dumps(cmd, indent=2) + "\n")
        with (out / "qemu.log").open("w") as err:
            proc = subprocess.Popen(cmd, stdout=err, stderr=subprocess.STDOUT)
            timings = {}
            host_samples = []
            ui_events = []
            vcpu = []
            sampler_done = threading.Event()
            sampler = threading.Thread(target=sample_host_vcpu,
                                       args=(proc.pid, sampler_done, host_samples, vcpu, audio))
            sampler.start()
            try:
                deadline = time.monotonic() + args.timeout
                for phase in PHASES:
                    marker_pattern = re.compile(rf"^{re.escape(PREFIX + phase)} t_ns=(\d+)$", re.M)
                    while True:
                        evidence = serial.read_text(errors="replace") if serial.exists() else ""
                        if PREFIX + "FAIL" in evidence:
                            raise RuntimeError("Native PCM scenario reported FAIL")
                        marker = marker_pattern.search(evidence)
                        if marker:
                            break
                        if proc.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError(f"QEMU exited or timed out before {phase}; see {serial}")
                        time.sleep(0.025)
                    timings[phase] = {
                        "guest_hpet_ns": int(marker[1]),
                        "host_monotonic_s": time.monotonic(),
                        "host_cpu_s": cpu_seconds(proc.pid),
                        # WAV output can be buffered. Use points well inside
                        # each phase, and retain these raw markers for replay.
                        "wav_bytes": audio.stat().st_size if audio.exists() else 0,
                    }
                    (out / "phase-markers.json").write_text(json.dumps(timings, indent=2) + "\n")
                    print(PREFIX + phase, flush=True)
                    if phase in ("BOTH", "GAIN_A_25", "MASTER_MUTE", "ONLY_B"):
                        try:
                            ui_probe(sock, out, phase, ui_events, vcpu)
                        except (OSError, ValueError) as error:
                            print(f"UI probe {phase}: {error}", flush=True)
            finally:
                sampler_done.set()
                sampler.join()
                (out / "host-vcpu.json").write_text(json.dumps(
                    {"columns": ["monotonic_ns", "cpu_ns", "runqueue_wait_ns", "slices", "state", "wav_bytes"],
                     "thread_ids": vcpu, "samples": host_samples}, indent=2) + "\n")
                (out / "host-qmp.json").write_text(json.dumps(ui_events, indent=2) + "\n")
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=5)


def analyze_capture(out):
    evidence = (out / "serial.log").read_text(errors="replace")
    timings = json.loads((out / "phase-markers.json").read_text())
    host_samples = json.loads((out / "host-vcpu.json").read_text())["samples"]
    ui_events = json.loads((out / "host-qmp.json").read_text())
    audio = out / "mixed.wav"

    def counters(line):
        return {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)}

    def guest_record(name):
        return counters(next(line for line in evidence.splitlines() if PREFIX + name in line))

    guest = guest_record("STATS")
    guest_timing = guest_record("TIMING")
    causes = guest_record("CAUSES")
    silence_counts = guest_record("SILENCE_COUNTS")
    dma_status = guest_record("DMA_STATUS")
    irq_timing = guest_record("IRQ_TIMING")
    if irq_timing["intervals"]:
        irq_timing["mean_interval_ns"] = irq_timing["interval_sum_ns"] / irq_timing["intervals"]
    irq_gaps = [counters(line) for line in evidence.splitlines() if PREFIX + "IRQ_GAP " in line]
    largest_gap = guest_record("IRQ_MAX_GAP")
    if largest_gap not in irq_gaps:
        irq_gaps.append(largest_gap)
    masked = [dict(label=line.split()[1], **counters(line))
              for line in evidence.splitlines() if PREFIX + "MASK_" in line]
    recoveries = [counters(line) for line in evidence.splitlines() if PREFIX + "RECOVERY" in line]
    shortfalls = [counters(line) for line in evidence.splitlines() if PREFIX + "SHORTFALL " in line]
    feed_gaps = [counters(line) for line in evidence.splitlines() if PREFIX + "FEED_GAP " in line]
    feed_stats = next((counters(line) for line in evidence.splitlines() if PREFIX + "FEED_STATS " in line), {})
    if guest_timing["work"]:
        guest_timing["avg_work_wait_ns"] = guest_timing["work_wait_ns"] / guest_timing["work"]
    wall = timings["DONE"]["host_monotonic_s"] - timings["BOTH"]["host_monotonic_s"]
    cpu = timings["DONE"]["host_cpu_s"] - timings["BOTH"]["host_cpu_s"]
    sustained, windows = continuity(audio, timings)
    result = analyze(audio, windows)
    result.update(guest=guest, guest_timing=guest_timing, recovery_causes=causes,
                  recovery_events=recoveries, continuity=sustained,
                  shortfalls=shortfalls, producer_timing=feed_stats, producer_gaps=feed_gaps,
                  silence_counts=silence_counts, dma_status=dma_status, irq_timing=irq_timing, irq_gaps=irq_gaps,
                  masked_scopes=masked, irq_host_context=irq_host_context(irq_gaps, timings, host_samples, ui_events),
                  qemu_wall_s=wall, qemu_cpu_s=cpu, qemu_one_core_percent=100 * cpu / wall)
    for name, verify in (("signals", verify_signals), ("continuous_playback", verify_continuity)):
        try:
            verify(result)
        except AssertionError as error:
            result[name] = f"FAIL: {error or 'DMA recovery or error'}"
        else:
            result[name] = "PASS"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--out", type=Path, help="Record and analyze a new capture in this directory")
    mode.add_argument("--analyze", type=Path, help="Analyze an existing capture without starting QEMU")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--iso", type=Path, default=ROOT / "BursztynOS.iso", help="Production ISO")
    parser.add_argument("--ovmf", type=Path, default=Path("/usr/share/ovmf/OVMF.fd"))
    args = parser.parse_args()
    out = (args.out if args.out else args.analyze).resolve()
    if args.out:
        capture(out, args)
    result = analyze_capture(out)
    summary = json.dumps(result, indent=2) + "\n"
    (out / "measurements.json").write_text(summary)
    print(summary, end="")
    return int(result["signals"] != "PASS" or result["continuous_playback"] != "PASS")


if __name__ == "__main__":
    sys.exit(main())
