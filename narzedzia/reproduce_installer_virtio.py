#!/usr/bin/env python3
"""Bounded, disposable-disk repro runner for the UEFI VirtIO installer boot.

Every QEMU process has a QMP-controlled exit path and a process-group cleanup
fallback.  This script deliberately never accepts a disk outside /tmp and
never replaces an existing disk image.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import select
import subprocess
import sys
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DISK = Path("/tmp/bursztyn-installer-debug.img")
DEFAULT_OVMF = Path("/usr/share/ovmf/OVMF.fd")
SAMPLE_SECONDS = 0.20
KERNEL_ENTRY_MARKER = "[VIRTIO RESET TRACE] kernel entry"


def gpu_reset_pending(serial_text: str) -> bool:
    """Only a GPU byte-store begin without its return identifies this boundary."""
    pending = False
    for line in serial_text.splitlines():
        if "[PCI TRACE] 00:06.0 8-bit device_status reset store begin" in line:
            pending = True
        elif "[PCI TRACE] 00:06.0 8-bit device_status reset store returned" in line:
            pending = False
    return pending


def timeout_classification(serial_text: str, entered_kernel: bool) -> str:
    if not entered_kernel:
        return "PREKERNELTIMEOUT"
    return "GPURESETHANG" if gpu_reset_pending(serial_text) else "RUNNERERROR"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as image:
        for block in iter(lambda: image.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def meminfo() -> dict[str, int]:
    wanted = {"MemAvailable", "SwapFree", "SwapTotal"}
    values: dict[str, int] = {}
    for line in Path("/proc/meminfo").read_text().splitlines():
        key, value, *_ = line.replace(":", "").split()
        if key in wanted:
            values[key] = int(value) * 1024
    return values


def process_stats(pid: int) -> tuple[int, int]:
    """Return (cpu ticks, RSS bytes) without invoking an unbounded monitor."""
    stat = Path(f"/proc/{pid}/stat")
    status = Path(f"/proc/{pid}/status")
    if not stat.exists() or not status.exists():
        return 0, 0
    # comm can contain spaces/parentheses; fields after the last ')' are stable.
    fields = stat.read_text().rsplit(")", 1)[1].split()
    cpu_ticks = int(fields[11]) + int(fields[12])
    rss_kib = 0
    for line in status.read_text().splitlines():
        if line.startswith("VmRSS:"):
            rss_kib = int(line.split()[1])
            break
    return cpu_ticks, rss_kib * 1024


def qemu_pids() -> set[int]:
    found: set[int] = set()
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            comm = (entry / "comm").read_text().strip()
        except OSError:
            continue
        if comm.startswith("qemu-system-x86"):
            found.add(int(entry.name))
    return found


class Qmp:
    def __init__(self, reader: Any, writer: Any, timeout: float = 5.0,
                 sock: socket.socket | None = None) -> None:
        self.reader = reader
        self.writer = writer
        self.timeout = timeout
        self.sock = sock
        self.pending = b""
        greeting = self._readline()
        if not greeting:
            raise RuntimeError("QMP closed before greeting")
        self.call("qmp_capabilities")

    @classmethod
    def from_socket(cls, sock_path: Path, timeout: float = 5.0) -> "Qmp":
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect(str(sock_path))
        stream = sock.makefile("rwb", buffering=0)
        return cls(stream, stream, timeout, sock)

    @classmethod
    def from_stdio(cls, proc: subprocess.Popen[bytes], timeout: float = 5.0) -> "Qmp":
        if proc.stdout is None or proc.stdin is None:
            raise RuntimeError("QEMU stdio pipes are unavailable for QMP")
        return cls(proc.stdout, proc.stdin, timeout)

    def _readline(self) -> bytes:
        if self.sock is not None:
            self.sock.settimeout(self.timeout)
            return self.reader.readline()
        deadline = time.monotonic() + self.timeout
        while b"\n" not in self.pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("QMP stdio response timeout")
            readable, _, _ = select.select([self.reader], [], [], remaining)
            if not readable:
                raise TimeoutError("QMP stdio response timeout")
            chunk = os.read(self.reader.fileno(), 65536)
            if not chunk:
                return b""
            self.pending += chunk
        line, self.pending = self.pending.split(b"\n", 1)
        return line + b"\n"

    def set_timeout(self, timeout: float) -> None:
        self.timeout = timeout

    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any:
        request: dict[str, Any] = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.writer.write((json.dumps(request) + "\n").encode())
        self.writer.flush()
        while True:
            data = self._readline()
            if not data:
                raise RuntimeError("QMP closed while waiting for response")
            response = json.loads(data)
            if "return" in response:
                return response["return"]
            if "error" in response:
                raise RuntimeError(f"QMP {command}: {response['error']}")

    def close(self) -> None:
        if self.sock is not None:
            self.reader.close()
            self.sock.close()


def make_installer_iso(artifact_dir: Path) -> Path:
    """Make an ISO whose existing installer entry is selected automatically."""
    source_tree = ROOT / "isodir"
    if not source_tree.is_dir():
        raise RuntimeError("isodir is missing; run with --build or build make iso first")
    tree = artifact_dir / "installer-isodir"
    shutil.copytree(source_tree, tree)
    cfg = tree / "boot/grub/grub.cfg"
    text = cfg.read_text()
    old = text
    text = text.replace("set default=0", "set default=3", 1)
    text = text.replace("set timeout=3", "set timeout=0", 1)
    if text == old:
        raise RuntimeError("could not select the existing installer GRUB entry")
    cfg.write_text(text)
    iso = artifact_dir / "installer-auto.iso"
    with (artifact_dir / "grub-mkrescue.stderr").open("wb") as stderr:
        subprocess.run(
            ["grub-mkrescue", "-o", str(iso), str(tree)],
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=stderr, check=True,
        )
    return iso


def ensure_disposable_disk(path: Path) -> None:
    if path.parent != Path("/tmp") or path.name in ("", ".", ".."):
        raise RuntimeError(f"refusing a non-/tmp disposable disk: {path}")
    if path.exists():
        return
    subprocess.run(["qemu-img", "create", "-f", "raw", str(path), "128M"], check=True)


def wait_for_qmp(path: Path, proc: subprocess.Popen[bytes], timeout: float) -> Qmp:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited before QMP became available ({proc.returncode})")
        if path.exists():
            try:
                return Qmp.from_socket(path)
            except OSError as error:
                last_error = error
        time.sleep(0.05)
    raise TimeoutError(f"QMP socket timeout: {last_error}")


def cleanup(proc: subprocess.Popen[bytes], qmp: Qmp | None) -> str:
    """Prefer QMP quit, then TERM, then KILL.  Always reap our process group."""
    result = "already-exited"
    if proc.poll() is None and qmp is not None:
        try:
            qmp.call("quit")
            proc.wait(timeout=5)
            result = "qmp-quit"
        except Exception:
            result = "qmp-failed"
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5)
            result = "sigterm"
        except (ProcessLookupError, subprocess.TimeoutExpired):
            pass
    if proc.poll() is None:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)
        result = "sigkill"
    if qmp is not None:
        try:
            qmp.close()
        except OSError:
            pass
    return result


def read_proc_text(path: Path, limit: int = 16384) -> str:
    """Best-effort, bounded /proc collection for a wedged QEMU process."""
    try:
        return path.read_text(errors="replace")[:limit]
    except OSError as error:
        return f"<unavailable: {error}>\n"


def capture_timeout_forensics(proc: subprocess.Popen[bytes], qmp: Qmp | None,
                              phase_dir: Path) -> dict[str, Any]:
    """Record evidence before cleanup without extending the bounded test materially."""
    evidence: dict[str, Any] = {"pid": proc.pid, "host_memory": meminfo()}
    evidence["process_cpu_ticks"], evidence["process_rss_bytes"] = process_stats(proc.pid)
    (phase_dir / "qemu-status.txt").write_text(read_proc_text(Path(f"/proc/{proc.pid}/status")))
    (phase_dir / "qemu-stack.txt").write_text(read_proc_text(Path(f"/proc/{proc.pid}/stack")))
    if qmp is not None:
        try:
            qmp.set_timeout(1.0)
            evidence["qmp_query_status"] = qmp.call("query-status")
        except Exception as error:
            evidence["qmp_query_status_error"] = repr(error)

    task_data: dict[str, dict[str, str]] = {}
    task_dir = Path(f"/proc/{proc.pid}/task")
    try:
        task_entries = sorted(task_dir.iterdir(), key=lambda item: int(item.name))
    except OSError as error:
        evidence["task_enumeration_error"] = repr(error)
        task_entries = []
    for task in task_entries:
        task_data[task.name] = {
            "wchan": read_proc_text(task / "wchan", 512).strip(),
            "status": read_proc_text(task / "status", 4096),
            "stack": read_proc_text(task / "stack", 4096),
        }
    evidence["tasks"] = task_data
    for name, command in (
        ("ps-threads", ["ps", "-L", "-p", str(proc.pid), "-o", "pid,tid,stat,pcpu,rss,wchan:32,comm"]),
        ("gdb-backtrace", ["gdb", "-q", "-nx", "-batch", "-ex", "set pagination off",
                           "-ex", "thread apply all bt", "-ex", "detach", "-p", str(proc.pid)]),
    ):
        if shutil.which(command[0]):
            try:
                capture = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                         timeout=15, check=False)
                (phase_dir / f"{name}.txt").write_bytes(capture.stdout)
                evidence[f"{name}_returncode"] = capture.returncode
            except subprocess.TimeoutExpired as error:
                (phase_dir / f"{name}.txt").write_bytes(error.stdout or b"")
                evidence[f"{name}_error"] = "external 15s timeout"
    (phase_dir / "timeout-forensics.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return evidence


def qemu_command(disk: Path, firmware: Path, iso: Path, accel: str, serial: Path,
                 nice_level: int, display: str, gpu_device: str, gpu_disable_legacy: str) -> list[str]:
    # This is the modern runuefi topology in Makefile order.  -display none
    # and QMP are host-observation additions; they do not change guest devices.
    command = [] if nice_level == 0 else ["nice", "-n", str(nice_level)]
    gpu_options = f"{gpu_device},xres=1280,yres=720"
    if gpu_disable_legacy != "auto":
        gpu_options += f",disable-legacy={gpu_disable_legacy}"
    command += [
        "qemu-system-x86_64",
        "-bios", str(firmware),
        "-machine", "pc,hpet=on", "-cpu", "max", "-accel", accel,
        "-smp", "1", "-m", "2G", "-audiodev", "alsa,id=snd",
        "-device", "intel-hda", "-device", "hda-output,audiodev=snd",
        "-serial", f"file:{serial}",
        "-drive", f"id=disk,file={disk},format=raw,if=none",
        "-device", "virtio-blk-pci,drive=disk,disable-legacy=on",
        "-netdev", "user,id=n1", "-device", "virtio-net-pci,netdev=n1,disable-legacy=on",
        "-object", "rng-random,id=rng0,filename=/dev/urandom",
        "-device", "virtio-rng-pci,rng=rng0,disable-legacy=on",
        "-vga", "none", "-device", gpu_options,
        "-device", "virtio-keyboard-pci", "-device", "virtio-mouse-pci",
        "-boot", "order=d", "-cdrom", str(iso),
        "-qmp", "stdio",
    ]
    if display == "none":
        command += ["-display", "none"]
    elif display != "default":
        # An explicit backend is a host-side experiment only: it preserves the
        # exact same VirtIO-GPU PCI device and guest-visible topology.
        command += ["-display", display]
    return command


def run_phase(phase: str, attempt_dir: Path, disk: Path, base_iso: Path, accel: str,
              kernel_entry_timeout: float, ready_timeout: float,
              nice_level: int, display: str, gpu_device: str, gpu_disable_legacy: str) -> dict[str, Any]:
    phase_dir = attempt_dir / phase
    phase_dir.mkdir()
    serial = phase_dir / "serial.log"
    stderr = phase_dir / "qemu.stderr"
    firmware = phase_dir / "OVMF.fd"
    shutil.copyfile(DEFAULT_OVMF, firmware)  # monolithic OVMF can persist state
    iso = make_installer_iso(phase_dir) if phase == "installer" else base_iso
    command = qemu_command(disk, firmware, iso, accel, serial, nice_level, display, gpu_device,
                           gpu_disable_legacy)
    command += ["-d", "guest_errors,unimp", "-D", str(phase_dir / "qemu-debug.log")]
    # These events are verified against this QEMU build's -trace help output.
    for event in ("virtio_gpu_features", "virtio_gpu_cmd_get_display_info", "virtio_pci_notify_write"):
        command += ["-trace", f"enable={event}"]
    (phase_dir / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    before_qemu = qemu_pids()
    started = time.monotonic()
    proc: subprocess.Popen[bytes] | None = None
    qmp: Qmp | None = None
    result: dict[str, Any] = {
        "phase": phase, "command": command, "started_monotonic": started,
        "outcome": "RUNNERERROR", "last_serial_line": "", "pid": None,
        "kernel_entry_timeout_s": kernel_entry_timeout, "ready_timeout_s": ready_timeout,
        "kernel_entry_elapsed_s": None,
    }
    samples: list[dict[str, Any]] = []
    expected = "[INSTALL] Installer mode ready" if phase == "installer" else "System operacyjny gotowy!"
    next_sample = started
    cpu_previous = (0, started)
    peak_rss = 0
    peak_cpu = 0.0
    max_drift = 0.0
    kernel_entered_at: float | None = None
    try:
        with stderr.open("wb") as errfile:
            proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=errfile, start_new_session=True)
        result["pid"] = proc.pid
        (phase_dir / "pid").write_text(str(proc.pid) + "\n")
        qmp = Qmp.from_stdio(proc, 10)
        while True:
            now = time.monotonic()
            if now >= next_sample:
                ticks, rss = process_stats(proc.pid)
                elapsed = max(now - cpu_previous[1], 0.001)
                hz = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
                cpu = max(0.0, (ticks - cpu_previous[0]) / hz / elapsed * 100.0)
                cpu_previous = (ticks, now)
                peak_rss = max(peak_rss, rss)
                peak_cpu = max(peak_cpu, cpu)
                host = meminfo()
                sample = {"elapsed_s": now - started, "qemu_cpu_percent": cpu,
                          "qemu_rss_bytes": rss, "host_load": Path("/proc/loadavg").read_text().split()[:3],
                          "host_mem_available_bytes": host.get("MemAvailable", 0),
                          "host_swap_free_bytes": host.get("SwapFree", 0)}
                samples.append(sample)
                # Measure a missed sampling deadline, not cumulative loop overhead.
                max_drift = max(max_drift, now - next_sample)
                next_sample = now + SAMPLE_SECONDS
            text = serial.read_text(errors="replace") if serial.exists() else ""
            lines = [line for line in text.splitlines() if line.strip()]
            if lines:
                result["last_serial_line"] = lines[-1]
            if kernel_entered_at is None and KERNEL_ENTRY_MARKER in text:
                kernel_entered_at = now
                result["kernel_entry_elapsed_s"] = now - started
            result["gpu_transport_modern"] = any(
                "bdf=00:06.0 transport=modern" in line or
                ("modern transport found" in line and "BDF 00:06.0" in line)
                for line in lines)
            if expected in text:
                result["outcome"] = "BOOTPASS"
                if kernel_entered_at is None or not result["gpu_transport_modern"]:
                    result["outcome"] = "RUNNERERROR"
                    result["error"] = "readiness reached without diagnostic kernel-entry/modern GPU binding evidence"
                try:
                    result["blockstats"] = qmp.call("query-blockstats") if qmp else []
                except Exception as error:
                    result["blockstats_error"] = repr(error)
                try:
                    result["display_options"] = qmp.call("query-display-options") if qmp else {}
                except Exception as error:
                    result["display_options_error"] = repr(error)
                break
            if proc.poll() is not None:
                result["outcome"] = "RUNNERERROR"
                result["error"] = f"QEMU exited before readiness ({proc.returncode})"
                break
            deadline = (started + kernel_entry_timeout if kernel_entered_at is None
                        else kernel_entered_at + ready_timeout)
            if now >= deadline:
                result["outcome"] = timeout_classification(text, kernel_entered_at is not None)
                result["timeout_stage"] = "prekernel" if kernel_entered_at is None else "kernel-ready"
                if result["outcome"] == "RUNNERERROR":
                    result["error"] = "kernel readiness timeout outside the GPU reset-store boundary"
                (phase_dir / "gpu-reset-trace.log").write_text("\n".join(
                    line for line in lines if "VIRTIO RESET TRACE" in line or "00:06.0" in line) + "\n")
                if proc is not None:
                    result["timeout_forensics"] = capture_timeout_forensics(proc, qmp, phase_dir)
                break
            time.sleep(0.05)
    except Exception as error:
        result["outcome"] = "RUNNERERROR"
        result["error"] = repr(error)
    finally:
        if proc is not None:
            result["cleanup"] = cleanup(proc, qmp)
        result["duration_s"] = time.monotonic() - started
        result["peak_qemu_rss_bytes"] = peak_rss
        result["peak_qemu_cpu_percent"] = peak_cpu
        result["max_timer_drift_s"] = max(0.0, max_drift)
        result["samples"] = samples
        after_qemu = qemu_pids()
        result["orphan_qemu_pids"] = sorted(after_qemu - before_qemu)
        (phase_dir / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


SEQUENCES = {
    "installer": ("installer",),
    "live-installer": ("live", "installer"),
    "live-live": ("live", "live"),
    "installer-installer": ("installer", "installer"),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sequence", choices=SEQUENCES, default="installer")
    parser.add_argument("--iterations", type=int, default=1)
    parser.add_argument("--accel", default="tcg,thread=single")
    parser.add_argument("--kernel-entry-timeout", type=float, default=180.0)
    parser.add_argument("--ready-timeout", type=float, default=180.0)
    parser.add_argument("--nice-level", type=int, default=10,
                        help="positive values lower host priority; 0 exactly preserves normal priority")
    parser.add_argument("--display", default="none",
                        help="none is headless; default omits -display like Makefile; other values pass through to -display")
    parser.add_argument("--gpu-device", choices=("virtio-vga", "virtio-gpu-pci"), default="virtio-vga",
                        help="VirtIO GPU PCI frontend; use only for a controlled host-side comparison")
    parser.add_argument("--gpu-disable-legacy", choices=("on", "off", "auto"), default="on",
                        help="VirtIO GPU legacy-interface policy for a controlled transport comparison")
    parser.add_argument("--disk", type=Path, default=DEFAULT_DISK)
    parser.add_argument("--artifacts", type=Path, default=Path("/tmp/bursztyn-installer-repro"))
    parser.add_argument("--build", action="store_true", help="run a clean safe build of BursztynOS.iso first")
    args = parser.parse_args()
    if args.iterations < 1 or args.kernel_entry_timeout < 180 or args.ready_timeout < 180:
        parser.error("iterations must be positive and both stage timeouts must be at least 180 seconds")
    if not DEFAULT_OVMF.is_file():
        parser.error(f"monolithic OVMF required by this machine's runuefi branch: {DEFAULT_OVMF}")
    args.disk = args.disk.resolve()
    ensure_disposable_disk(args.disk)
    if args.build:
        subprocess.run(["make", "rm", f"VIRTUAL_DISK={args.disk}"], cwd=ROOT, check=True)
        subprocess.run(["make", "iso", "BURSZTYN_DIAG_VIRTIO_BOOT=1"], cwd=ROOT, check=True)
    base_iso = ROOT / "BursztynOS.iso"
    if not base_iso.is_file():
        parser.error("BursztynOS.iso is absent; pass --build")
    args.artifacts.mkdir(parents=True, exist_ok=True)
    disk_before = sha256(args.disk)
    summary: list[dict[str, Any]] = []
    for index in range(1, args.iterations + 1):
        attempt_dir = args.artifacts / f"{args.sequence}-{args.accel.replace(',', '_').replace('=', '-')}-{index:02d}"
        if attempt_dir.exists():
            parser.error(f"refusing to replace artifacts: {attempt_dir}")
        attempt_dir.mkdir()
        phases = [run_phase(phase, attempt_dir, args.disk, base_iso, args.accel,
                            args.kernel_entry_timeout, args.ready_timeout,
                            args.nice_level, args.display, args.gpu_device, args.gpu_disable_legacy)
                  for phase in SEQUENCES[args.sequence]]
        summary.append({"attempt": index, "phases": phases})
        print(f"attempt {index}/{args.iterations}: " + ", ".join(
            f"{phase['phase']}={phase['outcome']} ({phase['duration_s']:.2f}s, last={phase['last_serial_line']!r})"
            for phase in phases), flush=True)
    disk_after = sha256(args.disk)
    report = {"sequence": args.sequence, "accel": args.accel, "gpu_device": args.gpu_device,
              "gpu_disable_legacy": args.gpu_disable_legacy, "disk": str(args.disk),
              "disk_sha256_before": disk_before, "disk_sha256_after": disk_after,
              "attempts": summary}
    (args.artifacts / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    passed = sum(all(phase["outcome"] == "BOOTPASS" for phase in item["phases"]) for item in summary)
    print(f"summary: {passed}/{args.iterations} pass; disk SHA256 {'unchanged' if disk_before == disk_after else 'CHANGED'}", flush=True)
    return 0 if passed == args.iterations and disk_before == disk_after else 1


if __name__ == "__main__":
    sys.exit(main())
