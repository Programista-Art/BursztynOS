# Bursztyn OS — Runtime Security Hardening: Compiler Stack Protection, BWS/BUR/BSP2 Fuzzing Infrastructure, Memory-Safety Fixes & BUR v2 ASLR Architecture

**Date:** 2026-11-04
**Baseline Commit:** `63bbd43d251be1e30dada0ddf1c8d79888324edf`
**Target Architecture:** x86-64 Freestanding (Ring 0 Kernel + Ring 3 Native `.bur` Applications)
**Compiler Toolchain:** `x86_64-linux-gnu-g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`

---

## 1. Executive Summary

Following the previous release reverse-engineering exposure audit (which introduced stripped public kernels, private DWARF symbol archives, removal of bundled mbedTLS test private keys, and automated release metadata verification), this milestone transitions **Bursztyn OS** to **active runtime exploit mitigation, coverage-guided fuzzing, and memory-safety hardening** while preserving 100% of its independent, non-POSIX x86-64 architecture:

1. **Compiler Stack Protection (`-fstack-protector-strong -mstack-protector-guard=global`)** was designed, implemented, and enabled across the freestanding Ring 0 kernel (**163 objects, 614 instrumented functions out of 2,266**) and all **23 Ring 3 native `.bur` applications** (**2,235 instrumented functions out of 13,148**).
2. **Cryptographic & Hardware Entropy Guard Seeding:**
   - **Ring 0:** Initialized in [`stack_protector_early_init`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stack_protector.cpp#L69-L108) called from [`boot.S`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/boot.S#L118-L125) *before* `kernel_main` executes a single C++ stack frame, mixing `RDRAND`/`RDSEED`, `RDTSC`, CMOS RTC registers, and SplitMix64 with a forced low-byte NUL (`0x00`) to stop unbounded C-string overflows.
   - **Ring 3:** Per-process random canaries generated in [`loader_utworz_proces_z_obrazu`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/loader.cpp#L688-L716) via `entropia_pobierz_bajty` (ChaCha20 CSPRNG) + `RDTSC` + SplitMix64 and written directly into the new process's private `.data` page at [`RING3_SSP_GUARD_VA` (`0x0000000001031000`)](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stack_protector.h#L27).
3. **Controlled Failure Containment (Verified in Host & QEMU Tests):**
   - **Ring 0:** [`__stack_chk_fail`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stack_protector.cpp#L117-L162) disables interrupts (`cli`), prevents recursion via an atomic latch, emits bounded serial (`COM1`) and framebuffer diagnostics (`STACK_PROTECTOR_VIOLATION`) without leaking the canary value, and halts all CPUs.
   - **Ring 3:** [`__stack_chk_fail`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/ring3_ssp.cpp#L20-L36) invokes `BWS_ZAKONCZ_PROCES(BWS_EXIT_STACK_PROTECTOR_VIOLATION)` (`0xB052535350464149ULL`), allowing [`syscalls.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/syscalls.cpp#L3505-L3531) to log `[SSP-RING3] STACK_PROTECTOR_VIOLATION` and cleanly terminate only the offending process while the kernel and sibling Ring 3 apps continue running.
4. **Coverage-Guided Fuzzing Suite (7 Targets, ASan + UBSan + `trace-pc`):**
   - Built an in-process mutation + dictionary + SanitizerCoverage (`__sanitizer_cov_trace_pc`) fuzzing engine ([`fuzz_engine.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_engine.h)) and 7 targets covering all **93 BWS syscalls**, the **BUR v1 executable & `.cebula` package loader**, and **5 BSP2/PSF filesystem surfaces** (superblock, inodes, directories, paths/slots, and stateful operations).
   - Executed **165,115 extended fuzzing iterations** exploring **5,042 coverage edges** with **0 unhandled crashes**.
5. **6 Real Security & Memory-Safety Vulnerabilities Discovered and Fixed:**
   - **Bug #1 (`syscalls.cpp`):** Unauthenticated Ring 3 kernel stack-overflow panic trigger (`BWS 99` with `arg1 == 0xDEADBEEFCAFEULL`) reachable by any `PZB_PIASKOWNICA` process.
   - **Bug #2 (`psf.cpp`):** Silent 160-byte buffer truncation and missing pre-translation `bezpieczna_sciezka` validation in `psf_przetlumacz_sciezke_slotu` for A/B slot paths (`/programy`, `/system`).
   - **Bug #3 (`psf.cpp`):** Missing directory cycle check for `id_korzenia` in `waliduj_katalog` and missing duplicate/aliasing block check involving `blok_posredni_1` / `blok_posredni_2` in `wskazniki_pliku_poprawne`.
   - **Bug #4 (`psf.cpp` & `psf_usuwanie.inc`):** 256-slot LRU data block cache eviction of `blok_posredni_1` / `blok_posredni_2` while freeing `> 256` data blocks of a large file (`> 490` blocks), which corrupted the indirect pointer table mid-loop and freed block 0.
   - **Bug #5 (`loader.cpp` — found by `fuzz_bur_loader`):** Missing NUL-termination of `out_manifest->wersja[32]` in `pakiet_parsuj_manifest_tekst` when `wersja` in `opis.aplikacji` was `>= 32` characters, plus uninitialized output struct on early error return.
   - **Bug #6 (`syscalls.cpp` — found by `test_bws_abuse_qemu.py`):** `bws_rozmiar_pliku` (`BWS_ROZMIAR_PLIKU = 44`) unconditionally returned `rozmiar_pliku(sciezka) + 1ULL` (`1ULL`) even when `sciezka` was invalid (e.g. `"/../../cytaty.txt"`), non-existent, or a directory, causing Ring 3 `pobierz_rozmiar_pliku()` to report success (`size = 0`) on invalid/traversal paths.
6. **BUR v2 & Ring 3 ASLR Technical Architecture & Prototype:**
   - Implemented [`bur_v2.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/bur_v2.h) (header validation, relocation table bounds, 28-bit page-aligned code/data/stack ASLR slide generator, W^X enforcement, and BUR v1 backward compatibility) and verified it under ASan+UBSan in [`test_bur_v2_aslr_host.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_bur_v2_aslr_host.cpp).

---

## 2. Phase 1 — Baseline Security Architecture

Prior to making changes, the baseline state at commit `63bbd43d251be1e30dada0ddf1c8d79888324edf` was audited and recorded in [`docs/security-hardening-2026-11-04/baseline.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/security-hardening-2026-11-04/baseline.json):

| Security Control / Property | Baseline Status | Source Location |
| :--- | :--- | :--- |
| Compiler Stack Protector (Ring 0 & Ring 3) | **Disabled** (`-fno-stack-protector`) | [`Makefile`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/Makefile#L15-L21) |
| `IA32_EFER.NXE` (No-Execute Bit) & `CR0.WP` | **Enabled** in 64-bit paging init | [`vmm.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/vmm.cpp#L430-L470) |
| W^X Enforcement (Kernel & Ring 3 `.bur`) | **Enforced** (`.text` RX, `.rodata` RO+NX, `.data`/`.bss`/stack RW+NX) | [`linker.ld`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/linker.ld), [`loader.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/loader.cpp#L600-L710) |
| Ring 0 Kernel Stacks | **64 KiB per process** (`0xFFFF940000000000` window) + unmapped lower/upper 4 KiB guard pages + bottom 64-bit software canary | [`stos_jadra.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stos_jadra.h), [`stos_jadra.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stos_jadra.cpp) |
| Ring 3 Process Address Spaces | **Private PML4 per process**; fixed virtual layout (`0x01001000` `.text`, `0x01031000` `.data`, `0x10000000..0x20000000` heap, `0x7FFFF0000000` stack + guard page) | [`pamiec.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/pamiec.h), [`vmm.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/vmm.cpp) |
| BWS Pointer Validation | Canonical + user-window + per-page PTE `PAGE_PRESENT | PAGE_USER` (+ `PAGE_WRITABLE` for writes) | [`syscalls.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/syscalls.cpp#L334-L450) |
| PZB / BZL Privilege Model | 6 trust levels (`PZB_JADRO=0` .. `PZB_PIASKOWNICA=5`) + 64-bit capability mask (`PRAWO_*`) + monotonic child privilege bounding | [`pzb.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/pzb.h), [`bezpieczenstwo.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/bezpieczenstwo.cpp) |
| System Entropy Subsystem | ChaCha20 CSPRNG seeded from `RDRAND`/`RDSEED`, `RDTSC`, VirtIO-RNG, RTC, and IRQ jitter | [`entropia.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/entropia.h), [`bezpieczenstwo.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/bezpieczenstwo.cpp) |
| Ring 3 ASLR | **Not present in BUR v1** (fixed link addresses in `app_linker.ld`) | [`app_linker.ld`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/app_linker.ld) |

---

## 3. Phase 2 — Compiler Stack Protection Design & Freestanding ABI

### 3.1 Toolchain ABI Inspection (`GCC 13.3.0`)
By default on `x86_64-linux-gnu-g++`, `-fstack-protector-strong` emits `%fs:0x28` TLS-relative canary loads (`-mstack-protector-guard=tls`). Because Bursztyn OS is a freestanding kernel and custom Ring 3 environment that does **not** use a glibc `fs:0x28` TCB layout (and `swapgs` is not used on kernel entry), using default `%fs:0x28` would dereference unmapped or arbitrary memory at `%fs:0x28`.

Passing `-fstack-protector-strong -mstack-protector-guard=global` instructs GCC 13.3 to load the canary from the external symbol `__stack_chk_guard` and call `__stack_chk_fail` on mismatch:
- **In Ring 0 (`-mcmodel=large`):**
  ```asm
  movabs $__stack_chk_guard, %rax
  mov    (%rax), %rax
  mov    %rax, -0x18(%rbp)
  ...
  mov    -0x18(%rbp), %rdx
  movabs $__stack_chk_guard, %rax
  sub    (%rax), %rdx
  jne    1f
  ...
  1: movabs $__stack_chk_fail, %rax
  call   *%rax
  ```
- **In Ring 3 (`app_linker.ld`):**
  ```asm
  mov    __stack_chk_guard(%rip), %rax
  mov    %rax, -0x8(%rbp)
  ...
  mov    -0x8(%rbp), %rdx
  sub    __stack_chk_guard(%rip), %rdx
  jne    1f
  ...
  1: call __stack_chk_fail
  ```
No undefined references to `__stack_chk_fail_local` are emitted (because `-fno-pic -fno-pie` is used), though [`stack_protector.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stack_protector.cpp#L164-L166) and [`ring3_ssp.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/ring3_ssp.cpp#L38-L40) provide `__stack_chk_fail_local` forwarding to `__stack_chk_fail` for completeness.

---

## 4. Kernel (Ring 0) Stack Guard Implementation

- **Files:** [`stack_protector.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stack_protector.h), [`stack_protector.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stack_protector.cpp), [`boot.S`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/boot.S#L118-L125).
- **Early Boot Initialization Sequence:**
  1. In [`boot.S`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/boot.S#L118-L125), immediately after entering 64-bit long mode (`long_mode_start`) and setting up the 64 KiB boot stack (`stack_top`), `boot.S` saves the Multiboot2 `magic` (`%rdi`) and `info` (`%rsi`) registers in `%r12`/`%r13` and calls `stack_protector_early_init`.
  2. [`stack_protector.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stack_protector.cpp) is compiled with `-fno-stack-protector` so `stack_protector_early_init` and `__stack_chk_fail` never check the canary while initializing or reporting it.
  3. `stack_protector_early_init` harvests hardware entropy directly from CPUID-verified `RDSEED` / `RDRAND`, two `RDTSC` samples, CMOS RTC time/date registers (`0x70`/`0x71`), and the stack pointer, diffuses them through `SplitMix64`, clears byte 0 (`guard &= ~0xFFULL`) to act as a C-string NUL terminator, and ensures the upper 7 bytes are non-zero and never equal to the static fallback constant.
  4. Only 5 kernel objects are excluded from `-fstack-protector-strong`: `boot.o`, `przerwania.o`, `syscall.o`, `ring3.o` (pure assembly entry/ISR/SYSCALL stubs), and `stack_protector.o` (the SSP runtime itself).
- **Intermediate Per-Kernel Design & Context-Switch Safety:**
  - Because `scheduler.cpp` switches threads both from timer IRQs (`przerwania.S`) and cooperative `sched_yield()` while C++ frames are active on per-process kernel stacks, mutating a single global `__stack_chk_guard` on context switch would cause false-positive `STACK_PROTECTOR_VIOLATION` panics when a thread resumes and executes its function epilogue. Therefore, Ring 0 uses a single hardware-seeded per-boot randomized `__stack_chk_guard` complemented by the existing per-process 64 KiB kernel stack guard pages (`P=0`) and bottom-of-stack software canaries (`stos_jadra.cpp`).
- **Ring 0 Failure Handler (`__stack_chk_fail`):**
  - Marked `extern "C" [[noreturn]] __attribute__((noinline, cold))`.
  - Immediately executes `cli`, sets an atomic recursion latch (`g_ssp_panic_aktywny`), emits a bounded `STACK_PROTECTOR_VIOLATION` diagnostic to `COM1` (`0x3F8`) and the graphical panic screen (`ekran_panika_stosu` / `wypisz_log`) including `caller_rip`, `rsp`, `pid`, and `last_bws` **without printing `__stack_chk_guard`**, and enters an infinite `cli; hlt` loop.

---

## 5. Ring 3 Stack Guard Implementation

- **Files:** [`ring3_ssp.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/ring3_ssp.cpp), [`app_linker.ld`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/app_linker.ld#L21-L26), [`loader.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/loader.cpp#L688-L716), [`syscalls.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/syscalls.cpp#L3505-L3531).
- **BUR v1 Compatible Per-Process Canary Seeding:**
  - In [`app_linker.ld`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/app_linker.ld#L21-L26), `.data` is placed at `0x0000000001031000` (`RING3_SSP_GUARD_VA`) and begins with `KEEP(*(.data.ssp_guard))`, pinning `uintptr_t __stack_chk_guard` at the exact first 8 bytes of every Ring 3 `.bur` application's `.data` segment without altering the 64-byte `NaglowekBur` v1 header format.
  - During process creation in [`loader_utworz_proces_z_obrazu`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/loader.cpp#L688-L716), after mapping and copying the `.bur` segments into the new process's private PML4 address space, the kernel generates a fresh per-process 64-bit canary via [`stack_protector_losuj_kanarek_procesu(pid)`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/stack_protector.cpp#L96-L108) (using `entropia_pobierz_bajty` ChaCha20 CSPRNG + `RDTSC` + SplitMix64 + low-byte `0x00`), stores it in `p.kanarek_ssp_user`, and writes it directly to the physical page backing `0x0000000001031000` in the child's PML4 before `_start` runs.
- **Threat Model & Placement Note:**
  - Each Ring 3 process receives a distinct canary unknown to sibling processes. Because BUR v1 uses `-mstack-protector-guard=global`, `__stack_chk_guard` resides at the start of `.data` (`RW+NX`). This protects against linear stack buffer overflows without requiring `%fs`/`%gs` segment base switching;BUR v2 can optionally move the guard page to a dedicated read-only page (`RO+NX`) mapped by the loader after seeding.
- **Ring 3 Failure Containment:**
  - When a Ring 3 function detects stack corruption, [`__stack_chk_fail`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/ring3_ssp.cpp#L20-L36) invokes `syscall` with `rax = BWS_ZAKONCZ_PROCES (10)` and `rdi = BWS_EXIT_STACK_PROTECTOR_VIOLATION (0xB052535350464149ULL)` in an uninterruptible loop (`ud2` fallback).
  - In [`syscalls.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/syscalls.cpp#L3505-L3531), `BWS_ZAKONCZ_PROCES` recognizes `BWS_EXIT_STACK_PROTECTOR_VIOLATION`, logs `[SSP-RING3] STACK_PROTECTOR_VIOLATION: proces zakonczony awaryjnie (ochrona stosu Ring 3)` along with the PID and executable path (never logging the canary value), closes the process's GUI windows and resources, and terminates only that process.

---

## 6. Stack Protector Coverage and Binary Impact

Full machine-readable coverage is stored in [`docs/security-hardening-2026-11-04/stack-protector-coverage.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/security-hardening-2026-11-04/stack-protector-coverage.json), and before/after binary metrics are stored in [`docs/security-hardening-2026-11-04/after.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/security-hardening-2026-11-04/after.json).

### 6.1 Instrumentation Coverage Summary
| Target Domain | Protected Binaries / Objects | Total Functions | Instrumented Functions (`__stack_chk_fail`) | Coverage Ratio |
| :--- | ---: | ---: | ---: | ---: |
| **Ring 0 Kernel (`system_operacyjny.bin`)** | 163 / 168 objects (102 with checks) | 2,266 | **614** | **27.10%** |
| **Ring 3 Native Apps (23 `.bur` apps)** | 23 / 23 apps (100% of apps) | 13,148 | **2,235** | **17.00%** |
| **Combined Total** | **186 binaries/objects** | **15,414** | **2,849** | **18.48%** |

Representative heavily-instrumented kernel modules include `siec.o` (48/62 functions), `psf.o` (33/80 functions), `syscalls.o` (46/126 functions), `kernel.o` (33/93 functions), `grafika.o` (31/122 functions), `skladacz_obrazu.o` (25/67 functions), `aktualizacja.o` (17/38 functions), and `loader.o` (14/44 functions).

### 6.2 Kernel & ISO Size Impact
| Section / Artifact | Baseline Size (Bytes) | Hardened Size (Bytes) | Delta (Bytes) | Delta (%) |
| :--- | ---: | ---: | ---: | ---: |
| Kernel `.boot` | 80 | 80 | 0 | 0.00% |
| Kernel `.text` | 970,896 | 1,012,240 | +41,344 | +4.26% |
| Kernel `.rodata` | 295,824 | 296,272 | +448 | +0.15% |
| Kernel `.data` (includes embedded `.bur` blobs) | 8,900,192 | 8,978,016 | +77,824 | +0.87% |
| Kernel `.bss` | 3,047,008 | 3,047,008 | 0 | 0.00% |
| Stripped Public Kernel (`isodir/boot/system_operacyjny.bin`) | 10,182,736 | 10,301,520 | +118,784 | +1.17% |
| Production ISO (`BursztynOS.iso`) | 39,974,912 | 40,093,696 | +118,784 | +0.30% |

---

## 7. Phase 3 — Stack Protector Failure Verification (Host & QEMU)

1. **Host Unit & Death Tests ([`narzedzia/test_stack_protector_host.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_stack_protector_host.cpp), `make test-stack-protector-host`):**
   - Verifies non-zero randomness, low-byte `0x00` C-string terminator invariant, and distinctness across 64 consecutive generated canaries under ASan+UBSan.
   - Compiles a helper with `-fstack-protector-strong` and triggers a deliberate 64-byte stack buffer overflow in a `fork()`ed child process, verifying that `__stack_chk_fail` intercepts the corrupted return frame, outputs `STACK_PROTECTOR_VIOLATION` without leaking the canary hex value, and exits with code `134`.
2. **Disposable QEMU Ring 3 Containment & Ring 0 Fail-Stop Tests ([`narzedzia/test_stack_protector_qemu.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_stack_protector_qemu.py), `make test-stack-protector-qemu`):**
   - **Ring 3 Test:** Temporarily builds a disposable ISO where `/programy/test.cebula/test.bur` overflows a 32-byte `volatile char` stack buffer by 96 bytes inside an `[[gnu::noinline]]` function. Booting in QEMU verifies:
     - `[SSP-RING3] STACK_PROTECTOR_VIOLATION: proces zakonczony awaryjnie (ochrona stosu Ring 3)` is logged on `COM1`.
     - The secret canary value is **not** leaked in the serial log.
     - No Ring 0 panic occurs; the kernel continues running `[AUTOMATED TEST] Wszystko dziala poprawnie!`.
   - **Ring 0 Test:** Temporarily compiles a disposable kernel with `-DBURSZTYN_DEBUG_STACK_OVERFLOW_BWS=1` and triggers a Ring 0 stack buffer overflow in `syscalls.cpp`. Booting in QEMU verifies that Ring 0 catches the corruption in epilogue, logs `[PANIKA-SSP] STACK_PROTECTOR_VIOLATION w Ring 0 (jadro zatrzymane).` without leaking the canary, and halts cleanly without returning to the corrupted address.

---

## 8. Phase 4 — BWS Fuzzing Infrastructure & QEMU Abuse Testing

- **Machine-Readable Inventory & Privilege Matrix:**
  - Generated via [`narzedzia/generate_bws_inventory.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/generate_bws_inventory.py):
    - [`docs/security-hardening-2026-11-04/bws-inventory.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/security-hardening-2026-11-04/bws-inventory.json) (all **93 public BWS operations** `1..93`, handler names, pointer/length arguments, PZB trust levels, and `PRAWO_*` capability masks).
    - [`docs/security-hardening-2026-11-04/bws-privilege-matrix.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/security-hardening-2026-11-04/bws-privilege-matrix.json) (`6 PZB levels x 93 BWS syscalls` authorization matrix).
- **Host Coverage-Guided Fuzzer ([`narzedzia/fuzz/fuzz_bws.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bws.cpp)):**
  - Ran **50,023 iterations** under ASan+UBSan+`trace-pc`, expanding the seed corpus from `21 -> 160` inputs across **224 coverage edges** with **0 crashes**, plus deterministic verification across all PZB levels (`0..7`) and BWS IDs (`0..100`).
- **QEMU Ring 3 Adversarial BWS Abuse Suite ([`narzedzia/test_bws_abuse_qemu.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_bws_abuse_qemu.py), `make test-bws-abuse-qemu`):**
  - Runs inside a disposable QEMU VM and exercises 6 adversarial categories from Ring 3:
    1. Out-of-range syscall numbers (`0`, `94`, `99` with `0xDEADBEEFCAFEULL`, `0xFFFFFFFF`, `UINT64_MAX`) -> all return `UINT64_MAX`.
    2. Invalid pointers (`NULL`, kernel high-half `0xFFFFFFFF80100000`, non-canonical `0x0001000000000000`, `UINT64_MAX`, stack guard page `0x00007FFFEFFFF000`, unmapped above-stack page `0x00007FFFF0010000`) -> all rejected cleanly without kernel fault.
    3. Integer overflow & extreme buffer lengths (`0`, `UINT32_MAX`, `0x80000000`) -> all rejected.
    4. Heap `sbrk` overflow (`0x7FFFFFFFFFFFFFFF`, `0x20000000`, negative `int64_t`) -> all rejected (`0`).
    5. Unprivileged `BWS_RESET`, `BWS_SHUTDOWN`, `BWS_OTA_POLECENIE`, and `BWS_URUCHOM_PROGRAM` escalation to `PZB_JADRO` -> all denied.
    6. Path traversal (`"/../../cytaty.txt"`) across `czytaj_plik`, `pobierz_rozmiar_pliku`, and `pobierz_metadane_pliku` -> uncovered **Real Bug #6** in `bws_rozmiar_pliku`, which was fixed and verified in QEMU (`[BWS-QEMU] PASS: All 6 categories of BWS Ring 3 abuse tests passed in QEMU!`).

---

## 9. Phase 5 — BUR Loader Fuzzing and Hardening

- **Fuzzer ([`narzedzia/fuzz/fuzz_bur_loader.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bur_loader.cpp)):**
  - Exercises `loader_waliduj_naglowek_bur`, `loader_waliduj_obraz_bur`, and `pakiet_parsuj_manifest_tekst` with mutated `.bur` headers, overlapping segments, non-page-aligned offsets, huge `bss_size`, and malformed `opis.aplikacji` manifests.
  - Ran **50,021 iterations** under ASan+UBSan+`trace-pc`, expanding corpus from `19 -> 170` seeds across **416 coverage edges** with **0 crashes** after fixing **Bug #5**.

---

## 10. Phase 6 & 7 — BSP2/PSF Filesystem Fuzzing Infrastructure

Five dedicated BSP2/PSF fuzzers were built under [`narzedzia/fuzz/`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz) and orchestrated via [`narzedzia/run_fuzz_suite.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/run_fuzz_suite.py) (`make fuzz-smoke` / `make fuzz-extended`):

| Fuzz Target | Surface Under Test | Extended Executions | Corpus Growth | Unique Edges | Crashes |
| :--- | :--- | ---: | ---: | ---: | ---: |
| [`fuzz_bws`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bws.cpp) | 93 BWS syscalls, pointer/range/PZB validation | 50,023 | 21 -> 160 | 224 | 0 |
| [`fuzz_bur_loader`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bur_loader.cpp) | `NaglowekBur` v1 image & `.cebula` manifest parser | 50,021 | 19 -> 170 | 416 | 0 |
| [`fuzz_bsp2_superblock`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_superblock.cpp) | Superblock geometry, bitmap/inode/data layout, mount refusal on corruption | 10,025 | 25 -> 30 | 557 | 0 |
| [`fuzz_bsp2_inode`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_inode.cpp) | Inode metadata, direct + `blok_posredni_1`/`2`/`3` pointers, block aliasing | 10,012 | 12 -> 53 | 1,228 | 0 |
| [`fuzz_bsp2_dir`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_dir.cpp) | Directory entries, missing NULs, duplicates, cycles, link count mismatches | 10,012 | 12 -> 29 | 890 | 0 |
| [`fuzz_bsp2_path`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_path.cpp) | Path parser, UTF-8 names, `..`/`//`, A/B slot translation (`/sloty/a`, `/sloty/b`) | 25,021 | 21 -> 68 | 566 | 0 |
| [`fuzz_bsp2_ops`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_ops.cpp) | Stateful create/write/read/rename/move/copy/delete & LRU cache eviction | 10,001 | 1 -> 201 | 1,161 | 0 |
| **Total** | **All 7 Fuzz Targets (`fuzzing-metrics.json`)** | **165,115** | **111 -> 711** | **5,042** | **0** |

---

## 11. Phase 8 — Real Bugs Discovered and Fixed

### Bug #1 — Unauthenticated Ring 3 Kernel Stack-Overflow Panic Trigger in `syscalls.cpp`
- **Location:** [`syscalls.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/syscalls.cpp#L4036-L4048) (`obsluga_wywolan_systemowych`)
- **Vulnerability:** Any unprivileged Ring 3 process (including `PZB_PIASKOWNICA` with `uprawnienia = 0`) could invoke undocumented syscall `nr_funkcji == 99` with `arg1 == 0xDEADBEEFCAFEULL`, triggering `test_przepelnienia_stosu_jadra(64)` and immediately crashing the entire operating system via a Ring 0 kernel stack overflow.
- **Fix:** Gated the debug trigger behind `#if defined(BURSZTYN_DEBUG_STACK_OVERFLOW_BWS) && BURSZTYN_DEBUG_STACK_OVERFLOW_BWS` (disabled in all production builds) AND required `operacja_systemowa_dozwolona(proces)`. In production builds, `BWS 99` falls through to `default: return UINT64_MAX;`.
- **Regression Tests:** Verified in [`fuzz_bws.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bws.cpp#L390) and in QEMU via [`test_bws_abuse_qemu.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_bws_abuse_qemu.py).

### Bug #2 — Silent 160-Byte Path Truncation & Missing Pre-Translation Validation in `psf_przetlumacz_sciezke_slotu`
- **Location:** [`psf.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/psf.cpp#L2181-L2235) (`psf_przetlumacz_sciezke_slotu`, `rozwiaz_sciezke`)
- **Vulnerability:** When A/B slot translation (`/sloty/a` or `/sloty/b`) was enabled, `psf_przetlumacz_sciezke_slotu` formatted the translated path into a hardcoded 160-byte buffer (`for (; k < 159 && s[i]; ++i, ++k)`), silently truncating valid paths between 160 and 503 bytes and potentially resolving to a different parent directory or file! Furthermore, it accessed `s[1..9]` before `bezpieczna_sciezka(s)` validated bounded NUL termination.
- **Fix:** Added `if (!bezpieczna_sciezka(s)) return nullptr;` at entry to `psf_przetlumacz_sciezke_slotu`, expanded translation buffers to `MAKS_DLUGOSC_SCIEZKI` (512 bytes), and made overflow fail-closed (`if (pref_len + s_len >= MAKS_DLUGOSC_SCIEZKI) return nullptr;`).
- **Regression Test:** Verified in [`fuzz_bsp2_path.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_path.cpp#L42-L86) (`run_deterministic_bug2_regression`).

### Bug #3 — Missing Indirect Block Aliasing Validation & Root Directory Cycle Check in `psf.cpp`
- **Location:** [`psf.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/psf.cpp#L2627-L2635) (`waliduj_katalog`) & [`psf.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/psf.cpp#L3277-L3396) (`wskazniki_pliku_poprawne`)
- **Vulnerability:**
  1. `waliduj_katalog` checked `wpisy[j].id_wezla == id_katalogu` (self-loop) but did not reject directory entries pointing to `dysk_superblok->id_korzenia`, allowing a subdirectory entry to link back to `/` and create a cycle in the directory tree.
  2. `wskazniki_pliku_poprawne` checked uniqueness among direct blocks and among L1 entries, but did not check whether `blok_posredni_1` or `blok_posredni_2` aliased each other, aliased a direct block, or appeared inside the indirect pointer tables.
- **Fix:** Added `wpisy[j].id_wezla == dysk_superblok->id_korzenia` rejection in `waliduj_katalog` and full cross-table uniqueness/aliasing validation in `wskazniki_pliku_poprawne`.
- **Regression Tests:** Verified in [`fuzz_bsp2_inode.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_inode.cpp) and [`fuzz_bsp2_dir.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_dir.cpp).

### Bug #4 — 256-Slot LRU Block Cache Eviction Corrupting Indirect Block Tables During Large File Deletion
- **Location:** [`psf.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/psf.cpp#L209-L258) (`zwolnij_wszystkie_bloki_wezla`, `ustaw_wskaznik_bloku`) & [`psf_usuwanie.inc`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/psf_usuwanie.inc#L28-L38) (`odwiedz_bloki_usuwania`)
- **Vulnerability:** When BSP2 operated on a block partition (`nosnik_blokowy_dostepny == true`), `pobierz_blok_danych` used the 256-slot LRU cache (`wpisy_cache[256]`). `zwolnij_wszystkie_bloki_wezla` obtained a raw pointer `uint64_t* t1 = reinterpret_cast<uint64_t*>(pobierz_blok_danych(w->blok_posredni_1))` and then iterated up to `i < 512`, calling `zwolnij_blok_danych(t1[i])` — which calls `pobierz_blok_danych(id, false)` to zero each freed block. After freeing 256 blocks, the LRU cache evicted the cache slot holding `t1` (`blok_posredni_1`), overwrote `t1` with zeros for a newly freed data block, and caused the remaining iterations to read `0` from `t1[i]` (freeing block 0 — the root directory `/` — and leaking the remaining indirect blocks)!
- **Fix:** Re-fetch the indirect block (`pobierz_blok_danych(id_l1)` / `pobierz_blok_danych(id2)`) on each iteration instead of holding a stale raw pointer across `zwolnij_blok_danych` calls.
- **Regression Test:** Verified on a 520-block (2.13 MiB) file on a disk-backed BSP2 partition in [`fuzz_bsp2_ops.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bsp2_ops.cpp#L123-L163) (`run_deterministic_bug4_regression`).

### Bug #5 — Missing NUL-Termination of `wersja[32]` in `pakiet_parsuj_manifest_tekst` (`loader.cpp`)
- **Location:** [`loader.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/loader.cpp#L1308-L1396) (`pakiet_parsuj_manifest_tekst`)
- **Vulnerability:** Discovered by `fuzz_bur_loader` under ASan: `pakiet_parsuj_manifest_tekst` parsed key/value pairs into `char wartosc[64]`. For `klucz == "wersja"`, it copied up to `sizeof(out_manifest->wersja)` (32) bytes without writing a NUL terminator when `wartosc` had length `>= 32`, leaving `out_manifest->wersja` non-NUL-terminated and causing downstream C-string reads to overread into `out_manifest->opis`. It also left `*out_manifest` partially uninitialized on early error return.
- **Fix:** Zero-initialized `*out_manifest = {}` at function entry and enforced `sizeof(out_manifest->wersja) - 1` maximum copy length with explicit NUL termination across all manifest string fields.
- **Regression Test:** Added deterministic regression in [`fuzz_bur_loader.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bur_loader.cpp#L204-L221).

### Bug #6 — `bws_rozmiar_pliku` (`BWS 44`) Returning Success (`size = 0`) on Invalid/Traversal/Directory Paths
- **Location:** [`syscalls.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/syscalls.cpp#L2965-L2976) (`bws_rozmiar_pliku`)
- **Vulnerability:** Discovered by `test_bws_abuse_qemu.py` in QEMU: `bws_rozmiar_pliku` returned `static_cast<uint64_t>(rozmiar_pliku(sciezka)) + 1ULL`. Because `rozmiar_pliku(sciezka)` returns `0` both for an empty file and when `sciezka` is invalid (e.g. `"/../../cytaty.txt"`), non-existent, or a directory, `bws_rozmiar_pliku` unconditionally returned `1ULL` (`rozmiar = 0`, success) for invalid/traversal/missing paths!
- **Fix:** Updated `bws_rozmiar_pliku` to query `psf_metadane meta{}; if (!pobierz_metadane_tworu(sciezka, &meta) || meta.typ != TYP_PLIK) return 0;` before returning `static_cast<uint64_t>(meta.rozmiar) + 1ULL`.
- **Regression Tests:** Verified in [`fuzz_bws.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/fuzz/fuzz_bws.cpp#L402-L404) and in QEMU via [`test_bws_abuse_qemu.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_bws_abuse_qemu.py).

---

## 12. Phase 9 — Ring 3 ASLR and BUR v2 Technical Architecture

- **Specification & Prototype Header:** [`bur_v2.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/bur_v2.h)
- **Host ASan/UBSan Verification Suite:** [`narzedzia/test_bur_v2_aslr_host.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_bur_v2_aslr_host.cpp) (`make test-bur-v2-aslr-host` — PASS)
- **Key Architectural Properties of BUR v2 (`NaglowekBurV2`, 128 bytes):**
  1. **Backward Compatibility:** `NaglowekBurV2` preserves the first 64 bytes of `NaglowekBur` v1 (`magic = "BUR\0"`, `wersja = 2`, `punkt_wejscia`, segment offsets/sizes) so the loader can dispatch v1 (fixed base `0x01000000`, `BUR_FLAGA_ASLR_GOTOWY = 0`) and v2 (`BUR_FLAGA_PIE | BUR_FLAGA_ASLR_GOTOWY | BUR_FLAGA_STACK_GUARD`) seamlessly.
  2. **Explicit Relocation Table (`BurRelokacjaV2`):** Checked `reloc_offset` + `reloc_count * 16` table inside the image supporting `BUR_RELOC_ABS64 (1)`, `BUR_RELOC_REL32 (2)`, and `BUR_RELOC_GUARD_VA (3)` with strict segment bounds verification (`bur_v2_waliduj_relokacje`).
  3. **28-Bit Page-Aligned ASLR Slide (`bur_v2_wylicz_uklad_aslr`):**
     - **Code/Data Base:** `0x0000000100000000ULL + (rand28 << 12)` (268,435,456 page slots = **28 bits of entropy**).
     - **Heap Base:** `0x0000001000000000ULL + (rand28 << 12)` (**28 bits of entropy**, strictly separated from code/data and stack).
     - **Stack Top:** `0x00007FFF00000000ULL - (rand24 << 12)` (**24 bits of entropy**), with an unmapped 4 KiB lower guard page (`P=0`).
     - **Dedicated Stack Guard Page:** Optional read-only (`RO+NX`) page holding the per-process canary after kernel seeding.

---

## 13. Phase 10–13 — Build/Release Integrity, Stack Usage & Regression Results

### 13.1 Kernel Stack Frame Audit (`-fstack-usage`)
- Every kernel and Ring 3 translation unit was compiled with `-fstack-usage` (`*.su`) and audited in [`docs/security-hardening-2026-11-04/after.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/security-hardening-2026-11-04/after.json).
- All newly added SSP and security functions use minimal stack frames (`stack_protector_early_init`: 48 B, `stack_protector_losuj_kanarek_procesu`: 32 B, `__stack_chk_fail`: 208 B, `bws_rozmiar_pliku`: 544 B).
- In `psf.cpp`, `psf_przetlumacz_sciezke_slotu` (80 B frame) and `rozwiaz_sciezke` (704 B frame) were marked `__attribute__((noinline))` and the slot translation buffer in `psf_przenies_twor_wynik` was moved to `static char translation[MAKS_DLUGOSC_SCIEZKI]` (protected by `BlokadaPSF blokada`), keeping the maximum kernel stack frame at **8,832 B** (`virtio_blk_stress_test`) — well within the **65,536 B (64 KiB)** per-process kernel stack.

### 13.2 Complete Verification & Regression Matrix
| Test Suite / Target | Command | Environment | Result |
| :--- | :--- | :--- | :--- |
| Production ISO Build & Release Audit | `make iso` | Freestanding x86-64 + `verifyreleasemetadata.py` | **PASS** (614 files, 0 findings) |
| SSP Host Unit + ASan/UBSan + Death Test | `make test-stack-protector-host` | Host (`ASan` + `UBSan` + `fork()`) | **PASS** |
| SSP QEMU Ring 3 Containment + Ring 0 Panic | `make test-stack-protector-qemu` | Disposable QEMU x86-64 | **PASS** |
| Kernel Stack Guard Page Regression | `make test-guard-page` | Disposable QEMU UEFI x86-64 | **PASS** |
| BWS Adversarial Abuse & Bug #1/#6 Regression | `make test-bws-abuse-qemu` | Disposable QEMU x86-64 (6 categories) | **PASS** |
| Extended Coverage-Guided Fuzzing (7 Targets) | `make fuzz-extended` | Host (`ASan` + `UBSan` + `trace-pc`, 165,115 execs) | **PASS** (0 crashes, 5,042 edges) |
| BUR v2 + Ring 3 ASLR Host Prototype Test | `make test-bur-v2-aslr-host` | Host (`ASan` + `UBSan`, 1,000 layouts) | **PASS** |
| Existing BUR Loader Unit Suite | `make test-bur-loader` | Host (`g++`) | **PASS** |
| Existing Installer GPT/ESP/BSP2 Unit Suite | `make installer-unit-test` | Host (`g++`) | **PASS** |
| Existing Recursive Delete / Move Atomicity Suite | `make test-recursive-delete-host` | Host (`g++`) | **PASS** |
| Existing PSF Bootstrap / Repair Suite | `make test-psf-bootstrap-host` | Host (`g++`) | **PASS** |
| Existing File Watch Notification Suite | `make test-file-watch-host` | Host (`g++`) | **PASS** |

---

## 14. Remaining Risks and Next Milestone Recommendations

1. **Per-Thread Kernel Canary Switching (`%gs`-relative SSP):**
   - Currently, Ring 0 uses a single hardware-seeded per-boot randomized `__stack_chk_guard` (`-mstack-protector-guard=global`) so timer IRQ and cooperative `sched_yield()` context switches do not invalidate active C++ stack frames mid-execution. Introducing a per-CPU/per-thread kernel `GS.base` (`MSR_IA32_KERNEL_GS_BASE` / `MSR_IA32_GS_BASE`) block that stores `proces_t::kanarek_ssp_kernel` at a fixed `%gs` offset will allow per-kernel-thread compiler canaries across context switches.
2. **Full Toolchain Transition to BUR v2 PIE + Ring 3 ASLR:**
   - With `bur_v2.h` and `test_bur_v2_aslr_host.cpp` validated, the next milestone should update `narzedzia/elf_to_bur.py` (or a new `elf_to_bur_v2.py`) to emit `NaglowekBurV2` + `BurRelokacjaV2` entries from `-fPIE -pie` Ring 3 ELF binaries and wire `bur_v2_wylicz_uklad_aslr` + `bur_v2_aplikuj_relokacje` into `loader_utworz_proces_z_obrazu`.
3. **Read-Only Guard Page (`BUR_RELOC_GUARD_VA`) for Ring 3 Canaries:**
   - Under BUR v2, mapping the per-process `__stack_chk_guard` into a dedicated 4 KiB page that is marked `PAGE_PRESENT | PAGE_USER | PAGE_NX` (read-only) after kernel seeding will prevent arbitrary Ring 3 `.data` write primitives from overwriting `__stack_chk_guard` prior to a function return.
