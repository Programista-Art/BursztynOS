#!/usr/bin/env python3
"""Real Intel/QEMU HDA boot, codec discovery and production deferred tone stop.

Uses the built production kernel, an isolated opt-in ISO and no guest data disk.
UEFI follows the production launcher preference for monolithic OVMF, or uses a
disposable copy of the OVMF variable store with separate CODE/VARS firmware.
No host-side HDA model or manual test tick is involved. The wav backend checks
that actual emulated DMA produced nonzero, finite PCM samples.
"""
import argparse
import array
import hashlib
import json
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".hda-test" / "regression-qemu"
PREFIX = "[HDA-QEMU-REGRESSION]"


class Qmp:
    def __init__(self, path: Path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.connect(str(path))
        self.reader = self.socket.makefile("rb")
        self.reader.readline()
        self.call("qmp_capabilities")

    def call(self, execute: str, arguments: dict | None = None):
        request = {"execute": execute}
        if arguments is not None:
            request["arguments"] = arguments
        self.socket.sendall((json.dumps(request) + "\n").encode())
        while True:
            reply = json.loads(self.reader.readline())
            if "return" in reply or "error" in reply:
                return reply

    def close(self):
        self.reader.close()
        self.socket.close()


def capture_failure(socket_path: Path, backend: str) -> None:
    diagnostics = {}
    try:
        qmp = Qmp(socket_path)
        for command in ("query-status", "query-cpus-fast", "query-pci", "query-blockstats"):
            diagnostics[command] = qmp.call(command)
        for command in ("info registers", "info pic", "info roms"):
            diagnostics[command] = qmp.call("human-monitor-command", {"command-line": command})
        diagnostics["screendump"] = qmp.call("screendump", {"filename": str(OUT / f"{backend}-failure.ppm")})
        qmp.close()
    except (OSError, ValueError) as error:
        diagnostics["capture_error"] = str(error)
    (OUT / f"{backend}-failure.json").write_text(json.dumps(diagnostics, indent=2) + "\n")


def make_iso(kernel: Path, wallpapers: Path, destination: Path, staging: Path, timeout: float,
             *, boot_args: str = "bursztyn.test=hda-tone", log_dir: Path = OUT) -> None:
    boot = staging / "boot"
    (boot / "grub").mkdir(parents=True)
    shutil.copy2(kernel, boot / "system_operacyjny.bin")
    shutil.copy2(wallpapers, boot / "tapety.pak")
    (boot / "grub" / "grub.cfg").write_text(
        'set timeout=0\nset default=0\n'
        'menuentry "Bursztyn OS explicit HDA regression" {\n'
        '    insmod all_video\n'
        '    set gfxmode=1920x1080x32,1280x720x32,1024x768x32,auto\n'
        '    set gfxpayload=keep\n'
        f'    multiboot2 /boot/system_operacyjny.bin {boot_args}\n'
        '    module2 /boot/tapety.pak tapety.pak\n'
        '    boot\n}\n', encoding="ascii")
    with (log_dir / "iso-build.log").open("w") as log:
        subprocess.run(["grub-mkrescue", "-o", str(destination), str(staging)],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=min(45, timeout), check=True)


def validate_pcm(path: Path) -> dict:
    with wave.open(str(path), "rb") as wav:
        channels, sample_bytes, rate, frames = (wav.getnchannels(), wav.getsampwidth(),
                                                wav.getframerate(), wav.getnframes())
        if sample_bytes != 2 or rate < 8000 or not frames:
            raise RuntimeError(f"Unexpected WAV format: {channels=} {sample_bytes=} {rate=} {frames=}")
        pcm = array.array("h", wav.readframes(frames))
    if sys.byteorder != "little":
        pcm.byteswap()
    nonzero_frames = [i // channels for i, sample in enumerate(pcm) if sample]
    if not nonzero_frames:
        raise RuntimeError("Real QEMU HDA DMA produced no nonzero PCM samples")
    first, last = nonzero_frames[0], nonzero_frames[-1]
    active_seconds = (last - first + 1) / rate
    # QEMU WAV audio timers can omit/pad the first/last small block. Hardware
    # RUN/deadline assertions in the guest supply the exact stopping evidence.
    if not 0.25 <= active_seconds <= 0.80:
        raise RuntimeError(f"PCM tone is not a bounded 500 ms burst: {active_seconds:.6f} s")
    return {"channels": channels, "sample_bytes": sample_bytes, "rate_hz": rate,
            "frames": frames, "nonzero_samples": len(nonzero_frames),
            "first_nonzero_frame": first, "last_nonzero_frame": last,
            "active_seconds": active_seconds, "peak": max(abs(s) for s in pcm)}


def run_vm(iso: Path, backend: str, timeout: float, firmware: list[str], boot_medium: str) -> dict:
    serial, errors = OUT / f"{backend}-serial.log", OUT / f"{backend}-qemu.log"
    audio = OUT / "tone.wav"
    debugcon, firmware_debug = OUT / f"{backend}-debugcon.log", OUT / f"{backend}-firmware.log"
    qmp_path = Path(tempfile.gettempdir()) / f"bursztyn-hda-qmp-{backend}-{time.time_ns()}.sock"
    for path in (serial, errors, debugcon, firmware_debug, audio if backend == "wav" else serial):
        path.unlink(missing_ok=True)
    audio_arg = f"wav,id=snd,path={audio}" if backend == "wav" else "none,id=snd"
    # Keep the production modern VirtIO relative mouse + keyboard configuration
    # from QEMU_VIRTIO_COMMON_ARGS. No user or test disk is attached.
    command = ["qemu-system-x86_64", "-machine", "pc,hpet=on", "-cpu", "max",
               "-accel", "tcg,thread=single", "-smp", "1", "-m", "2G",
               "-audiodev", audio_arg, "-device", "intel-hda",
               "-device", "hda-output,audiodev=snd",
               "-serial", f"file:{serial}", "-display", "none",
               "-debugcon", f"file:{debugcon}",
               "-chardev", f"file,id=fwdebug,path={firmware_debug}",
               "-device", "isa-debugcon,iobase=0x402,chardev=fwdebug",
               "-netdev", "user,id=n1",
               "-device", "virtio-net-pci,netdev=n1,disable-legacy=on",
               "-object", "rng-builtin,id=rng0",
               "-device", "virtio-rng-pci,rng=rng0,disable-legacy=on",
               "-vga", "none", "-device", "virtio-vga,disable-legacy=on,xres=1280,yres=720",
               "-device", "virtio-keyboard-pci", "-device", "virtio-mouse-pci",
               "-no-reboot",
               "-qmp", f"unix:{qmp_path},server=on,wait=off"] + firmware
    if boot_medium == "virtio":
        command += ["-drive", f"id=hdaboot,file={iso},format=raw,if=none,readonly=on",
                    "-device", "virtio-blk-pci,drive=hdaboot,disable-legacy=on,bootindex=1"]
    else:
        command += ["-cdrom", str(iso), "-boot", "d"]
    (OUT / f"{backend}-argv.json").write_text(json.dumps(command, indent=2) + "\n")
    deadline = time.monotonic() + timeout
    evidence = ""
    with errors.open("w") as err:
        proc = subprocess.Popen(command, cwd=ROOT, stdout=err, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() < deadline:
                evidence = serial.read_text(errors="replace") if serial.exists() else ""
                if PREFIX + " FAIL " in evidence:
                    raise RuntimeError("Guest HDA regression failed; see " + str(serial))
                if PREFIX + " PASS " in evidence:
                    # Allow the real backend to flush the final samples, then
                    # terminate gracefully so QEMU finalizes the WAV header.
                    time.sleep(0.5)
                    break
                if proc.poll() is not None:
                    raise RuntimeError(f"QEMU exited before regression result ({proc.returncode}); see {errors}")
                time.sleep(0.1)
            else:
                raise RuntimeError(f"QEMU regression exceeded {timeout} seconds; see {serial}")
        except Exception:
            if proc.poll() is None:
                capture_failure(qmp_path, backend)
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
            qmp_path.unlink(missing_ok=True)
    required = ["[SYSTEM] Pulpit gotowy.", "PCI=8086:2668 STATESTS_INITIAL=0x0001", "CORB_SIZE=256 RIRB_SIZE=256 CORB_RUN=1 RIRB_RUN=1 CODECS=1",
                "CODEC CAD=0 VENDOR=0x1AF4", " RESPONDS=1", " AFG NID=", "ROUTE COUNT=",
                "TEST_AVAILABLE=1", "TONE_START RUN=1 ACTIVE=1", "TONE_BEFORE_DEADLINE RUN=1 ACTIVE=1",
                "TONE_AFTER_DEADLINE RUN=0 ACTIVE=0 RUN_AFTER_STOP=0 STOP_REASON=deadline", PREFIX + " PASS "]
    missing = [token for token in required if token not in evidence]
    if missing:
        raise RuntimeError(f"Real QEMU evidence missing {missing}; see {serial}")
    result = {"backend": backend, "serial_log": str(serial), "result": "PASS"}
    if backend == "wav":
        result["pcm"] = validate_pcm(audio)
    for line in evidence.splitlines():
        if PREFIX in line:
            print(line, flush=True)
    print(f"Actual QEMU {backend} regression PASS", flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, default=ROOT / "system_operacyjny.bin")
    parser.add_argument("--wallpapers", type=Path, default=ROOT / "tapety.pak")
    parser.add_argument("--timeout", type=float, default=120, help="Total ISO/VM wall-clock budget in seconds")
    parser.add_argument("--also-none", action="store_true", help="Repeat actual controller test with null audio backend")
    parser.add_argument("--bios", action="store_true", help="Use legacy BIOS instead of the default production UEFI/OVMF path")
    parser.add_argument("--ovmf-mono", type=Path, help="Optional monolithic OVMF firmware override (production launcher preference)")
    parser.add_argument("--ovmf-code", type=Path, help="Optional OVMF CODE firmware override")
    parser.add_argument("--ovmf-vars", type=Path, help="Optional OVMF VARS template override; only a temporary copy is written")
    parser.add_argument("--boot-medium", choices=("cdrom", "virtio"), default="cdrom",
                        help="Boot temporary hybrid ISO from CD or a read-only production modern VirtIO-BLK device")
    args = parser.parse_args()
    if args.timeout < 5:
        parser.error("--timeout must allow at least 5 seconds")
    for tool in ("qemu-system-x86_64", "grub-mkrescue"):
        if not shutil.which(tool):
            parser.error(f"Required executable unavailable: {tool}")
    for path in (args.kernel, args.wallpapers):
        if not path.is_file():
            parser.error(f"Build production kernel/wallpaper first; unavailable: {path}")
    code, variables, mono = None, None, args.ovmf_mono
    if not args.bios:
        code = args.ovmf_code
        variables = args.ovmf_vars
        candidates = [(Path("/usr/share/OVMF/OVMF_CODE_4M.fd"), Path("/usr/share/OVMF/OVMF_VARS_4M.fd")),
                      (Path("/usr/share/OVMF/OVMF_CODE.fd"), Path("/usr/share/OVMF/OVMF_VARS.fd")),
                      (Path("/usr/share/edk2/x64/OVMF_CODE.fd"), Path("/usr/share/edk2/x64/OVMF_VARS.fd"))]
        if bool(code) != bool(variables):
            parser.error("Provide both --ovmf-code and --ovmf-vars together")
        if mono and code:
            parser.error("Use either monolithic OVMF or separate CODE/VARS firmware")
        if not mono and not code:
            for candidate in (Path("/usr/share/ovmf/OVMF.fd"), Path("/usr/share/OVMF/OVMF.fd"), Path("/usr/share/qemu/OVMF.fd")):
                if candidate.is_file():
                    mono = candidate
                    break
        if not mono and not code:
            for candidate_code, candidate_vars in candidates:
                if candidate_code.is_file() and candidate_vars.is_file():
                    code, variables = candidate_code, candidate_vars
                    break
        if mono and not mono.is_file():
            parser.error(f"Monolithic OVMF firmware unavailable: {mono}")
        if not mono and (not code or not variables or not code.is_file() or not variables.is_file()):
            parser.error("Complete OVMF CODE/VARS firmware unavailable; supply overrides or use --bios")
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "result.json").unlink(missing_ok=True)
    (OUT / "last-error.txt").unlink(missing_ok=True)
    metadata = {"kernel": str(args.kernel.resolve()),
                "kernel_sha256": hashlib.sha256(args.kernel.read_bytes()).hexdigest(),
                "qemu": subprocess.check_output(["qemu-system-x86_64", "--version"], text=True).splitlines()[0],
                "firmware": "BIOS" if args.bios else str(mono or code),
                "boot_medium": args.boot_medium,
                "disk_attached": False, "results": []}
    (OUT / "launch.json").write_text(json.dumps(metadata, indent=2) + "\n")
    finish_by = time.monotonic() + args.timeout
    with tempfile.TemporaryDirectory(prefix="bursztyn-hda-qemu-") as directory:
        temp = Path(directory)
        iso = temp / "hda-regression.iso"
        make_iso(args.kernel, args.wallpapers, iso, temp / "staging", args.timeout)
        firmware = []
        if not args.bios and mono:
            firmware = ["-bios", str(mono)]
        elif not args.bios:
            runtime_vars = temp / "OVMF_VARS.fd"
            shutil.copy2(variables, runtime_vars)
            firmware = ["-drive", f"if=pflash,format=raw,readonly=on,file={code}",
                        "-drive", f"if=pflash,format=raw,file={runtime_vars}"]
        metadata["results"].append(run_vm(iso, "wav", max(0, finish_by - time.monotonic()), firmware, args.boot_medium))
        if args.also_none:
            if not args.bios and not mono:
                shutil.copy2(variables, runtime_vars)
            metadata["results"].append(run_vm(iso, "none", max(0, finish_by - time.monotonic()), firmware, args.boot_medium))
    (OUT / "result.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.SubprocessError, OSError, wave.Error) as error:
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "last-error.txt").write_text(str(error) + "\n")
        print(f"Actual QEMU HDA verification FAILED: {error}", file=sys.stderr)
        raise SystemExit(1)
