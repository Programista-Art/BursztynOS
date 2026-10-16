# HDA tone-stop and QEMU transport regressions — 2026-10-14

## Runtime evidence and scope

The user reports a loud continuous tone immediately after boot on a physical Acer laptop, where the new dynamic speaker route now produces sound. Windows QEMU reports Intel 8086:2668, initial STATESTS=0x0001 and detected CAD0, but no usable codec verbs, CORB/RIRB RUN=0, initialization 23, failure stage 18, transport status 12 and one recovery. The audio stream was never prepared in that QEMU failure. These are separate failures.

The existing dirty worktree already contained dynamic physical routing, amplifier/pin/EAPD programming, asynchronous tones and the NVIDIA CORBRP quirk. This task preserves that routing implementation. No input driver, pointer configuration, splash policy, network or installer change is part of the repair. Automatic startup audio is disabled in normal production boot, including physical hardware.

## A: exact cause of the endless Acer tone

The former `hda_test_ton` performed START → wait → STOP. The incoming code performed START → store HPET nanosecond deadline → return. Its only edges into `hda_obsluz_okresowo` were:

- `hda_ton_aktywny` → `hda_obsluz_okresowo`; no external production caller existed.
- `hda_wew_test_tick` → `hda_obsluz_okresowo`; its caller `hda_diag_test_tick` had no production callers. Only the host test called that diagnostic tick.

`kernel_main` called `hda_test_ton(880, 500)` during boot. Legacy BWS audio in `syscalls.cpp` was the other public tone caller. Neither supplied maintenance. Thus the deadline was never checked on the startup path. The old stop routine did clear RUN on the same global `hda_stream_base` that start used; it was not invoked. The controller could continually loop its cyclic BDL. This confirms the missing-stop hypothesis from the code, without claiming a new Acer hardware trace.

HPET is the only current nanosecond clock. `hpet_dostepny()` is established by ACPI mapping and a progressing-counter check; without HPET, the underlying tick reader returns zero. Previously a tone could be started without checking availability. Timed starts now reject unavailable HPET and deadline arithmetic overflow before RUN. Loss of clock readiness during playback requests Stop.

The production PID0 loop now calls `hda_wykonaj_prace_odroczona` before USB/input/compositor work. This invokes the real `hda_obsluz_okresowo` independently of user polling and diagnostic test ticks. `znajdz_nastepny_proces` includes PID0 in the regular round-robin schedule even when user processes are runnable. Timer IRQ additionally sets one atomic deferred-work flag. The C++ syscall wrapper consumes it before dispatch and after the implementation releases its locks. This provides service when GUI activity delays PID0. IRQ performs no HDA register, clock or lock operations; maintenance executes outside IRQ.

Public and diagnostic tone starts share the monotonic deadline and active state. Deadline, manual Stop, stream error, controller reset and clock loss all use one stop routine: clear SDnCTL.RUN, perform bounded hardware acknowledgement, read RUN back, then clear tone-active only after RUN=0 is confirmed. A failed acknowledgement retains active state and queues another stop on the next production pass. The deadline remains available for diagnostics. Controller reset notifies playback before clearing CRST. No tone-duration busy wait is restored.

The normal 250 Hz APIC timer has a 4 ms interval; with the existing 16-slot scheduler the nominal CPU-only round-robin interval is at most 64 ms. This OS does not preempt arbitrary Ring0 syscalls, so this is a deferred-service guarantee under normal scheduling, not a hard realtime guarantee through a hung kernel syscall. The runtime test allows 100 ms scheduling tolerance. Automatic physical startup audio remains disabled pending Acer validation.

## B: exact cause of the QEMU regression

Compared with the last working legacy transport in commit `244d898`, commit `b9e1fcd` moved command handling to the new transport state machine. Both paths deliberately selected IC for Intel 8086:2668 and stopped CORB/RIRB. Therefore RUN=0 in the supplied report was a policy decision, not evidence that a ring-start write failed or a partial reset forgot to restart already-running QEMU rings.

The concrete regression is the IC recovery dependency: the legacy timeout path cleared the QEMU absent-CAD busy state; the new recovery allowed that clear only when `info.test_dostepny` was already true. The new boot topology scan runs before routing config sets that flag. CAD0 can initially answer, then probing absent CAD1 leaves QEMU ICB busy; recovery sees false `test_dostepny`, fails and poisons subsequent codec reads. A preserved responsive mask can therefore coexist with a later failed scan and zero published codecs. The unchanged historical production state machine from `git show b9e1fcd:sterowniki/dzwiek/hda_transport.cpp` was executed against the faithful QEMU register model. It reproduced: initial READY=1, transport=IC, RUN=0/0, test_available=0; CAD0 success with 1AF4:0012; absent CAD1 timeout; then READY=0, init=23, stage=18, status=12, recoveries=1, ICIS=1. Evidence: `.hda-test/physical/transport-before-reproduction-20261014.log`. This is a historical-code reproduction on modeled hardware, not an actual pre-fix VM log.

| Report value | Actual enum |
| --- | --- |
| Initialization 23 | `HDA_INIT_BLAD` |
| Failure stage 18 | `HDA_BLAD_RESET_W_TRAKCIE` |
| Transport status 12 | `HDA_TRANSPORT_RESET_REQUIRED` |

Initial STATESTS=0x0001 is preserved before W1C acknowledgement and records CAD0 presence. Clearing that status does not remove the codec. Presence detection is independent of the command transport, which failed as above. No conclusion of missing hardware is drawn from current STATESTS=0.

QEMU now uses the normal CORB/RIRB path. It observes CRST and the post-reset codec interval, preserves codec status, stops rings, chooses supported sizes, validates aligned physical DMA addresses, performs the standard Intel CORBRP set/read/clear/read handshake, resets RIRBWP, clears stale ring status and programs RINTCNT. RIRB DMA starts before CORB DMA; both RUN bits are read back before READY or any verb. Failed writes retain exact before/after values in diagnostics and emit one bounded failure record. Register sequencing was checked against the [Intel HDA specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf).

Enabling rings also requires a specific QEMU detail: with RINTCNT=1, RIRBCTL must enable response status generation (value 3, rather than 2), so polling can acknowledge RIRBSTS.RINTFL and allow subsequent responses. INTCTL stays zero, so this adds no processor IRQ. The host model now implements QEMU response-count throttling and tests multiple consecutive verbs, rather than allowing an unrealistically permissive ring. This behavior is visible in the installed-version [QEMU 8.2.2 controller source](https://github.com/qemu/qemu/blob/v8.2.2/hw/audio/intel-hda.c#L287).

IC remains a bounded state machine, but cannot operate during reset or recovery. An unfinished IC transaction or lost CRST requires complete STOP/reset/reinitialization to the ring READY state. Recovery no longer depends on the playback-test availability flag. No forced-IC workaround is used.

The NVIDIA 0x10DE self-clearing CORBRP quirk remains an independently zero-initialized field in each controller. Both NVIDIA→Intel and Intel→NVIDIA discovery orders are covered; Intel 8086:2668 uses the standard handshake.

The legacy NID 0x02/0x03 fallback is gated by both Intel 8086:2668 and a positively read supported QEMU codec 1AF4:0012 or 1AF4:0022. Without a dynamic route, an unknown physical codec cannot reach those configuration verbs or mark tests available.

## Diagnostics and compatibility

Diagnostic ABI version is now 3 because controller and stream structures gained fields. BWS89/90 numbers and PZB restrictions remain unchanged. Version/size validation rejects incompatible requests; the application is rebuilt against the same schema.

The stream view and saved report contain `Tone active`, `Tone deadline ns`, `Tone stop reason`, and `Stream RUN after stop`. Reasons include deadline, manual Stop, stream error and controller reset, plus clock unavailable. Controller diagnostics include CORBCTL/RIRBCTL before and after their RUN writes.

## Verification

Host validation on the final application bytes:

| Check | Result |
| --- | --- |
| Production transport/scanner, exact QEMU and both NVIDIA/Intel orders | 49 scenarios / 11,287 checks PASS, ASan + UBSan |
| Actual playback driver + production PID0 and timer/syscall deferred entries | 13 scenarios / 103 checks PASS, ASan + UBSan |
| Diagnostic format/report | 241 checks PASS, ASan + UBSan |
| BWS permission/isolation | 148 checks PASS, ASan + UBSan |
| Actual loader/PSF/package, normal | 30 scenarios / 4,701,418 checks PASS, ASan + UBSan |
| Actual loader/PSF/package, trace | 30 scenarios / 4,701,423 checks PASS, ASan + UBSan |
| Actual application GUI/font/report | 29 scenarios / 2,662 checks PASS, ASan + UBSan |
| Actual BWS89/90 + scanner | 8 scenarios / 2,161 checks PASS, ASan + UBSan |
| App package/paths/font/size and final ISO bytes | 63 source/app checks + 7 ISO checks PASS |
| Executable size | 131,328 B <= 524,288 B; maximum +1 B rejected |
| Full `make -B -j2 all iso` and final incremental builds | PASS, exit 0 |

The combined repair script initially failed linking its last BWS fixture because the newly added reset callback lacked a test stub. That fixture was corrected and rebuilt with the same -O1/ASan/UBSan flags; its final standalone run passed. The already-passed loader and GUI binaries did not require recompilation for that fixture-only correction. All final components passed; the earlier link failure is retained in the log rather than reported as a successful combined invocation.

Logs: `.hda-test/physical/transport-canonical-20261014.log`, `.hda-test/regression/diag.log`, `app-repair.log`, `app-hda-bws-final.log`, `production-build.log`, `post-review-build.log`, `final-build.log`, `final-image-check.log`. Tone suite: `python3 narzedzia/test_hda_tone.py`. Transport suite: `python3 narzedzia/test_hda_physical.py`.

Host models alone are not proof of QEMU or physical Acer operation. Actual QEMU verification is described below.

## Modified source and test files

- `kernel.cpp`: remove normal startup playback; invoke deferred service in PID0; explicit QEMU regression boot option.
- `idt.cpp`, `syscalls.cpp`: timer schedules one atomic flag; syscall boundaries execute deferred maintenance outside IRQ and syscall implementation locks.
- `sterowniki/dzwiek/hda.cpp`, `hda.h`, `hda_wewnetrzny.h`: shared timed-stop state, fail-closed clock check, stop readback/retry/reasons, controller-reset notification and positively identified legacy fallback.
- `sterowniki/dzwiek/hda_transport.cpp`, `hda_transport.h`: QEMU rings, RUN verification, status acknowledgement and complete invalid-state recovery; host-only IC injection for negative testing.
- `sterowniki/dzwiek/hda_praca_odroczona.h`: common production deferred entry.
- `sterowniki/dzwiek/hda_qemu_regression.h`: explicit diagnostic observer; never supplies maintenance itself. Playback is allowed only for the known emulated controller/codec, with an explicit Multiboot test token. Normal boot never enters it.
- `sterowniki/dzwiek/hda_diag.cpp`, `hda_diag_format.h`, `bws_hda_diag.h`: ABI3 tone/readback diagnostics; diagnostic test state follows actual playback activity.
- `programy/test_dzwieku/test_dzwieku.cpp`: show added stream fields.
- `narzedzia/hda_host_model.h`, `test_hda_transport.cpp`: exact QEMU ring/count/recovery model and per-controller quirk ordering.
- `narzedzia/test_hda_tone.cpp`, `test_hda_tone.py`: actual production playback logic and deferred entry.
- `narzedzia/test_hda_regression_qemu.py`: actual QEMU UEFI/OVMF, isolated test ISO, WAV capture and null-backend verification.
- `narzedzia/test_hda_diag.cpp`, `test_dzwieku_hda_bws.cpp`: new diagnostic fields, ABI rejection and current-driver integration.
- `Makefile`: host tone and explicit actual-QEMU test targets.
- This report.

The incoming `hda_quirks.h` NVIDIA change was preserved, not redesigned. Generated `.o`, `.d`, `.su`, host binaries, images and logs are build artifacts, separate from source changes. Six pointer/input source hashes are checked in `.hda-test/regression/protected.sha256`; no input-driver edits are included.

## Physical Acer retest still required

No Acer hardware is attached to this environment. This task does not claim that physical playback or STOP has been verified.

1. Cold boot the new production ISO: no automatic tone must start.
2. Open Test dźwięku and run one controlled short tone; verify audible termination and the report showing Tone active=0, reason=deadline, Stream RUN after stop=0.
3. Run a tone and press Stop; confirm immediate silence, active=0, reason=manual Stop and RUN after stop=0. Repeat Stop to confirm idempotence.
4. Confirm the previously discovered speaker route still works, and save `/dane/diagnostyka/hda.txt`. A STOP readback failure must remain visible as active=1/RUN after stop=1 instead of reporting success.
5. Verify no tone resumes after returning to the desktop or leaving the app open. Keep automatic physical startup sound disabled until these results are confirmed.

## Actual QEMU attempts and timing limits

The local QEMU version is 8.2.2 (Linux host, TCG, one virtual CPU, 2 GiB RAM). No attached Acer laptop or Windows host is available.

Initial paired CODE/VARS UEFI and SeaBIOS CD attempts timed out before any kernel serial output. BIOS screenshots showed "Booting from DVD/CD..."; QMP recorded slow successful 2 KiB CD reads without errors. These runs contain no HDA evidence. They are preserved under `.hda-test/regression-qemu/initial-uefi-timeout/` and corresponding BIOS diagnostics.

Using the project's preferred monolithic `/usr/share/ovmf/OVMF.fd` and the same test ISO as a temporary read-only modern VirtIO block device reached the actual Bursztyn kernel. This changes only the isolated test VM boot medium, not production launchers, filesystem code or input configuration. The relative VirtIO keyboard/mouse and Intel HDA/hda-output identities remain unchanged.

Two deliberately early trials verified real CORB/RIRB RUN=1, CAD0 1AF4:0012, AFG1, one route and test availability, plus nonzero WAV PCM and eventual automatic RUN=0 with active=0/reason=deadline. They failed the test's 100 ms timing tolerance while still on the startup splash: the first observed Stop was about 484.5 ms late and WAV contained 0.95367 s of audio; the second trial after an arbitrary 3 s wait was still during boot (about 329 ms late; 0.77306 s of PCM). These failed timing cases are retained under `uefi-mono-virtio-busy-deadline-failure/` and `uefi-mono-virtio-3s-boot-deadline-failure/`.

This confirms that cooperative PID0 service has no hard deadline during long existing Ring0 startup/graphics work. It is a material timing limit, not a transport failure and not hidden by increasing tolerance. Normal automatic startup sound stays disabled. The explicit runtime fixture is now gated on the real desktop-ready latch and the end of splash, then a 3 s observation warmup. It still uses the normal deferred service, the original 100 ms tolerance and an independent before-deadline RUN check.

## Completed verification after session resume — 2026-10-15

The final kernel includes the timer flag and syscall-boundary service described above. `make -j4` passed. Transport/scanner (49 scenarios, 11,287 checks), diagnostics (241 checks), BWS isolation (148 checks) and the expanded production-tone fixture (13 scenarios, 103 checks) passed again under ASan/UBSan. The added tone case exercises timer flag coalescing, confirms IRQ scheduling alone leaves RUN unchanged, and verifies the syscall deferred entry stops the stream and records the stop timestamp.

Actual command:

```sh
python3 narzedzia/test_hda_regression_qemu.py --boot-medium virtio --timeout 180 --also-none
```

Both independent UEFI VM boots passed with the original 100 ms tolerance:

| Evidence | WAV backend | Null backend |
| --- | --- | --- |
| Controller / initial STATESTS | 8086:2668 / 0x0001 | 8086:2668 / 0x0001 |
| CORB/RIRB size and RUN | 256/256, RUN=1/1 | 256/256, RUN=1/1 |
| CAD0 / codec count | 1AF4:0012 / 1 | 1AF4:0012 / 1 |
| Audio function group / route / test | AFG1 / 1 route / available | AFG1 / 1 route / available |
| Before deadline | RUN=1, active=1 | RUN=1, active=1 |
| After deadline | RUN=0, active=0, readback=0, deadline | RUN=0, active=0, readback=0, deadline |
| Observation after deadline | 11,731,060 ns | 9,856,820 ns |

Real WAV DMA produced 44,418 nonzero samples at 44.1 kHz stereo, with an active span of 0.5037415 s. The kernel SHA256 is `ec5893e780528aa4e98eb48422499cc4d18b37aead27b79ce6393131d2cb73fb`. Machine-readable evidence and PCM are `.hda-test/regression-qemu/result.json`, `wav-serial.log`, `none-serial.log` and `tone.wav`. The observer calls no diagnostic test tick to stop playback.

The historical-state-machine reproduction was rerun and again produced exactly init=23, stage=18, status=12, one recovery, ICIS=1. Final host logs are `.hda-test/regression/resumed-{build,tone,transport,diag,app,qemu,iso}.log`.

The full `make test-dzwieku-repair test-dzwieku-packaging` invocation now also passes end to end: both loader variants, actual GUI, actual BWS89/90 and package audit. `make iso` passed, and `python3 narzedzia/test_dzwieku_packaging.py --image` passed all 70 checks, including extraction and byte-for-byte comparison of the ISO kernel with the tested kernel. Final `BursztynOS.iso` SHA256: `c26019a32beb026fd134e265779c5d705e43db044ef8015d2a8d0c9b7ed2aae9`. The six preserved input-source hashes still match. Evidence: `.hda-test/regression/resumed-final-image.log` and `protected.sha256`.

These are Linux-host QEMU results. They verify the same emulated controller with the Windows launcher's null audio backend; the Windows host itself and physical Acer remain untested. Earlier startup timing failures remain recorded above. Automatic startup audio stays disabled.
