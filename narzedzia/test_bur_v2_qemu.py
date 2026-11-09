#!/usr/bin/env python3
"""Disposable mixed v1/v2 guest: execute converted PIE repeatedly (>= 100 trials) in the real loader,
verify live 64-bit hardware PTEs (W^X / PTE_NX), test all negative fault modes, and verify fail-closed entropy."""
import hashlib
import json
import pathlib
import re
import shutil
import socket
import struct
import subprocess
import time

from test_bws_abuse_qemu import get_make_objs, run_cmd, ROOT
from symbolicate_bur_v2_crash import symbolicate_line

PTE_PRESENT = 1 << 0
PTE_WRITE = 1 << 1
PTE_USER = 1 << 2
PTE_SHARED = 1 << 9
PTE_NX = 1 << 63


class QmpClient:
    def __init__(self, path: pathlib.Path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(10.0)
        self.sock.connect(str(path))
        self.file = self.sock.makefile("r", encoding="utf-8")
        self.file.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, execute: str, **arguments):
        payload = {"execute": execute}
        if arguments:
            payload["arguments"] = arguments
        self.sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
        while True:
            line = self.file.readline()
            if not line:
                raise RuntimeError("QMP closed")
            msg = json.loads(line)
            if "return" in msg:
                return msg["return"]
            if "error" in msg:
                raise RuntimeError(str(msg["error"]))

    def hmp(self, command_line: str) -> str:
        return self.cmd("human-monitor-command", **{"command-line": command_line})

    def read_phys(self, address: int, count: int) -> bytes:
        values = []
        for row in self.hmp(f"xp /{count}bx 0x{address:x}").splitlines():
            if ":" in row:
                values.extend(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]+)", row.split(":", 1)[1]))
        assert len(values) == count, (hex(address), count, len(values))
        return bytes(values)

    def read_u64(self, address: int) -> int:
        return struct.unpack("<Q", self.read_phys(address, 8))[0]

    def walk_pte(self, cr3: int, va: int) -> dict:
        table = cr3 & 0x000FFFFFFFFFF000
        levels = []
        for shift in (39, 30, 21, 12):
            idx = (va >> shift) & 0x1FF
            entry = self.read_u64(table + idx * 8)
            levels.append({"shift": shift, "index": idx, "entry": f"0x{entry:016x}"})
            if (entry & PTE_PRESENT) == 0:
                return {"present": False, "entry": 0, "levels": levels}
            if shift == 12 or (shift in (30, 21) and (entry & (1 << 7)) != 0):
                return {
                    "present": True,
                    "entry": entry,
                    "entry_hex": f"0x{entry:016x}",
                    "write": bool(entry & PTE_WRITE),
                    "user": bool(entry & PTE_USER),
                    "shared": bool(entry & PTE_SHARED),
                    "nx": bool(entry & PTE_NX),
                    "levels": levels,
                }
            table = entry & 0x000FFFFFFFFFF000
        return {"present": False, "entry": 0, "levels": levels}

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def resolve_kernel_symbols(elf_path: pathlib.Path) -> dict:
    out = subprocess.check_output(["nm", "-C", str(elf_path)], text=True)
    syms = {}
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and re.match(r"^[0-9a-fA-F]+$", parts[0]):
            syms[parts[2]] = int(parts[0], 16)
    return syms


def main():
    out = ROOT / ".aslr-test/guest"
    out.mkdir(parents=True, exist_ok=True)
    flags = [
        "x86_64-linux-gnu-g++", "-I.", "-std=c++17", "-O2", "-g", "-ffreestanding",
        "-mno-red-zone", "-fno-exceptions", "-fno-rtti", "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables", "-fstack-protector-strong", "-mstack-protector-guard=global",
    ]
    run_cmd([*flags, "-fPIE", "-fvisibility=hidden", "-c", "narzedzia/bur_v2_probe.cpp", "-o", str(out / "probe.o")])
    run_cmd([
        "x86_64-linux-gnu-ld", "-shared", "-Bsymbolic", "--no-undefined", "-z", "text",
        "--gc-sections", "-T", "bur_v2_linker.ld", str(out / "probe.o"), "-o", str(out / "probe.elf"),
    ])
    run_cmd(["python3", "narzedzia/elftoburv2.py", str(out / "probe.elf"), str(out / "test.bin")])

    # Legacy WM launch ABI, running simultaneously with v2 children.
    source = (ROOT / "narzedzia/fixtures/bur_v1_launcher_prefix.inc").read_text()
    source += 'extern "C" __attribute__((noreturn)) void _start() {'
    start = source.index('extern "C" __attribute__((noreturn)) void _start() {')
    source = (
        source[:start]
        + '''extern "C" __attribute__((noreturn)) void _start() {
      bws_wywolaj(BWS_URUCHOM_PROGRAM, (uint64_t)"/programy/fpu-a.cebula/fpu-a.bur", 0, 0, 0);
      unsigned iteration = 0;
      static const char* modes[] = {"p", "g", "t", "r", "e", "d", "b", "h", "s", "m", "u", "c"};
      for (;;) {
        const char* mode = iteration < 12 ? modes[iteration] : "";
        if (bws_wywolaj(BWS_URUCHOM_PROGRAM, (uint64_t)"/programy/test.cebula/test.bur", (mode[0] ? (uint64_t)mode : 0), 0, 0)) {
          uint64_t wait_ns = (iteration == 0) ? 1800000000ULL : (iteration < 12 ? 120000000ULL : 35000000ULL);
          ++iteration;
          auto until = bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0) + wait_ns;
          while (bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0) < until) asm volatile("pause");
        } else {
          static bool logged_deny = false;
          if (!logged_deny) {
            bws_wywolaj(BWS_WYPISZ, (uint64_t)"[BUR2 LAUNCH DENIED]\\n", 0, 0, 0);
            logged_deny = true;
          }
          auto until = bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0) + 50000000ULL;
          while (bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0) < until) asm volatile("pause");
        }
      }
    }
'''
    )
    (out / "launcher.cpp").write_text(source)
    run_cmd([*flags, "-mcmodel=large", "-fno-pie", "-c", str(out / "launcher.cpp"), "-o", str(out / "launcher.o")])
    run_cmd([
        "x86_64-linux-gnu-ld", "--gc-sections", "-T", "menedzer_okien_linker.ld",
        "-nostdlib", "-no-pie", "--undefined=__stack_chk_guard", "--undefined=__stack_chk_fail",
        str(out / "launcher.o"), "bursztyn_gui.o", "-o", str(out / "launcher.elf"),
    ])
    run_cmd(["objcopy", "-O", "binary", str(out / "launcher.elf"), str(out / "menedzer_okien.bin")])
    shutil.copy2(ROOT / "narzedzia/fixtures/bur_v1_legacy/fpu_probe.bin", out / "fpu_probe.bin")
    for app in ("test", "menedzer_okien", "fpu_probe"):
        run_cmd(["ld", "-r", "-b", "binary", app + ".bin", "-o", app + "_blob.o"], cwd=out)
    objects = [
        str(out / o) if o in ("test_blob.o", "menedzer_okien_blob.o", "fpu_probe_blob.o") else o
        for o in get_make_objs()
    ]
    run_cmd([
        "x86_64-linux-gnu-g++", "-T", "linker.ld", "-nostdlib", "-no-pie", "-static",
        "-Wl,-z,noexecstack", "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
        "-o", str(out / "kernel.elf"), *objects, "-lgcc",
    ])
    syms = resolve_kernel_symbols(out / "kernel.elf")
    stage = out / "iso"
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "boot/grub").mkdir(parents=True)
    run_cmd(["objcopy", "--strip-all", str(out / "kernel.elf"), str(stage / "boot/system_operacyjny.bin")])
    (stage / "boot/grub/grub.cfg").write_text(
        'set timeout=0\nmenuentry "BUR v2 guest test" {\ninsmod all_video\nset gfxmode=1280x720x32\n'
        "set gfxpayload=keep\nmultiboot2 /boot/system_operacyjny.bin\nboot\n}\n"
    )
    iso = out / "test.iso"
    run_cmd(["grub-mkrescue", "-o", str(iso), str(stage)])

    log = out / "serial.log"
    log.write_text("")
    qmp_sock = out / "qmp.sock"
    if qmp_sock.exists():
        qmp_sock.unlink()

    vm = subprocess.Popen(
        [
            "qemu-system-x86_64", "-machine", "pc,hpet=on", "-cpu", "max",
            "-accel", "tcg", "-m", "2048", "-bios", "/usr/share/ovmf/OVMF.fd", "-display", "none",
            "-vga", "none", "-device", "virtio-vga,disable-legacy=on",
            "-drive", f"file={iso},format=raw,if=none,id=boot,readonly=on",
            "-device", "virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1",
            "-device", "virtio-rng-pci,disable-legacy=on",
            "-qmp", f"unix:{qmp_sock},server=on,wait=off",
            "-serial", f"file:{log}", "-no-reboot",
        ],
        stdout=subprocess.DEVNULL,
        stderr=(out / "qemu.log").open("w"),
    )
    pte_audit = None
    try:
        deadline = time.monotonic() + 300
        layout_re = re.compile(
            r"\[BUR2 LAYOUT\] code=(0x[0-9a-f]+) stack=(0x[0-9a-f]+) heap=(0x[0-9a-f]+) "
            r"guard=(0x[0-9a-f]+) shm=(0x[0-9a-f]+) rodata=(0x[0-9a-f]+) data=(0x[0-9a-f]+) bss=(0x[0-9a-f]+)"
        )
        while time.monotonic() < deadline:
            text = log.read_text(errors="replace")
            if pte_audit is None and "[BUR2 PTE HOLD]" in text:
                m = layout_re.search(text)
                assert m is not None
                code_va, stack_va, heap_va, guard_va, shm_va, rodata_va, data_va, bss_va = (
                    int(m.group(i), 16) for i in range(1, 9)
                )
                qmp = QmpClient(qmp_sock)
                try:
                    # Find the active BUR v2 child in uklady_procesow
                    uklady_sym = syms["uklady_procesow"]
                    tablica_sym = syms["tablica_procesow"]
                    target_pid = None
                    stack_low = 0
                    stack_high = 0
                    for pid in range(1, 16):
                        raw_u = qmp.read_phys(uklady_sym + pid * 72, 72)
                        baza, entry, s_dol, s_gora, s_guard, h_baza, h_lim, shm_b, ver = struct.unpack("<8QI4x", raw_u)
                        if ver == 2 and baza == ((code_va & ~0xFFF) - 0x1000):
                            target_pid = pid
                            stack_low = s_dol
                            stack_high = s_gora
                            break
                    assert target_pid is not None, "Could not locate BUR v2 process in uklady_procesow"
                    cr3 = qmp.read_u64(tablica_sym + target_pid * 144 + 0x38)
                    pte_audit = {
                        "pid": target_pid,
                        "cr3": f"0x{cr3:016x}",
                        "text": qmp.walk_pte(cr3, code_va),
                        "rodata": qmp.walk_pte(cr3, rodata_va),
                        "ssp_guard": qmp.walk_pte(cr3, guard_va),
                        "data": qmp.walk_pte(cr3, data_va),
                        "bss": qmp.walk_pte(cr3, bss_va),
                        "heap": qmp.walk_pte(cr3, heap_va),
                        "stack": qmp.walk_pte(cr3, stack_va),
                        "shm": qmp.walk_pte(cr3, shm_va),
                        "stack_guard_lower": qmp.walk_pte(cr3, stack_low - 4096),
                        "stack_guard_upper": qmp.walk_pte(cr3, stack_high),
                    }
                finally:
                    qmp.close()

            if text.count("[BUR2 EXEC PASS]") >= 112 and "[FPU PROBE] A PASS" in text:
                break
            if vm.poll() is not None:
                break
            time.sleep(0.2)

        text = log.read_text(errors="replace")
        rows = layout_re.findall(text)
        assert len(rows) >= 112, f"Expected >= 112 layouts, got {len(rows)}\n" + text[-5000:]
        assert "[FPU PROBE] A PASS" in text, text[-5000:]
        assert "[BUR2 NEGATIVE FAILED]" not in text
        for mode in "gtredbhsmuc":
            assert "[BUR2 NEGATIVE] " + mode in text, f"Missing negative mode {mode}"
        assert text.count("[USER FAULT]") >= 10, f"Expected >= 10 user faults, got {text.count('[USER FAULT]')}"
        assert "[STACKPROTECTORVIOLATION]" in text
        assert "[BUR2 FAIL]" not in text
        assert pte_audit is not None, "Live hardware PTE audit did not run"

        # Verify hardware page-table permissions (64-bit PTE including bit 63 PTE_NX):
        assert pte_audit["text"]["present"] and pte_audit["text"]["user"] and not pte_audit["text"]["write"] and not pte_audit["text"]["nx"], pte_audit["text"]
        assert pte_audit["rodata"]["present"] and pte_audit["rodata"]["user"] and not pte_audit["rodata"]["write"] and pte_audit["rodata"]["nx"], pte_audit["rodata"]
        assert pte_audit["ssp_guard"]["present"] and pte_audit["ssp_guard"]["user"] and not pte_audit["ssp_guard"]["write"] and pte_audit["ssp_guard"]["nx"], pte_audit["ssp_guard"]
        for rw_nx_section in ("data", "bss", "heap", "stack", "shm"):
            sec = pte_audit[rw_nx_section]
            assert sec["present"] and sec["user"] and sec["write"] and sec["nx"], (rw_nx_section, sec)
        assert not pte_audit["stack_guard_lower"]["present"], pte_audit["stack_guard_lower"]
        assert not pte_audit["stack_guard_upper"]["present"], pte_audit["stack_guard_upper"]

        symbolicated = [
            r for line in text.splitlines()
            if (r := symbolicate_line(line, bur_path=out / "test.bin")) is not None
        ]
        assert len(symbolicated) >= 10, symbolicated
        code_faults = [
            r for r in symbolicated
            if r["bur_version"] == 2 and r["symbol"] == "_start" and "bur_v2_probe.cpp" in (r["location"] or "")
        ]
        assert len(code_faults) >= 3, symbolicated

        five_col_rows = [r[:5] for r in rows]
        for column in range(5):
            assert len({r[column] for r in five_col_rows}) >= 95, ("insufficient variation", column, len({r[column] for r in five_col_rows}))
            assert all(0 < int(r[column], 16) < (1 << 47) for r in five_col_rows)
    finally:
        vm.terminate()
        try:
            vm.wait(timeout=5)
        except subprocess.TimeoutExpired:
            vm.kill()
            vm.wait()

    # Second boot test: prove fail-closed behavior when VirtIO-RNG, RDSEED, and RDRAND are ALL absent.
    no_rng_log = out / "serial_no_entropy.log"
    no_rng_log.write_text("")
    vm_no_rng = subprocess.Popen(
        [
            "qemu-system-x86_64", "-machine", "pc,hpet=on", "-cpu", "qemu64,-rdrand,-rdseed",
            "-accel", "tcg", "-m", "2048", "-bios", "/usr/share/ovmf/OVMF.fd", "-display", "none",
            "-vga", "none", "-device", "virtio-vga,disable-legacy=on",
            "-drive", f"file={iso},format=raw,if=none,id=boot,readonly=on",
            "-device", "virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1",
            "-serial", f"file:{no_rng_log}", "-no-reboot",
        ],
        stdout=subprocess.DEVNULL,
        stderr=(out / "qemu_no_entropy.log").open("w"),
    )
    try:
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            ntext = no_rng_log.read_text(errors="replace")
            if "[BUR2 LAUNCH DENIED]" in ntext and "[FPU PROBE] A PASS" in ntext:
                break
            if vm_no_rng.poll() is not None:
                break
            time.sleep(0.25)
        ntext = no_rng_log.read_text(errors="replace")
        assert "[FPU PROBE] A PASS" in ntext, "Legacy BUR v1 should still run without ASLR entropy:\n" + ntext[-3000:]
        assert "[BUR2 LAUNCH DENIED]" in ntext, "BUR v2 loader did not fail closed without hardware entropy:\n" + ntext[-3000:]
        assert "[BUR2 EXEC PASS]" not in ntext, "BUR v2 must NOT execute when trusted hardware entropy is absent!"
    finally:
        vm_no_rng.terminate()
        try:
            vm_no_rng.wait(timeout=5)
        except subprocess.TimeoutExpired:
            vm_no_rng.kill()
            vm_no_rng.wait()

    result = dict(
        executions=len(five_col_rows),
        unique_per_column=[len({r[c] for r in five_col_rows}) for c in range(5)],
        collision_counts_per_column=[len(five_col_rows) - len({r[c] for r in five_col_rows}) for c in range(5)],
        columns=["code", "stack", "heap", "canary_address", "shared_mapping"],
        layouts=five_col_rows,
        pte_audit=pte_audit,
        symbolicated_faults=symbolicated,
        legacy_fpu_fixture_sha256=hashlib.sha256((out / "fpu_probe.bin").read_bytes()).hexdigest(),
        canary_code_rodata_write_denied=True,
        rodata_data_bss_heap_stack_shm_execution_denied=True,
        guard_page_unmapped_fault_verified=True,
        ssp_contained=True,
        entropy_fail_closed_verified=True,
        kernel_sha256=hashlib.sha256((stage / "boot/system_operacyjny.bin").read_bytes()).hexdigest(),
    )
    (out / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(
        "BUR v2 guest execution PASS:",
        f"executions={result['executions']}",
        f"unique={result['unique_per_column']}",
        "PTE W^X+NX verified, 11 negative modes + fail-closed entropy verified!",
        flush=True,
    )


if __name__ == "__main__":
    main()
