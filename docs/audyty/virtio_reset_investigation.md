# VirtIO GPU reset investigation — 2026-09-28 continuation

Status: investigation incomplete; interactive validation requires host desktop
access and host process debugging. No production GPU/profile change has been
made in this continuation. Earlier workspace modifications are preserved.

## Established boundary

The previous session reached PCI BDF `00:06.0` (`virtio-vga`) after leaving
VirtIO RNG. Its reset transaction failed to return with IF=0. This identifies
a transaction boundary, but does not establish which host QEMU function blocks.
A host stack from that failure is still required for a QEMU root-cause claim.

## MMIO audit

`VirtioPciCommonCfgRegs::device_status` is one byte at offset `0x14`.
Compile-time assertions now check both properties. Inspection of the generated
kernel object confirms:

```asm
virtio_pci_get_status: movzbl 0x14(%rax),%eax
virtio_pci_set_status: mov %sil,0x14(%rax)
```

The latter instruction stores eight bits. Both accesses use explicit volatile
byte MMIO helpers; the write helper has a compiler memory barrier after the
store. BAR MMIO is identity mapped with PWT and PCD, and remains mapped through
reset. Capability parsing validates the mapped BAR range before initialization.

Observed in the first two headless A boots:

| Field | Value |
| --- | --- |
| BDF | `00:06.0` |
| Transport | modern PCI capabilities |
| Common-config BAR | 2 |
| BAR physical base | `0x000000C00000C000` |
| Capability offset | `0x1000` |
| Capability length | `0x800` |
| Common-config virtual/physical address | `0x000000C00000D000` |
| Device-status virtual/physical address | `0x000000C00000D014` |
| Access width | 8 bits |
| Old status | 0 |
| IF | 0 |
| Reset write | returned |

These addresses are observations for this firmware/topology, not constants to
hard-code. `0x14 < 0x800`; the address equals BAR base + capability offset +
`offsetof(device_status)`.

`virtio_pci_reset()` writes zero, reads byte status until zero, and stops after
10,000 polls. The unsigned counter cannot overflow at this bound. This is an
iteration guard, not a monotonic HPET deadline. It cannot bound a non-returning
MMIO instruction. The GPU driver's initialization also performs a second reset
by calling `virtio_pci_set_status()` directly, without a separate reset-completion
wait; diagnostics now cover that store too. Neither observation is established
as the cause of the earlier wedge.

## Diagnostic changes

`virtio_pci.cpp` and `virtio_pci.h`: disabled-by-default
`BURSZTYN_DIAG_VIRTIO_BOOT` traces report BAR/capability/status addresses, old
status, byte access, reset-store return, and modern capability regions. The old
status read has its own marker to distinguish a read stall from a write stall.

`narzedzia/reproduce_installer_virtio.py`: permits controlled GPU frontend and
legacy-interface comparisons, including omission of `disable-legacy`; QMP over
stdio; bounded QMP line reads; timeout `ps -L`, `/proc` thread collection and a
15-second GDB backtrace attempt before cleanup. Orphan reporting now includes
the launched PID if it survives. Sampling drift measures lateness against each
sample's deadline instead of accumulating normal loop overhead.

Installed QEMU supports GTK, SDL and headless displays. Its verified trace
events include `virtio_gpu_features`, `virtio_gpu_cmd_get_display_info`, and
`virtio_pci_notify_write`; the harness enables these and uses
`-d guest_errors,unimp` (the installed spelling of guest-errors logging).

## Measurements and environment limits

All experiments use `/tmp` disks and private OVMF copies. The real installation
disk is outside the experiment.

GTK A: five **launch errors**, zero guest attempts. QEMU reports
`gtk initialization failed`, with ALSA access denied as well. This is an
environment restriction, not a GPU reset result. There is no interactive wedge
to attach to in this environment.

Headless A: **2/5 installer readiness**, **2/5 GPU reset returned**; remaining
three attempts never entered the kernel and ended after GRUB's
`Ladowanie zasobow instalatora...`. QMP remained responsive on these timeouts.
All five cleaned up by QMP quit; orphan count zero. The disk SHA256 stayed
`254bcc3fc4f27172636df4bf32de9f107f620d559b20d760197e452b97453917`.
Peak observed QEMU RSS in the two successful boots was 369.8 MiB; early runs
overlapped other checks and are unsuitable for accelerator/host responsiveness
comparisons. Their older cumulative drift metric must not be interpreted as
desktop freeze duration.

A first overlapping headless B attempt timed out in GRUB before kernel entry.
It has been excluded from the sequential GPU comparison. `ps -L` and `/proc`
captures were saved before QMP cleanup. GDB attachment was denied:
`ptrace: Inappropriate ioctl for device`. This capture is not a stack from the
established GPU reset wedge.

Headless B sequential (`virtio-vga,xres=1280,yres=720`, with the legacy-policy
property omitted): **0/5 installer readiness**, **0/5 GPU reset reached**. All
five ended at GRUB's `Ladowanie zasobow instalatora...`; none tested the GPU
status write or provided a modern-binding observation. All five exited through
QMP quit; orphan count zero; disk SHA256 unchanged (same hash and disk as A).
Peak RSS 144.9 MiB and peak sampled CPU 207.2%. Minimum sampled available host
memory: 16,242.7 MiB (A: 15,912.5 MiB). These measurements do not establish
desktop responsiveness or a threading-policy preference.

Default-display probe: one launch error (`gtk initialization failed`), no guest
attempt. SDL probe: one timeout at `Ladowanie jadra...`, before kernel entry;
QMP quit, zero orphans, unchanged disk hash. SDL peak RSS 273.3 MiB and sampled
CPU 257.8%. This establishes SDL startup availability, not desktop acceptance.

The A/B readiness rates cannot establish causality: the A failures and all B
failures occur before the reset boundary. The B reset result is **not reached**,
not a non-returning MMIO write. No timeout has been counted as a pass.

Artifacts: `/tmp/bursztyn-reset-matrix-a`, `/tmp/bursztyn-reset-headless-a`,
`/tmp/bursztyn-reset-headless-b`, `/tmp/bursztyn-reset-headless-b-sequential`.

## Validation and remaining acceptance

`make check-kernel`: passed. Diagnostic `make iso`: passed.
`make test-virtio-pci`: the assertions ran, but LeakSanitizer failed under the
managed execution environment. Running the same ASan/UBSan binary with
`ASAN_OPTIONS=detect_leaks=0` passed **143 tests, zero failures**. Leak checking
is unverified here. QMP stdio smoke test passed: query-status, QMP quit, exit 0.
Scoped diff whitespace checks and Python compilation passed.
Full-tree `git diff --check` reports a pre-existing blank line at EOF in
`obrazy/dekoder_obrazu.h:58`; that unrelated user modification is preserved.
The latest diagnostic transport source also compiled separately with
`BURSZTYN_DIAG_VIRTIO_BOOT=1` after the additional capability and second-reset
markers were added. Matrix runs used the earlier diagnostic image with the
address/read/write-return markers; this distinction matters when comparing logs.

No cause has been established for `disable-legacy=on` versus omitted, display
backend, or TCG threading. No production Makefile cleanup or accelerator change
is justified yet. Required final variable names and profile cleanup remain
pending the causal comparison. The existing production GPU command still uses
`virtio-vga,disable-legacy=on,xres=1280,yres=720`.

The direct interactive installer and Live → Installer acceptance matrices are
unverified. Final image/installer/normal-boot/diff-boot regressions and installed
profile checks remain pending a production change. Desktop usability cannot be
measured through the blocked GTK frontend. Continue on a host terminal with
desktop access and ptrace permitted, using the same disposable harness and
topology. Do not substitute headless readiness for interactive acceptance.

## Latest continuation: staged timeouts and fresh host-access check

The requested real-host GTK run was attempted again after rebuilding
`kernel.o` and `virtio_pci.o` with `BURSZTYN_DIAG_VIRTIO_BOOT=1`, then rebuilding
the ISO. This is the flag spelling actually defined by this tree; the spelling
`BURSZTYNDIAGVIRTIOBOOT` does not enable the existing diagnostic build.
The built kernel contains the explicit kernel-entry and GPU byte-store markers.

The harness now has independent `--kernel-entry-timeout 180` and
`--ready-timeout 180` bounds. The latter starts when the explicit kernel-entry
serial marker is observed. Both CLI bounds must be at least 180 seconds.
Readiness without the marker or without modern GPU binding evidence is rejected.
Each attempt is classified as exactly one of:

- `PREKERNELTIMEOUT`: no diagnostic kernel-entry marker by the first deadline.
- `GPURESETHANG`: kernel entered; GPU BDF `00:06.0` logged its byte-store begin
  without the matching return before the readiness deadline.
- `BOOTPASS`: readiness reached, with kernel-entry and modern GPU binding logs.
- `RUNNERERROR`: launch/runner failure, premature QEMU exit, missing required
  diagnostic evidence, or kernel readiness timeout outside the reset-store
  boundary (the reason is recorded explicitly).

The timeout capture now additionally saves QEMU process status/stack files,
current CPU ticks/RSS, free host memory/swap, and a GPU trace extract before
QMP → TERM → KILL cleanup. The GDB and per-thread captures remain bounded.
Classification checks cover pre-kernel loading, unreturned GPU stores,
returned stores, repeated resets, and an unreturned RNG store.

Fresh GTK A full-profile launch at normal priority:
`RUNNERERROR`, 0.34 seconds, no guest serial output. QEMU stderr says
`gtk initialization failed`; ALSA reports `Operation not permitted`.
`xdpyinfo -display :0` also reports `unable to open display ":0"`, despite
the existence of `/tmp/.X11-unix/X0`. A separate paused QEMU control probe
again rejects GDB attachment with `ptrace: Inappropriate ioctl for device`.
That control probe exited through QMP quit. It is an access test, not a stack
from a GPU wedge. These are the actual current tool-session results; interactive
display/debugging access has not become available in this session.

Full-profile artifact directory:
`/tmp/bursztyn-gtk-staged-a-20260928`. Recorded orphan count: zero; disposable
disk SHA256 unchanged. The working installation disk was not used.
No new five-run A/B or single/multi result can be obtained from an immediate
GTK launch failure. The required direct Installer and Live → Installer
acceptance runs remain unexecuted. Blocking host function and root cause remain
unknown; production Makefile/GPU/accelerator settings remain unchanged.

Run the following sequentially in an execution session with functioning GTK
and GDB attachment. Use fresh artifact directories on every invocation:

```sh
python3 narzedzia/reproduce_installer_virtio.py --iterations 5 --display gtk,gl=off --accel tcg,thread=single --nice-level 0 --gpu-disable-legacy on --kernel-entry-timeout 180 --ready-timeout 180 --disk /tmp/bursztyn-gtk-causal.img --artifacts /tmp/bursztyn-gtk-causal-a-single
python3 narzedzia/reproduce_installer_virtio.py --iterations 5 --display gtk,gl=off --accel tcg,thread=single --nice-level 0 --gpu-disable-legacy auto --kernel-entry-timeout 180 --ready-timeout 180 --disk /tmp/bursztyn-gtk-causal.img --artifacts /tmp/bursztyn-gtk-causal-b-single
```

Then repeat the same A and B commands with `--accel tcg,thread=multi` and
distinct artifact directories. Keep the image, disk, firmware source, RAM, SMP,
display, priority, and PCI order unchanged. The `auto` experiment omits the
`disable-legacy` property entirely. Capture desktop usability independently
of guest correctness; sampled timer drift alone does not prove UI responsiveness.
