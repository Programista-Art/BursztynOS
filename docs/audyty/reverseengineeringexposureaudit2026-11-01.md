# Comprehensive Defensive Reverse-Engineering Exposure Audit of Bursztyn OS (2026-11-01)

> Resumed on 2026-11-05 against the current working tree. Sections 3–6 and 12
> contain the historical November 1 measurements. See sections 15–16 for fresh
> artifact hashes, verification results and remaining acceptance failures.
> The NX/W^X assurance in the earlier report was incorrect and is corrected
> in section 11. This report does not certify release readiness.

## 1. Executive Summary & Purpose

This audit evaluates the public release artifact of **Bursztyn OS** (`BursztynOS.iso`) from the perspective of an external reverse engineer who receives only distributed release media.

**Explicit Scope and Philosophy:**
- **Bursztyn OS remains reverse engineerable.** Once native x86-64 machine code is distributed in a bootable ISO, a skilled analyst using standard disassemblers and decompilers (`Ghidra`, `IDA`, `Binary Ninja`, `radare2`, `objdump`, `QEMU` + GDB stub) can recover control flow, system-call dispatchers, hardware register protocols, and binary formats.
- **No malware-style anti-analysis or obfuscation was introduced.** No anti-debugger traps, VM/hypervisor detection, timing-based traps, self-modifying code, runtime unpackers, encrypted code segments with embedded keys, corrupted ELF/BUR headers, crash-on-debugger behavior, Ghidra/IDA signatures, or bogus control-flow graphs were added.
- **Security does not rely on obscurity.** Ring 0 / Ring 3 isolation, page-table isolation and write protection (`CR0.WP`; see the NX limitation in section 11), user-pointer validation (`bws_vmm_sprawdz_zakres_uzytkownika`), BUR loader bounds checking, and PZB/BZL capability enforcement remain the sole security boundaries—never secrecy of syscall numbers, manifest syntax, or filesystem layouts.
- **Implemented exposure cleanup:** Minimize accidental implementation leakage in the public ISO (unstripped ELF symbol tables, embedded upstream test private keys, unused TLS server/debug instrumentation, and unarchived debug metadata) while preserving full developer debuggability through deterministic split-symbol archiving (`.release-private/<public_kernel_sha256>/`), third-party attribution notices (`/licenses/`; distribution source obligations require separate verification), and automated CI release verification (`narzedzia/verifyreleasemetadata.py`).

---

## 2. Threat Model

### 2.1 Assumed Adversary / External Researcher Capabilities
- **Input Artifacts:** Public release `BursztynOS.iso` (and everything extractable from it: `/boot/system_operacyjny.bin`, `/boot/tapety.pak`, `/boot/ustawienia.bur`, `/boot/installer/BOOTX64.EFI`, `/efi.img`, `/czcionki/ttf/*`, `/ikonki/*`, `/dane/sponsorze/*`, `/licenses/*`, plus embedded `.bur` / `.cebula` payloads inside the kernel image).
- **Static & Dynamic Tooling:** `xorriso`, `file`, `strings`, `nm`, `nm -C`, `readelf -a/-S/-s/-w`, `objdump -h/-t/-d`, `llvm-objdump`, `Ghidra`, `IDA Pro`, `Binary Ninja`, `radare2`/`rizin`, hex editors, custom Python parsers for `BUR\0`, `BSP2`, and `BTP`, and dynamic execution under `QEMU` (with monitor/QMP/GDB stub inspection).
- **Unavailable to External Analyst:** The private Git source repository, private developer symbol/DWARF archives (`.release-private/`), intermediate build objects (`*.o`, `*.elf`, `*.d`, `*.su`), host test binaries, and private CI logs.

### 2.2 Central Audit Question
> *How difficult is it to recover meaningful architecture and implementation details from the public release, which exposures are intentional or inherent to an operating system, and which exposures were accidental build leakage?*

- **Before Remediation:** **Very Low reverse-engineering effort / High accidental info leakage.** Even though `.bur` Ring 3 executables were flat binaries without symbol tables, the public kernel `/boot/system_operacyjny.bin` inside `BursztynOS.iso` shipped unstripped with **4,096 ELF symbols** (`98,304` bytes `.symtab` + `112,433` bytes `.strtab`), including **1,770 local symbols**, `STT_FILE` source file names (`scheduler.cpp`, `vmm.cpp`, `virtio_input.cpp`, etc.), C++ mangled function/class names, **10 embedded upstream mbedTLS test RSA/EC PEM private keys** (`MBEDTLS_CERTS_C`), dead TLS 1.2 server code (`MBEDTLS_SSL_SRV_C`), and embedded `__FILE__` debug strings (`biblioteki/mbedtls/library/ssl_*.c` via `MBEDTLS_DEBUG_C`). Ironically, `linker.ld` previously discarded `.debug_*` sections at link time, depriving developers of DWARF debug info while still shipping `.symtab` publicly.
- **After Remediation:** **Moderate reverse-engineering effort / Low info leakage.** The public `/boot/system_operacyjny.bin` in `BursztynOS.iso` is stripped (`0` ELF symbols, `0` DWARF bytes, `0` source paths, `0` PEM private keys, `0` mbedTLS `__FILE__` strings), while the matching developer ELF (`13,810,232` bytes with `3,428,854` bytes of DWARF v5 and `3,795` symbols) is archived privately in `.release-private/3bcda5b0b0c4c9e9dfd068879a304013ca37bd1e70455f183b4e53650b10f9f5/` with verified `PT_LOAD` byte-for-byte identity.

---

## 3. Clean Release Build Metadata & Hashes

### 3.1 Toolchain & Build Configuration
| Parameter | Value |
|---|---|
| **C/C++ Compiler** | `x86_64-linux-gnu-gcc` / `x86_64-linux-gnu-g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0` |
| **Linker** | `GNU ld (GNU Binutils for Ubuntu) 2.42` |
| **Objcopy** | `GNU objcopy (GNU Binutils for Ubuntu) 2.42` |
| **ISO Packager** | `grub-mkrescue` + `xorriso 1.5.6` |
| **OTA Trust Root Mode** | `BURSZTYN_AKT_TRUST_ROOT=release` (public key fingerprint `aceba052f9096313...`) |
| **Kernel CXXFLAGS** | `-mgeneral-regs-only -std=gnu++17 -O2 -g -ffreestanding -fno-asynchronous-unwind-tables -fno-unwind-tables -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -fno-stack-protector -fcf-protection=none -fno-pic -fno-pie -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti -fstack-usage` (`bws_nowoczesne.o` additionally uses `-Os`) |
| **mbedTLS CFLAGS** | `-std=gnu11 -O2 -ffreestanding -fno-asynchronous-unwind-tables -fno-unwind-tables -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -fno-stack-protector -fcf-protection=none -fno-pic -fno-pie -Wall -Wextra -fstack-usage` |
| **Ring 3 CXXFLAGS** | `-std=gnu++17 -O2/-Os -ffreestanding -fno-asynchronous-unwind-tables -fno-unwind-tables -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -fno-stack-protector -fcf-protection=none -fno-pic -fno-pie -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -DBSF_NATIVE=1 -fcheck-new` |
| **Kernel LDFLAGS** | `-T linker.ld -nostdlib -no-pie -static -Wl,-z,noexecstack -Wl,-z,max-page-size=0x1000 -Wl,--build-id=none` |
| **Ring 3 LDFLAGS** | `-nostdlib -no-pie --gc-sections` + `objcopy -O binary` |

### 3.2 Artifact Hashes & Sizes (Before vs. After)
| Artifact | Metric | Before Remediation | After Remediation | Delta |
|---|---|---|---|---|
| **Public ISO (`BursztynOS.iso`)** | Size (bytes) | `40,304,640` | `39,974,912` | **`-329,728` B (`-322.0 KiB`)** |
| **Public ISO (`BursztynOS.iso`)** | SHA-256 | `b2192b28cfde74a9c428317da96ee2c65436a05b3c6f39d25ebbaadad8bb69fe` | `4fce1693656730bd7334dc6b5df6a4ffb552cf43ea3ebc6a8f2fc2a9ac457944` | Clean release ISO |
| **Public ISO Kernel (`/boot/system_operacyjny.bin`)** | Size (bytes) | `10,512,544` | `10,182,736` | **`-329,808` B (`-322.1 KiB`)** |
| **Public ISO Kernel (`/boot/system_operacyjny.bin`)** | SHA-256 | `fb63913f0be63a67fd70e0c1dd037efcf4ca320bd80d3249ea7ecf520e7e51d6` | `3bcda5b0b0c4c9e9dfd068879a304013ca37bd1e70455f183b4e53650b10f9f5` | Stripped, `PT_LOAD` verified |
| **Private Dev Kernel (`system_operacyjny.bin` / `.release-private/.../kernel.elf`)** | Size (bytes) | `10,512,544` *(no DWARF)* | `13,810,232` *(full DWARF v5 + `.symtab`)* | `+3,297,688` B *(private only)* |
| **Private Dev Kernel (`system_operacyjny.bin`)** | SHA-256 | `fb63913f0be63a67fd70e0c1dd037efcf4ca320bd80d3249ea7ecf520e7e51d6` | `d23e38aef2cf80a572a0d753b38f1690f50a4c2920529841ecf7298ab2915abc` | Archived privately |

---

## 4. Complete Shipped ISO Inventory & Development Artifact Search

The complete machine-readable inventories are stored in:
- Pre-remediation baseline: [`docs/reverseengineering-exposure-2026-11-01/before.json`](reverseengineering-exposure-2026-11-01/before.json)
- Post-remediation baseline: [`docs/reverseengineering-exposure-2026-11-01/after.json`](reverseengineering-exposure-2026-11-01/after.json) (and `.release-private/release-inventory.json`)

### 4.1 Summary of All 614 Files in `BursztynOS.iso`
| Path / Category | Count | Total Size (B) | Format / Magic | Purpose & Classification |
|---|---:|---:|---|---|
| `/boot/system_operacyjny.bin` | 1 | `10,182,736` | ELF64 LSB `EXEC` (`7f454c46...`), stripped | Multiboot2 kernel + embedded initial RAM disk blobs (Required) |
| `/boot/tapety.pak` | 1 | `10,977,056` | `BTP` package (`42545050...`) | Multiboot2 wallpaper package (Required) |
| `/boot/ustawienia.bur` | 1 | `202,368` | `BUR\0` (`42555200...`) | Ring 3 Settings app payload (Required) |
| `/boot/installer/BOOTX64.EFI` | 1 | `3,809,280` | PE32+ EFI x86-64 (`4d5a...`), stripped | Standalone GRUB EFI loader installed to target disk ESP by Installer (Required) |
| `/boot/grub/grub.cfg` | 1 | `1,152` | ASCII text | Hybrid BIOS/UEFI GRUB boot menu (Required) |
| `/boot/grub/i386-pc/*`, `/boot/grub/x86_64-efi/*`, `/boot/grub/fonts/*`, `/boot/grub/roms/*` | 578 | `6,881,449` | GRUB ELF relocatable `.mod` / `.img` / `.pf2` | Upstream `grub-mkrescue` BIOS/UEFI runtime modules (Required by GRUB boot) |
| `/efi.img`, `/efi/boot/bootx64.efi`, `/System/Library/CoreServices/*`, `/boot.catalog`, `/mach_kernel`, `/.disk/*` | 8 | `3,369,327` | FAT12 El Torito EFI image / PE32+ EFI | Hybrid ISO9660 El Torito UEFI/BIOS boot metadata created by `grub-mkrescue` (Required) |
| `/czcionki/ttf/*.ttf` | 4 | `2,140,416` | TrueType Font (`00010000...`) | `DejaVuSans.ttf`, `DejaVuSans-Bold.ttf`, `DejaVuSansMono.ttf`, `DejaVuSerifCondensed-Italic.ttf` (Required) |
| `/czcionki/ttf/*.txt` | 3 | `12,359` | UTF-8 / ASCII text | `LICENSE-DejaVu.txt`, `LICENSE-stb.txt`, `SOURCE.txt` (Required license attribution) |
| `/ikonki/przycisk*.png` | 5 | `38,957` | PNG RGBA 128x128 (`89504e47...`) | Power menu icons: shutdown, reboot, sleep, lock, power (Required) |
| `/dane/sponsorze/*.txt` | 3 | `117` | UTF-8 / ASCII text | `mecenasi.txt`, `sponsorzy.txt`, `wkladtworczy.txt` (Required public attribution) |
| `/licenses/*` | 8 | `53,991` | UTF-8 / ASCII text | `BursztynOS.md`, `mbedtls.txt`, `stb-truetype.txt`, `stb-image.txt`, `DejaVu.txt`, `GRUB-copyright.txt`, `GPL-3.txt`, `README.txt` (Required license notices) |

### 4.2 Search for Accidental Development Artifacts
An automated scan across all 614 files in the extracted ISO searched for:
- `.map`, `.sym`, `.pdb`, `.dSYM`, `.debug`, `.a`, `.lib`, `.su` (stack usage), `.d` (header dependency), `.gcda`/`.gcno` (coverage), ASan/UBSan logs, QEMU/serial logs, crash dumps, `.bak`/`.tmp` files, `.cpp`/`.c`/`.h`/`.hpp`/`.asm`/`.S`/`.py` source files, and test directories (`.bsf-test`, `.virtiotest`, `.wm-test`, `.husarz-test`, `build`, `debug`, `tests`, `tmp`, `logs`, `symbols`, `.release-private`).
- **Result:** **0 Bursztyn OS development artifacts** are present in `BursztynOS.iso`.
- **Documented Upstream GRUB Exception:** `boot/grub/i386-pc/efiemu32.o` (`7,780` bytes) and `boot/grub/i386-pc/efiemu64.o` (`11,224` bytes) are placed automatically by `grub-mkrescue` from `/usr/lib/grub/i386-pc/` as part of GRUB's BIOS EFI-emulation runtime. Similarly, GRUB's `/boot/grub/*/*.mod` files are ELF relocatable modules (`ET_REL`) whose symbol tables (`SHT_SYMTAB`) are required at runtime by GRUB's dynamic `insmod` linker. These are upstream GPLv3 boot components, not Bursztyn OS object files.

---

## 5. Kernel Binary Symbol & Debug Info Audit

### 5.1 Program Headers (`PT_LOAD`)
Both before and after stripping, `/boot/system_operacyjny.bin` has 4 `PT_LOAD` segments (`Entry point 0x117d26` before, `0x117d26` after):
```text
Type   Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
LOAD   0x001000 0x0000000000100000 0x0000000000100000 0x000050 0x000050 R   0x1000  (.boot)
LOAD   0x002000 0x0000000000101000 0x0000000000101000 0x0ed090 0x0ed090 R E 0x1000  (.text)
LOAD   0x0f0000 0x00000000001ef000 0x00000000001ef000 0x048390 0x048390 R   0x1000  (.rodata)
LOAD   0x139000 0x0000000000238000 0x0000000000238000 0x87ce60 0xb64dc0 RW  0x1000  (.data + .bss)
```
Note that no segment is `RWE`: `.boot` and `.rodata` are `R`, `.text` is `R E`, and `.data`/`.bss` is `RW`.

### 5.2 Section & Symbol Table Comparison
| ELF Section / Metadata | Before ISO Kernel (`/boot/system_operacyjny.bin`) | After Public ISO Kernel (`/boot/system_operacyjny.bin`) | After Private Dev ELF (`.release-private/.../kernel.elf`) | Runtime Required? |
|---|---:|---:|---:|---|
| `.boot` (`PROGBITS`, `A`) | `80` B | `80` B | `80` B | **Yes** (Multiboot2 header) |
| `.text` (`PROGBITS`, `AX`) | `1,018,144` B | `970,896` B | `970,896` B | **Yes** (Kernel code) |
| `.rodata` (`PROGBITS`, `A`) | `370,480` B | `295,824` B | `295,824` B | **Yes** (Read-only constants) |
| `.data` (`PROGBITS`, `WA`) | `8,900,336` B | `8,900,192` B | `8,900,192` B | **Yes** (Initialized data + embedded `.bur`/asset blobs) |
| `.bss` (`NOBITS`, `WA`) | `3,047,008` B | `3,047,008` B | `3,047,008` B | **Yes** (Zero-initialized kernel tables/heap/stacks) |
| `.symtab` (`SYMTAB`) | `98,304` B (`4,096` syms: `1,770` local, `2,326` global) | **`0` B (`0` syms)** | `91,080` B (`3,795` syms: `1,711` local, `2,084` global) | **No** (Not loaded by Multiboot2 or kernel) |
| `.strtab` (`STRTAB`) | `112,433` B | **`0` B** | `107,077` B | **No** |
| `.debug_info` | `0` B *(discarded by old `linker.ld`)* | **`0` B** | `1,259,745` B | **No** (Developer symbolication only) |
| `.debug_abbrev` | `0` B | **`0` B** | `144,310` B | **No** |
| `.debug_loclists` | `0` B | **`0` B** | `927,139` B | **No** |
| `.debug_aranges` | `0` B | **`0` B** | `15,440` B | **No** |
| `.debug_rnglists` | `0` B | **`0` B** | `156,743` B | **No** |
| `.debug_line` | `0` B | **`0` B** | `591,829` B | **No** |
| `.debug_str` | `0` B | **`0` B** | `198,645` B | **No** |
| `.debug_line_str` | `0` B | **`0` B** | `6,819` B | **No** |
| `.comment` / `.note.gnu.build-id` / `.eh_frame` | `0` B | **`0` B** | `0` B | **No** (`--build-id=none`, `-fno-unwind-tables`, `/DISCARD/` in `linker.ld`) |
| `.shstrtab` (`STRTAB`) | `58` B | `42` B | `146` B | **No** (Minimal 42-byte section header names for ELF tooling) |

**Proof that `.symtab`, `.strtab`, and `.debug_*` are not runtime-required:**
1. `boot.S` and `kernel.cpp` do not request or parse `MULTIBOOT_TAG_TYPE_ELF_SECTIONS`.
2. `narzedzia/package_release_kernel.py` parses the ELF64 header and all `PT_LOAD` program headers before and after `x86_64-linux-gnu-objcopy --strip-all` and asserts that `(e_entry, [(p_flags, p_vaddr, p_paddr, p_filesz, p_memsz, p_align, sha256(segment_bytes)) ...])` are identical.
3. The on-disk installer (`instalator.cpp`) validates `installer.kernel` via magic `\x7fELF` and size checks, and GRUB Multiboot2 loads only the `PT_LOAD` segments.

---

## 6. Source Path, Filename, Function/Class Identifier & String Audit

### 6.1 Source Path Leakage
- **Before Remediation:** `0` build paths in `PT_LOAD` segments, because `__FILE__` was only invoked with relative paths. However, when DWARF is enabled in `system_operacyjny.bin`, `.debug_line_str` / `DW_AT_comp_dir` contains `/home/programista/Pulpit/Burstzyn/BursztynOS`.
- **After Remediation:** Because `package_release_kernel.py` strips `/boot/system_operacyjny.bin` for the ISO while archiving the DWARF-enabled `kernel.elf` in `.release-private/`, the public ISO contains **`0` occurrences** of `/home/`, `/mnt/`, `/workspace/`, `/build/`, `/src/`, `C:\Users`, or `/Burstzyn/BursztynOS`.

### 6.2 Source Filename Leakage
- **Before Remediation:**
  1. `.strtab` in `/boot/system_operacyjny.bin` leaked **all 130+ translation unit filenames** (`STT_FILE` symbols such as `scheduler.cpp`, `vmm.cpp`, `virtio_input.cpp`, `bws_nowoczesne.cpp`, `bezpieczenstwo.cpp`, `aktualizacja.cpp`, `skladacz_obrazu.cpp`, etc.).
  2. `.rodata` in `/boot/system_operacyjny.bin` leaked `biblioteki/mbedtls/library/ssl_cli.c`, `biblioteki/mbedtls/library/ssl_msg.c`, `biblioteki/mbedtls/library/ssl_srv.c`, and `biblioteki/mbedtls/library/ssl_tls.c` because `MBEDTLS_DEBUG_C` and `MBEDTLS_SSL_SRV_C` were left enabled in `mbedtls/config.h` defaults.
- **After Remediation:**
  1. All `STT_FILE` entries are removed from `/boot/system_operacyjny.bin` via `--strip-all`.
  2. `#undef MBEDTLS_DEBUG_C` and `#undef MBEDTLS_SSL_SRV_C` in `biblioteki/mbedtls/bursztyn_mbedtls_config.h` removed all `biblioteki/mbedtls/library/ssl_*.c` strings from `.rodata`.
  3. The only intentional `.h` string remaining in any binary is `extronic16B_unicode.h` inside the self-test UI output of `test.bur` (`[BURSZTYNUI 1/4] Bezpieczenstwo extronic16B_unicode.h i UTF-8...`), which is explicitly allowlisted in `narzedzia/verifyreleasemetadata.py`.

### 6.3 Function/Class Name & C++ RTTI/Exception Exposure
- Both `KERNEL_CXXFLAGS` and `RING3_FLAGS` pass `-fno-rtti -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables`. Consequently, there are **zero** `_ZTI*` / `_ZTS*` RTTI typeinfo structures, **zero** `_ZTV*` vtable name strings in `.rodata`, and **zero** `.eh_frame` / `.gcc_except_table` sections in public binaries.
- Stripping `.symtab`/`.strtab` from `/boot/system_operacyjny.bin` eliminated all **2,650+ C++ mangled `_Z*` symbol names** and internal C function identifiers that were previously exposed in the public ISO kernel.

### 6.4 Release Logging & Diagnostic String Policy
Strings remaining in `/boot/system_operacyjny.bin` and `.bur` apps are classified under a clear two-tier policy:
1. **Retained in Production Release (`BursztynOS.iso`):**
   - **User-facing UI & Attribution:** Polish UI labels, application titles, error dialogs, `Bursztyn OS v1.0.1.9`, sponsor/patron credits, and `/licenses/*`.
   - **Normal Hardware/Boot Diagnostics:** Concise serial/log status messages (`[KERNEL] Jadro i scheduler gotowe.`, `[BWS] API Wywolan Systemowych gotowe.`, `[E1000]`, `[VIRTIO-PCI]`, `[XHCI]`, `[INSTALL]`) required for hardware bring-up support and QEMU regression verification.
   - **Security & Fault Diagnostics:** `[PANIC]`, `[STACK]`, `[PF]`, `[PZB] file_create=DENY`, `[BUR FAIL] reason=...` — essential for diagnosing security denials and faults without exposing source line numbers or symbol tables.
2. **Excluded from Production Release (Compile-Time Diagnostic Flags Default `0`):**
   - High-frequency pointer/input forensic telemetry (`BURSZTYNDIAGABSMAP=0`, `BURSZTYNDIAGVIRTIOINPUTFORENSIC=0`, `BURSZTYNDIAGINPUTPOINTER=0`, `BURSZTYNDIAGXHCIPOINTING=0`, `BURSZTYNDIAGXHCIPHYSICALMOUSE=0`).
   - Geometry/compositor/splash frame traces (`BURSZTYN_DIAG_GEOM_TRACE=0`, `BURSZTYN_DEBUG_GUI_LAYERS=0`, `BURSZTYNDIAGGUIRUNTIME=0`, `BURSZTYNDIAGSPLASHTIMING=0`, `BURSZTYNDIAGSPLASHFIRSTFRAME=0`, `BURSZTYNDIAGSPLASHRUNTIMEFRAMES=0`).
   - Boot/OTA/stack diagnostic modes (`BURSZTYN_DIAG_BOOT=0`, `BURSZTYN_OTA_TRACE=0`, `BURSZTYN_STACK_TRACE=0`, `BURSZTYN_DIAG_STOS16K=0`).
   - `narzedzia/verifyreleasemetadata.py` scans the final ISO for forbidden diagnostic markers (`[ABS MAP]`, `[VINPUT FORENSIC]`, `BURSZTYNDIAGABSMAP`, `BURSZTYNDIAGVIRTIOINPUTFORENSIC`) and fails packaging if a diagnostic build is accidentally packaged as `BursztynOS.iso`.

---

## 7. Secret Scanning & Cryptographic Material Audit

### 7.1 Removed Public mbedTLS Test Private-Key Fixtures
During the pre-remediation secret scan of `BursztynOS.iso`, `verifyreleasemetadata.py` detected **10 PEM-encoded private keys** (`-----BEGIN RSA PRIVATE KEY-----` and `-----BEGIN EC PRIVATE KEY-----`) inside `/boot/system_operacyjny.bin`:

| File | Byte Offset (Before) | Secret Category | Redacted SHA-256 Fingerprint | Origin |
|---|---:|---|---|---|
| `boot/system_operacyjny.bin` | `1,214,720`, `1,227,264`, `1,243,712` | `private-key` (PEM) | `3a585f45c91692e7` | `biblioteki/mbedtls/library/certs.c` (`MBEDTLS_CERTS_C`) |
| `boot/system_operacyjny.bin` | `1,217,664`, `1,232,512`, `1,252,576` | `private-key` (PEM) | `7d8c8c5cc7901bfb` | `biblioteki/mbedtls/library/certs.c` (`MBEDTLS_CERTS_C`) |
| `boot/system_operacyjny.bin` | `1,225,792`, `1,245,440` | `private-key` (PEM) | `bba1f3fa6459b7d9` | `biblioteki/mbedtls/library/certs.c` (`MBEDTLS_CERTS_C`) |
| `boot/system_operacyjny.bin` | `1,229,824`, `1,254,304` | `private-key` (PEM) | `184a146ccd4b5d90` | `biblioteki/mbedtls/library/certs.c` (`MBEDTLS_CERTS_C`) |

**Analysis & Fix:** Although these were upstream mbedTLS sample test keys (`mbedtls_test_ca_key_*`, `mbedtls_test_srv_key_*`, `mbedtls_test_cli_key_*`) rather than Bursztyn OS infrastructure secrets, shipping any private keys in a production kernel image is unacceptable release hygiene and wastes `.rodata` space. Adding `#undef MBEDTLS_CERTS_C` in [`biblioteki/mbedtls/bursztyn_mbedtls_config.h`](../biblioteki/mbedtls/bursztyn_mbedtls_config.h#L137-L141) completely removed all 10 PEM private keys and their DER equivalents (**0 secret findings after remediation**).

### 7.2 Intentional Public Cryptographic Roots
- **OTA Update Verification Public Key (`aktualizacja_klucz.h`):** Generated by `narzedzia/aktualizacje/generuj_naglowek_klucza.py --tryb release` (public key fingerprint `aceba052f9096313...`). This is a **public verification key** (never a private signing key) required by the kernel to authenticate signed OTA update packages.
- **TLS Root CA Bundle (`tls.cpp`):** Public X.509 root CA certificates required by the kernel HTTPS/TLS 1.2 client.

---

## 8. BUR Executable Format, CEBULA Package & BSP2 / PSF Audit

### 8.1 `.bur` Native Ring 3 Executable Format
- **Derivation & Structure:** Each `.bur` executable is linked as an intermediate ELF (`<app>.elf`) using a custom linker script and converted via `x86_64-linux-gnu-objcopy -O binary <app>.elf <app>.bin` into a flat binary starting with a 60-byte `NaglowekBur` header (`loader.h`):
  - `0x00..0x03` (`4` B): Magic `"BUR\0"` (`42 55 52 00`)
  - `0x04..0x0B` (`8` B): `punkt_wejscia` (Entry VA, e.g. `0x1001000`..`0x1001xxx`)
  - `0x0C..0x13` (`8` B): `tekst_przesuniecie` (`0x1000`, page-aligned)
  - `0x14..0x1B` (`8` B): `tekst_rozmiar` (4 KiB-aligned size of `.text` + `.rodata`)
  - `0x1C..0x23` (`8` B): `tekst_wirtualny` (`0x1001000`, mapped read/execute, user-mode read-only)
  - `0x24..0x2B` (`8` B): `dane_przesuniecie` (`0x1000 + tekst_rozmiar`, page-aligned)
  - `0x2C..0x33` (`8` B): `dane_rozmiar` (total memory size of `.data` + `.bss`; bytes beyond EOF up to `dane_rozmiar` are zero-filled by `loader.cpp`)
  - `0x34..0x3B` (`8` B): `dane_wirtualny` (`0x1001000 + tekst_rozmiar`, mapped read/write; NX is not established (section 11))
- **What an external analyst can recover:** Entry point VA, code/rodata segment bounds, data/BSS segment bounds, x86-64 instructions (`syscall` numbers passed in `%rax`), and UI string literals.
- **What is absent:** Because `objcopy -O binary` emits only raw loadable segments and `RING3_FLAGS` omits `-g` and disables RTTI/exceptions/unwind tables, `.bur` files contain **zero** ELF headers, **zero** section headers, **zero** symbol tables, **zero** relocation records, **zero** source file names (except the intentional `extronic16B_unicode.h` test label in `test.bur`), and **zero** build paths.

### 8.2 `.cebula` Application Packages
- On the BSP2/PSF filesystem, applications live in `/programy/<app>.cebula/` (or `/gry/snake.cebula/`), containing:
  1. `<app>.bur` — the flat Ring 3 executable.
  2. `opis.aplikacji` — a human-readable UTF-8 manifest (`nazwa`, `wersja`, `plik_startowy`, `poziom_bzl`, `uprawnienia`, `ikona`).
- **Audit result:** `.cebula` packages contain only the runtime `.bur` binary and `opis.aplikacji`. No source files, developer notes, symbol maps, or private keys are present. PZB trust levels (`poziom_bzl`) and capability masks (`uprawnienia`) in `opis.aplikacji` are intentionally auditable security policy metadata.

### 8.3 BSP2 / PSF Filesystem Specification
- **Structure (`psf.h`):** 4,096-byte blocks; 44-byte packed `superblok` at block 0 (`sygnatura = "BSP2"`, `calkowity_rozmiar`, `ilosc_blokow`, `id_korzenia`, `start_wezlow`, `start_danych`); 4,096-byte `wezel_indeksowy` inodes (`typ`, `rozmiar_w_bajtach`, `czas_utworzenia`, `flagi_zabezpieczen` carrying `PSF_META_MAGIC = 0x5053464D00000000` + PZB owner level, and 490 direct relative block pointers); 64-byte `wpis_katalogowy` directory entries (`uint64_t id_wezla` + `char nazwa[56]`).
- **Audit result:** The BSP2 format is clean and self-describing by design. Host and bootstrap inspections (`psf_bootstrap.h`, `test-psf-bootstrap-host`) confirm that only intended system directories (`/system`, `/programy`, `/gry`, `/dane`, `/konfiguracja`, `/pulpit`) and production `.cebula` packages/assets are created during bootstrap.

---

## 9. Boot Component & Third-Party License Audit

### 9.1 Boot Path Inspection
- **BIOS & UEFI Live ISO Boot:** `BursztynOS.iso` uses GRUB 2 (`grub-mkrescue`) with `/boot/grub/grub.cfg` loading `/boot/system_operacyjny.bin` via `multiboot2` and `/boot/tapety.pak` via `module2`.
- **Installed Disk Boot (`/boot/installer/BOOTX64.EFI`):** Generated via `grub-mkstandalone -O x86_64-efi --locales="" --fonts=""` with embedded `narzedzia/instalator/grub-embedded.cfg` supporting A/B kernel slot selection (`bursztyn.slot=A/B`).
- Both `/efi/boot/bootx64.efi` (`208,896` B) and `/boot/installer/BOOTX64.EFI` (`3,809,280` B) are stripped PE32+ EFI executables containing zero developer home paths.

### 9.2 Third-Party License Notices (`/licenses/`)
`narzedzia/package_release_notices.py` packages all required license notices into `/licenses/` on `BursztynOS.iso` (in addition to `/czcionki/ttf/LICENSE-DejaVu.txt` and `/czcionki/ttf/LICENSE-stb.txt`):
- `/licenses/BursztynOS.md` — Bursztyn OS project license and attribution (`License.md`).
- `/licenses/mbedtls.txt` — Apache-2.0 license for `mbedTLS 2.28.x`.
- `/licenses/stb-truetype.txt` & `/licenses/stb-image.txt` — MIT / public-domain terms for `stb_truetype.h` and `stb_image.h`.
- `/licenses/DejaVu.txt` — Bitstream Vera / Arev / DejaVu Fonts license.
- `/licenses/GRUB-copyright.txt` & `/licenses/GPL-3.txt` & `/licenses/README.txt` — Clear separation between GPLv3 GRUB boot binaries distributed on the ISO and Bursztyn OS's standalone Multiboot2 kernel and Ring 3 applications.

---

## 10. Public System Call Interface (BWS) & PZB / BZL Security Architecture Review

### 10.1 BWS Syscall Reconstructibility
- An external researcher disassembling `.bur` apps or the kernel can straightforwardly identify the `syscall` instruction entry point (`MSR_LSTAR`) and the dispatch table for **BWS 1..93** (`bws_api.h`).
- **Architectural Verification:** BWS numbers are a public ABI contract required by Ring 3 applications. Security does not rely on hiding syscall numbers:
  1. **Pointer & Range Validation (`bezpieczenstwo.cpp` + `vmm.cpp`):** The intended validation path uses `zakres_numerycznie_poprawny()` (`0x1000 <= addr < 0x0000800000000000`, overflow-safe length check) AND `bws_vmm_sprawdz_zakres_uzytkownika()` (walking the active process's 4-level page tables to verify `PTE_PRESENT | PTE_USER`, plus `PTE_WRITE` for output buffers), and copied into kernel buffers via `skopiuj_z_przestrzeni_uzytkownika()` / `skopiuj_string_z_uzytkownika()` before use. This is not a proof that every handler is free of double-fetch/TOCTOU or pointer-validation bugs.
  2. **Capability & Trust Enforcement (`pzb.h`):** Each process descriptor (`proces_t`) carries both `poziom_zaufania` (`PZB 0..5`, where `3 = PZB_ZAUFANE`, `4 = PZB_UZYTKOWNIK`, `5 = PZB_PIASKOWNICA`) and an explicit 64-bit `uprawnienia` mask (`PRAWO_PLIKI_CZYTAJ`, `PRAWO_PLIKI_ZAPISZ`, `PRAWO_SIEC`, `PRAWO_GUI`, `PRAWO_URUCHOM_PROGRAM`, `PRAWO_SYSTEM_CONFIG`, `PRAWO_STEROWNIK`, `PRAWO_DEBUG`).
  3. **Negative Test Verification:**
     - Lower-trust processes attempting privileged BWS calls (e.g. `BWS_OTA_POLECENIE = 85`, `BWS_RESET = 25`, `BWS_SHUTDOWN = 26`, `BWS_HDA_TEST = 90`, or cross-tier file deletion/move on `/system` or higher-trust directories) are deterministically denied (`test-move-authorization`, `test-desktop-settings-host`, `test-hda-diag`, `test-aktualizacje`).
     - Existing loader tests exercise malformed manifests and malformed `.bur` headers (overlapping segments, non-4K alignment, `entry` outside `.text`, stack/guard-page overlap, or BSS overflow) are rejected fail-closed (`test-bur-loader`).

---

## 11. Exploit Hardening Review: NX / W^X & ASLR Status

### 11.1 NX / W^X Review (Correction on 2026-11-05)
- **Hardware Activation (`vmm.cpp`):** During VMM initialization, `vmm.cpp` checks `CPUID(0x80000001).EDX[20]` and sets `IA32_EFER.NXE` (`bit 11` of `MSR 0xC0000080`) and `CR0.WP` (`bit 16` of `CR0`, enforcing read-only pages even in Ring 0).
- **Kernel image:** ELF program headers declare R, RX and RW segments. Those flags describe the load image; they do not prove runtime page permissions or exclude writable aliases.
- **Confirmed limitation:** `ZmapujStroneSprawdzona()` and `mapuj_strone_wewnetrznie()` take `uint32_t` flags, which cannot carry the NX bit (bit 63). `pamiec.h` explicitly documents this limitation. `VmmMapujNowaStroneProcesu()` accepts only a write boolean and sets PRESENT/USER/WRITE. The loader uses it for both application segments and stacks. User code is mapped without WRITE, but writable user data and stacks are not demonstrated NX. Enabling EFER.NXE alone does not enforce W^X.
- **Heap/shared mappings:** non-executable protection is not established by this audit. There is no public arbitrary-page-mapping syscall, but that alone does not prevent writable executable pages.
- **Recommendation:** introduce explicit execute permission throughout the mapping API, audit all callers and aliases, then run execution-negative tests on data, stack, heap and shared pages. This requires a separate correctness-sensitive memory-management change. No NX/KASLR/ASLR runtime changes were made while resuming this exposure audit.
- **Earlier text corrected:** the previous version of this section incorrectly described user stacks, heaps and data as NX. Do not use that statement as security assurance.

### 11.2 ASLR / Address Predictability Status (Reported Separately)
- **Current Status:** Neither KASLR (Ring 0) nor Ring 3 ASLR is currently enabled.
  - The kernel loads at fixed physical/virtual base `0x00100000` (`1 MiB`).
  - Ring 3 `.bur` executables are statically linked at fixed virtual base `0x01001000` (`16 MiB + 4 KiB`), heaps start at `32 GiB` (`RING3_HEAP_BASE`), and user stacks start at `0x00007FFFFFFF0000`.
- **Rationale & Recommendation:** As required by task constraints, KASLR / Ring 3 ASLR was **not** retrofitted in this audit because `.bur` v1 is a non-relocatable flat binary format (`-fno-pic -fno-pie`). Introducing Ring 3 ASLR in a future milestone would require bumping the `.bur` specification (e.g., `BUR v2` with `ET_DYN`/PIE relative relocations `.rela.dyn`) and randomizing stack/heap bases using `bursztyn_krypto_wypelnij_losowe()`. Note that ASLR is an exploit-mitigation control (against ROP/code-reuse after a memory-corruption bug), not a reverse-engineering defense.

---

## 12. String Baseline, Split Debug Symbols & Crash Symbolication Workflow

### 12.1 String & Metadata Baseline (Before vs. After)
| Binary Artifact | Size Before (B) | Size After (B) | ELF Symbols (Before -> After) | DWARF Bytes (Public -> Private) | Printable Strings (Before -> After) | Source Paths (Public) | Likely C++/Internal Identifiers (Before -> After) |
|---|---:|---:|---|---|---:|---:|---|
| `/boot/system_operacyjny.bin` (Public ISO) | `10,512,544` | **`10,182,736`** | `4,096 -> 0` | `0 -> 3,300,670` *(private only)* | `65,102 -> 59,279` | `0` | `1,953 -> 153` (**`-92.2%`**) |
| `przegladarka.bur` (Husarz) | `546,496` | `546,496` | `0 -> 0` | `0` (`przegladarka.elf` private) | `4,157` | `0` | `7` (`0` mangled) |
| `test.bur` | `513,696` | `513,696` | `0 -> 0` | `0` (`test.elf` private) | `5,018` | `0` | `32` (`0` mangled) |
| `menedzer_okien.bur` | `356,960` | `356,960` | `0 -> 0` | `0` (`menedzer_okien.elf` private) | `2,800` | `0` | `5` (`0` mangled) |
| `kontrolki4.bur` | `333,888` | `333,888` | `0 -> 0` | `0` (`kontrolki4.elf` private) | `2,885` | `0` | `12` (`0` mangled) |
| `tapety.bur` | `284,320` | `284,320` | `0 -> 0` | `0` (`tapety.elf` private) | `2,585` | `0` | `1` (`0` mangled) |
| `eksplorator.bur` | `267,936` | `267,936` | `0 -> 0` | `0` (`eksplorator.elf` private) | `2,463` | `0` | `20` (`0` mangled) |
| `ekran.bur` | `235,136` | `235,136` | `0 -> 0` | `0` (`ekran.elf` private) | `1,568` | `0` | `0` |
| `ustawienia.bur` | `202,368` | `202,368` | `0 -> 0` | `0` (`ustawienia.elf` private) | `1,642` | `0` | `2` (`0` mangled) |
| `instalator.bur` | `202,368` | `202,368` | `0 -> 0` | `0` (`instalator.elf` private) | `1,449` | `0` | `0` |
| `aktualizacje.bur` | `202,368` | `202,368` | `0 -> 0` | `0` (`aktualizacje.elf` private) | `1,136` | `0` | `2` (`0` mangled) |
| `notatnik.bur` | `202,368` | `202,368` | `0 -> 0` | `0` (`notatnik.elf` private) | `1,509` | `0` | `1` (`0` mangled) |
| `test_dzwieku.bur` | `202,368` | `202,368` | `0 -> 0` | `0` (`test_dzwieku.elf` private) | `2,224` | `0` | `1` (`0` mangled) |
| `diagnostyka.bur` | `194,176` | `194,176` | `0 -> 0` | `0` (`diagnostyka.elf` private) | `1,760` | `0` | `0` |
| `shell.bur` | `153,312` | `153,312` | `0 -> 0` | `0` (`shell.elf` private) | `1,411` | `0` | `8` (`0` mangled) |

### 12.2 Split Debug Symbol & Private Crash Symbolication Workflow
1. **Build-Time Archiving (`narzedzia/package_release_kernel.py`):**
   - `make iso` links `system_operacyjny.bin` with full DWARF v5 debug sections (`.debug_info`, `.debug_line`, `.debug_loclists`, etc.) and `.symtab`/`.strtab`.
   - `narzedzia/package_release_kernel.py` produces the stripped public kernel `isodir/boot/system_operacyjny.bin` (`objcopy --strip-all`), verifies that `e_entry` and all `PT_LOAD` segments match byte-for-byte, computes `public_kernel_sha256` (`3bcda5b0b0c4c9e9dfd068879a304013ca37bd1e70455f183b4e53650b10f9f5`), and archives:
     - `.release-private/<public_kernel_sha256>/kernel.elf` (full DWARF + `.symtab`)
     - `.release-private/<public_kernel_sha256>/kernel.nm` (sorted demangled symbol table `nm -n -C`)
     - `.release-private/<public_kernel_sha256>/manifest.json` (`public_kernel_sha256`, `private_elf_sha256`, `load_segments_identical: true`)
2. **Symbolicating a Public Crash Report:**
   - When a user or tester reports a fault (`RIP=0x0000000000117d26`, `CR2=...`, `lastbws=...`) from a release ISO with known `public_kernel_sha256`, a developer resolves the exact function, file, and line offline using:
     ```bash
     x86_64-linux-gnu-addr2line -f -C -e .release-private/<public_kernel_sha256>/kernel.elf 0x117d26
     ```

---

## 13. External Researcher Simulation (Artifact-Only Analysis)

An automated artifact-only inspection (performed after source access, so not an independent clean-room experiment) ([`docs/reverseengineering-exposure-2026-11-01/artifact-observations.json`](reverseengineering-exposure-2026-11-01/artifact-observations.json)) was executed against the extracted ISO artifacts using only `xorriso`, `file`, `readelf`, `objdump`, and `strings`:

| Discovery Target | Estimated effort (not measured analyst time) | Evidence in Public Artifact | Intentional vs. Accidental |
|---|---|---|---|
| **Boot Architecture & Kernel Format** | `< 1 min` (Trivial) | `grub.cfg` (`multiboot2 /boot/system_operacyjny.bin`), `readelf -l` showing 64-bit static ELF at `0x100000` | **Intentional / Inherent** (Standard Multiboot2 x86-64 boot contract) |
| **Major Subsystems & Drivers** | `~2 min` (Low) | Boot/diagnostic strings for `VirtIO-PCI`, `VirtIO-GPU`, `VirtIO-BLK`, `VirtIO-NET`, `VirtIO-RNG`, `VirtIO-INPUT`, `Intel E1000`, `AHCI`, `xHCI`, `OHCI`, `Intel HDA` | **Intentional** (Supportable hardware diagnostics) |
| **Filesystem Identity (`BSP2` / `PSF`)** | `~5 min` (Low) | Strings `BSP2`, `[INSTALL] progress=30 stage=BSP2FORMAT`, and superblock/inode parser routines | **Intentional** (On-disk format interoperability & installer progress) |
| **App Packaging (`.bur` / `.cebula`)** | `~5 min` (Low) | `/boot/ustawienia.bur` starting with `42 55 52 00` (`BUR\0`) + embedded `/programy/*.cebula/*.bur` and `opis.aplikacji` manifests | **Intentional** (OS executable & manifest format) |
| **GUI & Font Architecture (`BursztynUI` / `BSF`)** | `~5 min` (Low) | `/czcionki/ttf/DejaVu*.ttf`, `/licenses/stb-truetype.txt`, UI strings `BursztynUI`, `Husarz` | **Intentional** (Public UI identity & font licensing) |
| **Syscall Interface (`BWS`) & Trust Model (`PZB`)** | `~15-30 min` (Moderate) | `syscall` wrappers in `.bur` binaries (`mov $N, %eax; syscall`), `[PZB] file_create=DENY`, `poziom_bzl` / `uprawnienia` in `opis.aplikacji` | **Intentional** (Auditable security policy & OS ABI) |
| **Internal Kernel Function & Source File Mapping** | **Before:** `< 1 min` (`nm -C` dumped 4,096 symbols)<br>**After:** **Additional manual decompilation (not timed)** | **Before:** `.symtab` named every C++ function, anonymous namespace variable, and `.cpp` file.<br>**After:** `.symtab`/`.strtab` removed; analyst must reconstruct call graphs in Ghidra/IDA from stripped machine code. | **Accidental before -> Remediated** |

---

## 14. Summary of Findings, Severity Classification & Changes Implemented

### 14.1 Findings Classification
| ID | Severity | Finding | Status |
|---|---|---|---|
| **RE-01** | **LOW** *(public upstream test fixtures; no deployment secret demonstrated)* | 10 upstream mbedTLS test PEM private keys (`mbedtls_test_ca_key_*`, `mbedtls_test_srv_key_*`, `mbedtls_test_cli_key_*`) compiled into `/boot/system_operacyjny.bin` via `MBEDTLS_CERTS_C`. | **FIXED** (`#undef MBEDTLS_CERTS_C` in `bursztyn_mbedtls_config.h`) |
| **RE-02** | **MEDIUM** | Public `/boot/system_operacyjny.bin` in `BursztynOS.iso` shipped unstripped with `4,096` ELF symbols (`98,304` B `.symtab` + `112,433` B `.strtab`), exposing all internal function names, C++ mangled names, and `STT_FILE` source filenames. | **FIXED** (`narzedzia/package_release_kernel.py` strips ISO kernel and archives private ELF/symbols in `.release-private/<sha256>/`) |
| **RE-03** | **MEDIUM** | `linker.ld` discarded `.debug_*` sections at link time, preventing developers from having DWARF symbols in private builds while still shipping `.symtab` publicly. | **FIXED** (`linker.ld` retains DWARF in private developer ELF; `package_release_kernel.py` strips DWARF/symbols from public ISO kernel) |
| **RE-04** | **LOW** | Unused mbedTLS TLS 1.2 Server code (`MBEDTLS_SSL_SRV_C`) and `__FILE__` debug instrumentation (`MBEDTLS_DEBUG_C` leaking `biblioteki/mbedtls/library/ssl_*.c`) compiled into kernel `.text`/`.rodata`. | **FIXED** (`#undef MBEDTLS_SSL_SRV_C` and `#undef MBEDTLS_DEBUG_C` in `bursztyn_mbedtls_config.h`, saving `~122 KiB` of `.text`/`.rodata`) |
| **RE-05** | **LOW** | Root-level `/licenses/` directory consolidating Bursztyn OS, mbedTLS, stb, DejaVu, and GRUB GPLv3 notices was not generated during ISO staging. | **FIXED** (`narzedzia/package_release_notices.py` integrated into `Makefile`) |
| **RE-06** | **INFORMATIONAL** | Architecture identity (`x86_64` monolithic Multiboot2 kernel), `BUR\0` flat binary header, `.cebula` manifests (`opis.aplikacji`), `BSP2` filesystem structures, and `BWS 1..93` syscall numbers are recoverable from release artifacts. | **BY DESIGN** (Required runtime formats and public ABI; see validation limits below) |

### 14.2 Qualitative Scores Before & After Remediation
| Metric | Before Remediation | After Remediation |
|---|---|---|
| **Accidental Information Leakage** | **High** (Full `.symtab` with 4,096 symbols, `STT_FILE` source names, mbedTLS `__FILE__` strings, 10 test PEM private keys) | **Low** (`0` ELF symbols in public kernel, `0` DWARF bytes, `0` build paths, `0` private keys, `0` diagnostic leaks) |
| **Reverse-Engineering Effort** | **Very Low** (`nm -C` and `readelf -s` immediately named every kernel function and global variable) | **Moderate** (No symbol tables or DWARF in public kernel or `.bur` apps; requires control-flow and cross-reference analysis in a decompiler, while remaining standard un-obfuscated native code) |

### 14.3 Source & Build Files Modified/Added
1. [`Makefile`](../Makefile) — Integrated `narzedzia/package_release_kernel.py`, `narzedzia/package_release_notices.py`, and `narzedzia/verifyreleasemetadata.py` into canonical `$(GRUB_CFG)` / `$(ISO)` targets.
2. [`linker.ld`](../linker.ld) — Retained DWARF `.debug_*` sections in the private developer ELF (`system_operacyjny.bin` / `.release-private/<sha256>/kernel.elf`) while `package_release_kernel.py` strips them from `isodir/boot/system_operacyjny.bin`.
3. [`biblioteki/mbedtls/bursztyn_mbedtls_config.h`](../biblioteki/mbedtls/bursztyn_mbedtls_config.h) — Added `#undef MBEDTLS_CERTS_C`, `#undef MBEDTLS_SSL_SRV_C`, and `#undef MBEDTLS_DEBUG_C`.
4. [`narzedzia/package_release_kernel.py`](../narzedzia/package_release_kernel.py) — Strips public ISO kernel (`--strip-all`), verifies `PT_LOAD` and entry-point identity, and archives matching private DWARF ELF + `kernel.nm` + `manifest.json` under `.release-private/<public_kernel_sha256>/`.
5. [`narzedzia/package_release_notices.py`](../narzedzia/package_release_notices.py) — Stages `/licenses/` (`BursztynOS.md`, `mbedtls.txt`, `stb-truetype.txt`, `stb-image.txt`, `DejaVu.txt`, `GRUB-copyright.txt`, `GPL-3.txt`, `README.txt`) into `isodir`.
6. [`narzedzia/verifyreleasemetadata.py`](../narzedzia/verifyreleasemetadata.py) — Automated CI/release metadata verifier checking all ISO files (or individual binaries) for DWARF/debug sections, unexpected ELF symbol tables, build paths, development files/directories, secret patterns, and forbidden diagnostic markers.
7. [`.gitignore`](../.gitignore) — Added `/.release-private/` and `/.release-audit/` so private symbol archives are never committed or packaged.


## 15. Resumed Verification — 2026-11-05

This verification resumes session `01a19938-c2da-7e22-b469-154c6a5e2daa`.
The tree has subsequently gained stack protection and a host-only BUR v2/ASLR
prototype. Those existing changes were preserved. The production loader still
loads BUR v1 at fixed addresses; passing the prototype's ASLR tests is not
production ASLR coverage.

### 15.1 Fresh production artifact and measured exposure

`make -j4 iso` succeeded using GCC 13.3.0 and GNU ld 2.42. This was the normal
production target from the current dirty tree, not a checkout/rebuild of the
historical November 1 source. Existing dependency-tracked objects were reused;
a from-scratch, hermetic build and bit-for-bit reproducibility were not proven.
The production target reconstructed the ISO and ran both BSF verification and
metadata verification. No runtime or compiler hardening settings were changed
in this resumed audit.

| Metric | Current production value |
|---|---|
| ISO SHA-256 | `cd5d9fe15c6c8c0bc78843a79398145ce7b16c3f30a6d4c14bae5cae421048bf` |
| ISO bytes | 40,093,696 |
| Public kernel SHA-256 | `0b5d4ac34db8553a05ecd30634c3729d3137037e20fb779a9d3ecedcc0964016` |
| Public kernel bytes | 10,301,520 |
| Public kernel symbols / DWARF bytes / build-path matches | 0 / 0 / 0 |
| Public kernel printable strings / likely identifiers | 63,857 / 153 |
| Public kernel DEBUG/TEST/ASSERT strings | 102 (heuristic count; includes intentional self-tests) |
| Public kernel source-extension string candidates | 1 beyond allowlist; `AuiCxk.h` appears in binary payload, not demonstrated to be a source filename |
| Private developer ELF bytes / symbols / DWARF bytes | 13,962,920 / 3,937 / 3,457,753 |
| ISO inventory / metadata findings | 614 files / 0 findings |
| Embedded BUR payloads matched byte-for-byte and scanned | 22 / 0 findings |

The current public kernel is 3,661,400 bytes smaller than its matching private
ELF, while retaining identical entry point and PT_LOAD contents. The earlier
before/after size table concerns November 1; later functional changes mean it
cannot be treated as a controlled size comparison for this resumed build.
The private matching ELF remains under
`.release-private/0b5d4ac34db8553a05ecd30634c3729d3137037e20fb779a9d3ecedcc0964016/`.

Machine-readable evidence:

- [Complete ISO inventory and string baseline](reverseengineering-exposure-2026-11-01/resumed-2026-11-05.json).
- [Compiler, linker, flags, private ELF sections and artifact hashes](reverseengineering-exposure-2026-11-01/resumed-build-2026-11-05.json).
- [Embedded app offsets, hashes and string baseline](reverseengineering-exposure-2026-11-01/resumed-apps-2026-11-05.json).
- [Sanitizer fuzzing results](reverseengineering-exposure-2026-11-01/resumed-fuzzing-2026-11-05.json).

### 15.2 Release gate changes

The verifier previously exempted every ELF below `boot/grub/` from symbol-table
checks. The exception now requires an ELF relocatable object at an expected
GRUB architecture/module path. An ordinary executable placed below that
prefix no longer bypasses the gate. Upstream modules still retain symbols
needed by GRUB's loader. Both ELF32 and ELF64 remain supported.

PE/EFI images now receive COFF symbol-table and debug-directory checks.
Truncated executable metadata becomes a recorded failure rather than a Python
traceback. Missing inputs and empty artifacts cannot produce a passing audit.
Eight tests cover those rejection paths, source paths, diagnostics, token/key
redaction and the distinction between PEM parser markers and complete PEM
private-key payloads. Run `python3 narzedzia/test_release_metadata.py`.

The scanner is a conservative release-hygiene gate, not an exhaustive secret
finder. Its zero findings mean none of its patterns matched. Raw binary
scanning does not decode arbitrary compression, cryptographic containers or
custom archive formats; the EFI filesystem image is inventoried as a file,
not recursively enumerated. Embedded app scans use exact matches against
local build payloads, so they are not independent clean-room reconstruction.

### 15.3 Verification and limits

| Check | Result |
|---|---|
| Fresh production ISO, BSF packaging and final metadata gate | PASS; 614 files, zero gate findings |
| Release-gate tests | PASS; 8 tests |
| Stack-protector host ASan/UBSan and death test | PASS; 2,007 checks per run |
| BUR v2 prototype/legacy-loader host ASan/UBSan | PASS; includes 10,000 layout trials; prototype is not production ASLR |
| Fuzz smoke ASan/UBSan | PASS; 35,033 executions, zero crashes |
| Installer storage host ASan/UBSan | PASS |
| Existing BUR loader tests | PASS; 17 groups |
| Settings themes / Start power host ASan/UBSan | PASS; 2,696 checks |
| Bounded PNG host ASan/UBSan | PASS; 3,446 checks |
| Display snapshot permissions/stale generation host ASan/UBSan | PASS |
| BSF font host ASan/UBSan | PASS; 12,201 checks |
| Husarz engine host ASan/UBSan | PASS; 127,432 checks |
| Isolated BWS abuse QEMU ISO | PASS; all six denial/validation categories |
| Normal production BIOS QEMU test, default limits | FAIL; no kernel readiness before the 60-second inactivity timeout |
| Same BIOS test with 240-second global / 180-second inactivity limits | FAIL overall; desktop ready at 126.80 s, boot/storage/network/wallpaper/initial-cursor checks PASS; cursor movement/restoration FAIL |
| Production UEFI Settings/themes/power QEMU | FAIL; desktop and Settings reached; `countdown label must be visible` assertion |
| Installer GUI integration QEMU | INCOMPLETE; manually interrupted after more than 11 minutes, at 50% / SYSTEMFILES; GPT, ESP, BSP2 format and package-installation progress observed, but completion, disk-only reboot and reinstall were not validated |

[Host test log](reverseengineering-exposure-2026-11-01/resumed-host-tests-2026-11-05.txt),
[BWS denial log](reverseengineering-exposure-2026-11-01/resumed-bws-2026-11-05.txt),
[extended BIOS log](reverseengineering-exposure-2026-11-01/resumed-normal-boot-extended-2026-11-05.txt),
[UEFI GUI failure](reverseengineering-exposure-2026-11-01/resumed-gui-2026-11-05.txt).

[Installer observation](reverseengineering-exposure-2026-11-01/resumed-installer-serial-2026-11-05.txt).
The installer test's own transaction timeout is 3,600 seconds; this run was
manually interrupted, not a timeout verdict from its harness. Its disposable
disks and screenshots remain in `.installertest/guest-ij13u19y/` for inspection.

No runtime source changed during this resumed audit. These acceptance failures
are observed on the supplied tree; a before/after runtime regression comparison
against the November 1 binaries was not performed. Their exact causes have not
been established. In particular, the initial BIOS timeout is not evidence of
an unbootable ISO, because the longer run reaches the desktop. The BWS write
protection test does not test NX execution denial.

Full audio/input/DisplayGeometry/OTA and all app workflows were not exhaustively
exercised in QEMU. The report does not certify full functional acceptance.

### 15.4 Remaining exposures and review work

- **Exploit hardening:** writable user mappings are not proven NX. The explicit
  mapping API limitation is documented in section 11. No ASLR or KASLR was
  introduced; the host BUR v2 prototype is not wired into production loading.
- **Reproducibility:** hashes associate public/private files exactly, but
  bit-for-bit rebuilds, ISO timestamp normalization and a complete toolchain
  lock were not tested. A private symbol archive alone does not establish
  reproducibility.
- **Licensing:** the ISO preserves notices. Corresponding GRUB source/patches
  or a suitable source-distribution arrangement were not verified. Notice
  presence is not a completed redistribution-compliance assessment.
- **Diagnostics:** public UI strings, security denials, fixed ABI values,
  module identities and intentional self-test strings remain visible. The
  normal ISO has an explicit automated-test boot menu; it is not a binary
  from which all self-test code has been removed. Forbidden pointer forensic
  markers are checked, but the gate does not prove absence of every possible
  diagnostic array or entry point.
- **Compiler/dead code:** existing Ring 3 section garbage collection is retained.
  LTO, kernel-wide section garbage collection and visibility changes were
  not enabled without a runtime-reference audit. No complete linker-map
  reachability proof or dynamic-table-safe dead-driver removal was performed.
- **Researcher simulation:** discovery-time entries in section 13 are effort
  estimates, not measured timings from an independent researcher. Native
  code, BUR segment boundaries, syscall wrappers, policy manifests, driver
  identities and GUI behavior remain recoverable after stripping.

The qualitative exposure assessment remains **High → Low accidental metadata
leakage**, and **Very Low → Moderate effort for detailed implementation
recovery**. Architectural identification and public ABI recovery remain easy.
These are qualitative estimates, not an exploitation-resistance score or a
release-readiness certification.

## 16. Resumed verification — 2026-11-07

The release metadata gate now extracts the GRUB `efi.img` FAT filesystem with
`mcopy` and applies the same inventory, executable-metadata, development-file,
path, secret and diagnostic checks to its members. JSON paths use
`efi.img!/EFI/...` to associate each member with its container. The container
itself remains inventoried; member sizes must not be added to the ISO size.
Missing mtools, extraction failures and empty EFI filesystems fail the gate.
This supersedes section 15.2's EFI enumeration limitation; arbitrary other
containers and compression formats are still not recursively decoded.

Two added tests prove that a private `.map` inside a valid FAT image is
inventoried and rejected, and an invalid EFI filesystem cannot pass.
All 10 release gate tests pass; see
[test evidence](reverseengineering-exposure-2026-11-01/resumed-gate-tests-2026-11-07.txt).
The existing installer storage host target also passes with ASan/UBSan:
10,512 storage/BSP2 checks and 138 native directory/file move assertions.
Leak detection is disabled by that existing target; this is not a leak-sanitizer
result or an installer GUI completion test.

The ISO target now depends explicitly on the verifier so a verifier edit
invalidates an existing ISO target. Changes in this continuation cover that
Makefile dependency, `narzedzia/verifyreleasemetadata.py`, its tests, and audit
documentation. No runtime code or hardening flags changed.
The QEMU failures and incomplete installer acceptance in section 15.3 remain
open until independently retested.

### 16.1 Fresh production artifact

`make -j4 iso` completed successfully, including BSF asset verification and
metadata verification: **615 inventory entries, zero findings**. These comprise
614 files extracted from the ISO and one member of the embedded EFI filesystem:
`efi.img!/efi/boot/bootx64.efi` (208,896 bytes). The EFI executable has no COFF
symbols or PE debug directory. Its `file` description mentions external PDB
stripping; no PDB was shipped or discovered by this inventory.

| Metric | Value |
|---|---|
| ISO bytes | 40,097,792 |
| ISO SHA-256 | `ff2dd94f2497536f7d1ed865b66f33ac71544e2e24574572a40497682b6be20f` |
| Public kernel bytes | 10,305,616 |
| Public kernel SHA-256 | `6a621c4be13c139dbb30009ff2408fb346190ace8ffd604e01fef25ca5007803` |
| Public kernel symbols / DWARF bytes / build-path matches | 0 / 0 / 0 |
| Public kernel printable strings / likely identifiers | 63,985 / 154 |
| Public kernel DEBUG/TEST/ASSERT string candidates | 102 |

[Complete inventory, string baseline and findings](reverseengineering-exposure-2026-11-01/resumed-2026-11-07.json).
[Installer host sanitizer evidence](reverseengineering-exposure-2026-11-01/resumed-installer-host-2026-11-07.txt).
The build log remains locally at `/tmp/bursztyn-resumed-release-2026-11-07.log`.
The matching private developer artifacts are under
`.release-private/6a621c4be13c139dbb30009ff2408fb346190ace8ffd604e01fef25ca5007803/`.
The zero findings apply to the verifier's implemented patterns and supported
formats. This continuation did not repeat QEMU acceptance or prove reproducible
rebuilds; the outstanding acceptance limitations in section 15 remain open.
