# PSF Live boot regression — 2026-10-08

The regression is reproduced in the current worktree with real PSF functions and a fresh RAM filesystem. No disk corruption or hardware failure is needed.

## Exact panic chain and cause

`utworz_drabine_katalogow()` in `kernel.cpp` emitted the generic message through `zatrzymaj_start()`. Its caller order is PSF initialization → `aktualizacja_sprawdz_rozruch()` → base-tree preparation. The failing chain was:

1. Step **18** (zero-based), logical path **`/system`**.
2. `zapewnij_katalog_startowy()` calls `katalog_istnieje()`.
3. That helper called `wylistuj_katalog(path, buffer, 1)` as an existence/type probe.
4. Slot translation resolves `/system` to **`/sloty/a/system`**, already created at step 3; its five children were created at steps 4–8.
5. Path resolution and inode lookup succeed. The inode is **5**, type `TYP_KATALOG`, with **5 entries**; its parent `/sloty/a` is inode **3**, with **2 entries**. Root is inode **1** and PSF is ready.
6. `wylistuj_katalog()` reaches its first tag byte, tests `pozycja+1 >= max_dlugosc` (**1 >= 1**), and returns **false** for output-buffer truncation. It does not mean the directory is missing, unreadable or the wrong type.
7. Bootstrap attempts `utworz_katalog("/system")`. `utworz_twor()` finds the existing resolved inode and returns **false** before allocating an inode, inserting an entry, allocating blocks, writing or flushing. The symbolic reason is **`PSF_JUZ_ISTNIEJE`** (the previous boolean API did not expose a symbolic error).
8. Bootstrap panics. On a filesystem already containing the tree, it can fail earlier at a nonempty `/sloty`.

The introducing change is precisely the three truncation exits in **`psf.cpp::wylistuj_katalog()`**, changed from `return true` to `return false` during desktop-capacity hardening (tag, name and missing-newline truncation). The stricter full-list contract is correct for enumeration; the kernel's one-byte existence probe depended on the old truncated-success behavior. The previous host suites never executed this kernel adapter and therefore missed the regression.

## Focused fix and error reporting

The bootstrap adapter uses inode lookup and type checks, never serialized directory enumeration. The exact 51-path list was moved verbatim to `psf_bootstrap.h`, shared by normal kernel boot, installer and host tests. `psf_przygotuj_katalog_startowy()` holds the existing PSF lock, looks up the target, accepts an existing directory, rejects a wrong-type target, and creates only an absent directory. It verifies the result and preserves the ordinary non-idempotent CREATE API.

`PsfStanStartu` is bounded in-memory status, unrelated to disk structures or Ring3/syscall ABI. It records the first symbolic failure and allocation, insertion, write and flush stages. Tracking is scoped to the base-tree operation under the PSF lock; ordinary filesystem operations produce no new logs. Failed insertion retains the existing inode rollback. Generic CREATE keeps its existing RAM-commit semantics on persistent I/O failure; the bootstrap wrapper additionally reports and rejects that physical write/flush failure rather than claiming a durable bootstrap success.

Normal panic text now includes the trusted failing path and symbolic reason, for example `Nie mozna przygotowac katalogu /ustawienia PSF_NIE_KATALOG`. It contains no pointer or raw memory address. The panic remains mandatory on a real preparation failure.

`BURSZTYNDIAGPSFBOOTSTRAP` is **0 by default**. Opt-in builds (`make -B -j2 all iso BURSZTYNDIAGPSFBOOTSTRAP=1`) log only the 51 bootstrap steps, with logical and slot-resolved path, physical parent path, Live/installed mode, mount/ready state, root ID, backend, existence/lookup/create results, symbolic PSF reason, target/parent IDs, parent entry count, free-inode/free-block counts, allocation/insertion/write/flush results, first failed write block and BlockDevice error. Counts are scanned only with diagnostics enabled; PSF does not maintain global free-inode counters. The normal build does not scan them or emit diagnostic lines. No diagnostic heap allocation is added.

## Initialization ordering and architecture audit

Actual current order in `kernel.cpp`:

1. `instalator_boot()` reads Multiboot mode (`bursztyn.install=1` / `bursztyn.installed=1`).
2. Heap/scheduler and device setup precede PSF; `blokowy_inicjalizuj()`, AHCI initialization and VirtIO-BLK probing register storage devices. Normal probes perform read-only sanity checks.
3. Kernel maps and zeroes its **128 MiB** PSF RAM area before initialization.
4. Installed boot explicitly enumerates BSP partitions, attaches one with `psf_ustaw_partycje()`, calls `inicjalizuj_psf()`, and verifies the complete installed filesystem with `psf_weryfikuj_dysk()`.
5. Live boot calls `inicjalizuj_psf()` with no attached BSP partition. `wczytaj_istniejacy_bsp2()` has no device to mount; initialization creates a **writable ephemeral RAM BSP2**, initializes geometry/inodes and root, then sets `psf_gotowy=true`. It does not select the ISO as a writable disk and does not attach the QEMU disk image merely because VirtIO/AHCI discovered it.
6. `aktualizacja_sprawdz_rozruch()` selects the active slot. Its Live return skips **only installed A/B validation** and occurs after PSF initialization. It does not reset/unmount PSF or bypass base-tree preparation.
7. Base directories are prepared, then OTA settings/directories and embedded programs are installed. WM loading and desktop layout settings occur later. `/ustawienia/uklad-pulpitu.cfg` is not required for boot.

Therefore: no incorrect Live storage selection, mount order, root initialization, permission/PZB check or early desktop/settings access caused this failure. No physical write or flush occurs on its failing path.

The desktop diff was audited: the only generic PSF behavior change was the listing truncation result. BWS file APIs, path parsing, generic UTF-8/name validation, inode geometry and directory-entry limits were unchanged. Desktop `FileCapacity=128`, `ModelCapacity=141` remain in desktop headers/WM/settings only; no such limit is used in PSF or base directories. PSF retains **490 × 64 = 31,360 physical directory slots**, separately bounded by actual inode/storage resources. The 128-valued GPT record size/count and kernel path-array sizes are unrelated pre-existing values.

The desktop staging/model bounds, static arrays, UTF-8 termination and copies were reviewed. Model admission remains bounded to 128 files followed by 13 launchers. Production desktop, geometry fuzz and PSF/BWS tests pass under sanitizers. In addition, bootstrap fixtures guard RAM boundaries, partition boundaries, lock release and diagnostic-pointer lifetime. No memory overwrite was found.

## Every actual base path: fresh Live host trace

These are **host observations**, not runtime claims about a user's disk. Tests use real PSF and the actual source list, with a 16 MiB host RAM filesystem (kernel runtime uses 128 MiB). All rows: mount/ready **1**, root **1**, backend **RAM**, mode **Live**, PSF result **PSF_OK**, physical writes/flushes **not attempted**. Parent entry/free-resource counts are snapshots **before each step**. `lookup=FAIL` for an absent directory is followed by successful creation. Existing aliases have `create=not-attempted`.

The [complete tab-separated trace](psf_bootstrap_paths_2026-10-08.tsv) contains every required diagnostic field for all 51 paths in fresh Live, duplicate Live, half-present, persistent-first and persistent-reboot scenarios (255 rows). Persistent fixtures use an in-memory faulting VirtIO BlockDevice; no real disk image is opened by tests.

| Step | Path | Physical parent | Existed / lookup | Create | Inode / parent inode | Parent entries | Free inodes / blocks |
|---:|---|---|---|---|---|---:|---|
| 0 | `/sloty` | `/` | 0 / FAIL | OK | 2 / 1 | 0 | 1023 / 3070 |
| 1 | `/sloty/a` | `/sloty` | 0 / FAIL | OK | 3 / 2 | 0 | 1022 / 3069 |
| 2 | `/sloty/a/programy` | `/sloty/a` | 0 / FAIL | OK | 4 / 3 | 0 | 1021 / 3068 |
| 3 | `/sloty/a/system` | `/sloty/a` | 0 / FAIL | OK | 5 / 3 | 1 | 1020 / 3067 |
| 4 | `/sloty/a/system/jadro` | `/sloty/a/system` | 0 / FAIL | OK | 6 / 5 | 0 | 1019 / 3067 |
| 5 | `/sloty/a/system/biblioteki` | `/sloty/a/system` | 0 / FAIL | OK | 7 / 5 | 1 | 1018 / 3066 |
| 6 | `/sloty/a/system/konfig` | `/sloty/a/system` | 0 / FAIL | OK | 8 / 5 | 2 | 1017 / 3066 |
| 7 | `/sloty/a/system/motywy` | `/sloty/a/system` | 0 / FAIL | OK | 9 / 5 | 3 | 1016 / 3066 |
| 8 | `/sloty/a/system/boot` | `/sloty/a/system` | 0 / FAIL | OK | 10 / 5 | 4 | 1015 / 3066 |
| 9 | `/sloty/b` | `/sloty` | 0 / FAIL | OK | 11 / 2 | 1 | 1014 / 3066 |
| 10 | `/sloty/b/programy` | `/sloty/b` | 0 / FAIL | OK | 12 / 11 | 0 | 1013 / 3066 |
| 11 | `/sloty/b/system` | `/sloty/b` | 0 / FAIL | OK | 13 / 11 | 1 | 1012 / 3065 |
| 12 | `/sloty/b/system/jadro` | `/sloty/b/system` | 0 / FAIL | OK | 14 / 13 | 0 | 1011 / 3065 |
| 13 | `/sloty/b/system/biblioteki` | `/sloty/b/system` | 0 / FAIL | OK | 15 / 13 | 1 | 1010 / 3064 |
| 14 | `/sloty/b/system/konfig` | `/sloty/b/system` | 0 / FAIL | OK | 16 / 13 | 2 | 1009 / 3064 |
| 15 | `/sloty/b/system/motywy` | `/sloty/b/system` | 0 / FAIL | OK | 17 / 13 | 3 | 1008 / 3064 |
| 16 | `/sloty/b/system/boot` | `/sloty/b/system` | 0 / FAIL | OK | 18 / 13 | 4 | 1007 / 3064 |
| 17 | `/jadro` | `/` | 0 / FAIL | OK | 19 / 1 | 1 | 1006 / 3064 |
| 18 | `/system` | `/sloty/a` | 1 / OK | not-attempted | 5 / 3 | 2 | 1005 / 3064 |
| 19 | `/dane` | `/` | 0 / FAIL | OK | 20 / 1 | 2 | 1005 / 3064 |
| 20 | `/dane/sponsorze` | `/dane` | 0 / FAIL | OK | 21 / 20 | 0 | 1004 / 3064 |
| 21 | `/system/konfig` | `/sloty/a/system` | 1 / OK | not-attempted | 8 / 5 | 5 | 1003 / 3063 |
| 22 | `/system/motywy` | `/sloty/a/system` | 1 / OK | not-attempted | 9 / 5 | 5 | 1003 / 3063 |
| 23 | `/programy` | `/sloty/a` | 1 / OK | not-attempted | 4 / 3 | 2 | 1003 / 3063 |
| 24 | `/programy/notatnik.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 22 / 4 | 0 | 1003 / 3063 |
| 25 | `/programy/eksplorator.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 23 / 4 | 1 | 1002 / 3062 |
| 26 | `/programy/kalkulator.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 24 / 4 | 2 | 1001 / 3062 |
| 27 | `/programy/przegladarka.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 25 / 4 | 3 | 1000 / 3062 |
| 28 | `/programy/przegladarka-obrazow.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 26 / 4 | 4 | 999 / 3062 |
| 29 | `/programy/demo_ui.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 27 / 4 | 5 | 998 / 3062 |
| 30 | `/programy/kontrolki2.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 28 / 4 | 6 | 997 / 3062 |
| 31 | `/programy/kontrolki3.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 29 / 4 | 7 | 996 / 3062 |
| 32 | `/programy/kontrolki4.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 30 / 4 | 8 | 995 / 3062 |
| 33 | `/programy/aktualizacje.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 31 / 4 | 9 | 994 / 3062 |
| 34 | `/programy/test.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 32 / 4 | 10 | 993 / 3062 |
| 35 | `/programy/ekran.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 33 / 4 | 11 | 992 / 3062 |
| 36 | `/programy/tapety.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 34 / 4 | 12 | 991 / 3062 |
| 37 | `/programy/motywy.cebula` | `/sloty/a/programy` | 0 / FAIL | OK | 35 / 4 | 13 | 990 / 3062 |
| 38 | `/gry` | `/` | 0 / FAIL | OK | 36 / 1 | 3 | 989 / 3062 |
| 39 | `/gry/snake.cebula` | `/gry` | 0 / FAIL | OK | 37 / 36 | 0 | 988 / 3062 |
| 40 | `/uslugi` | `/` | 0 / FAIL | OK | 38 / 1 | 4 | 987 / 3061 |
| 41 | `/sterowniki` | `/` | 0 / FAIL | OK | 39 / 1 | 5 | 986 / 3061 |
| 42 | `/uzytkownicy` | `/` | 0 / FAIL | OK | 40 / 1 | 6 | 985 / 3061 |
| 43 | `/uzytkownicy/Pulpit` | `/uzytkownicy` | 0 / FAIL | OK | 41 / 40 | 0 | 984 / 3061 |
| 44 | `/ustawienia` | `/` | 0 / FAIL | OK | 42 / 1 | 7 | 983 / 3060 |
| 45 | `/logi` | `/` | 0 / FAIL | OK | 43 / 1 | 8 | 982 / 3060 |
| 46 | `/piaskownica` | `/` | 0 / FAIL | OK | 44 / 1 | 9 | 981 / 3060 |
| 47 | `/tymczasowe` | `/` | 0 / FAIL | OK | 45 / 1 | 10 | 980 / 3060 |
| 48 | `/obrazy` | `/` | 0 / FAIL | OK | 46 / 1 | 11 | 979 / 3060 |
| 49 | `/aktualizacje` | `/` | 0 / FAIL | OK | 47 / 1 | 12 | 978 / 3060 |
| 50 | `/aktualizacje/przygotowanie` | `/aktualizacje` | 0 / FAIL | OK | 48 / 47 | 0 | 977 / 3060 |

## Host validation

`test-psf-bootstrap-host` links the real `aktualizacja.cpp`, `storage_format.cpp`, and PSF implementation, and executes the actual Live A/B early-return path plus the shared production directory adapter/list. Hardware discovery, mapping and Ring3 launch are intentionally outside the host test. It proves that A/B BlockDevice access is skipped, PSF remains initialized, all base paths succeed, and the next OTA filesystem preparation executes. Slot A and Multiboot-selected slot B are exercised.

The 21 fixtures cover: reproduced legacy step-18 failure; empty/formatted root; absent/all-existing/half-existing directories; repeated invocation and persistent remount; unrelated long/UTF-8 root names; layout file present/absent and byte preservation; 163 desktop files and re-creation of a missing base directory; one/zero free inodes, including one-inode completion of the full tree and no-inode existing-tree success; one/zero free blocks; host-injected allocation failure with rollback; root at 31,359 and 31,360 physical slots; wrong-type `/ustawienia`; malformed root type, ID and pointer; invalid/missing-parent paths; uninitialized PSF; injected directory-data and inode writes; injected flush timeout; root read error; duplicate directory entry on remount; bounded panic formatting and RAM/partition canaries.

The physical-full directory fixture uses valid distinct names referring to an existing file inode, because the small host RAM geometry has 1024 inodes. Only fixture construction touches internals; actual insertion/bootstrap uses production PSF, so the 31,360-slot limit is tested separately from inode exhaustion. Existing mount validation repairs duplicate directory entries under its unchanged policy.

| Suite | Checks/assertions passed |
|---|---:|
| PSF/bootstrap/Live, normal diagnostics-off build, 21 fixtures | **25,185,564** |
| PSF/bootstrap/Live, diagnostics-on build, 21 fixtures | **25,185,314** |
| Storage/BSP2 and BlockDevice abstraction | **10,493** |
| Native filesystem moves | **138** |
| Production BWS moves/authorization | **438** |
| Actual PSF/BWS settings and overflow | **197** |
| Production desktop model/file actions | **131,828** |
| Desktop geometry/persistence/capacity fuzz, 600 layouts | **1,453,528** |
| Explorer/desktop integration | **12,650** |
| Desktop selection themes | **161** |

Large bootstrap totals include per-byte checks of RAM/partition sentinels; they are not millions of distinct startup scenarios. All targets ran with **ASan + UBSan**, with the repository's existing `detect_leaks=0` setting. No sanitizer diagnostics or compiler warnings occurred in the final test runs. Tests prove default-off diagnostic logging emits no bootstrap lines.

Commands: `make -j2 test-filesystem-move-host test-desktop-settings-host test-desktop-files-host test-desktop-capacity-host test-explorer-host test-desktop-selection-theme`; final affected-PSF verification `make -j2 test-psf-bootstrap-host test-desktop-settings-host installer-unit-test`.

## Dependencies, build and compatibility

Compiler-generated **`-MMD -MP`** dependencies now cover kernel C++, Ring3 C++ and mbedTLS C compilation. Make includes generated `.d` files. Explicit `psf.o`/`kernel.o` edges cover the new shared status/bootstrap headers on the first build of an existing worktree; existing WM source/header → temporary object → blob → kernel dependencies are retained. No clean is required.

`make -B -j2 all iso` completed with **exit 0**, without compiler warnings. The normal-build `psf.o` and `kernel.o` contain no `[PSF-BOOTSTRAP]` log prefix. The kernel extracted from `/boot/system_operacyjny.bin` in the ISO is byte-identical to the linked kernel.

- ISO SHA-256: `e65afe47e184f11fdc17ded3be0aca6e633964ea7bf4f120ec8322cfc6ab82a8`.
- Kernel SHA-256: `47a82306802f24b0f01ad4538554c3cd9aac608cde06d0e6bd108e2f036821b4`.
- Unchanged disk-image SHA-256 (checked before/after build): `8479e43911dc45e89f934fe48d01297e16f51d17aa561d4d1c216b1ae0fcddca`.
- Actual workspace image is `wirtualny_dysk.img`; `wirtualnydysk.img` is absent. Existing image size **4,294,967,296 bytes**, inode **2671852**, and mtime **1791433352809382104 ns** are unchanged.

Incremental dependency dry runs confirmed `psf.h` changes rebuild both `psf.o` and `kernel.o`; `desktop_entries.h` changes rebuild the WM temporary object and embedded blob. With no changed inputs, `psf.o`, `kernel.o` and `menedzer_okien_blob.o` are up to date. No source touching or cleaning was used for these dry runs.

Normal compiler stack frames: base-tree orchestration **528 bytes**, optional-status adapter **288 bytes**, locked PSF preparation **144 bytes**. These are individual frame sizes, not total nested stack use. No large fixed kernel array or diagnostic heap allocation is added.

No BSP2 format or on-disk structure changed. No migration is needed; old disks remain compatible. The disk image needs **no content change**. No `make rm`, `make clean`, disk recreation, automatic QEMU boot or reboot loop was run. Manual runtime acceptance remains required.

Production files changed for this fix: `kernel.cpp`, `psf.cpp`, `psf.h`, new `psf_bootstrap.h`, new `psf_bootstrap_status.h`. Build: `Makefile`. New test: `narzedzia/test_psf_bootstrap_host.cpp`. Reports: this file and the full per-path TSV. Existing desktop-capacity production/test edits remain preserved in the current worktree; they were not rolled back.

Complete current-worktree production changes, including the preserved earlier desktop work: `desktop_entries.h`, `desktop_layout.h`, `desktop_layout_settings.h`, `menedzer_okien.cpp`, `psf.cpp`, `kernel.cpp`, `psf.h`, `psf_bootstrap.h`, `psf_bootstrap_status.h`; build file `Makefile`.

Complete current-worktree test changes: new `narzedzia/test_psf_bootstrap_host.cpp`, preserved new `narzedzia/test_desktop_capacity_host.cpp` and `narzedzia/desktop_capacity_cases.h`; preserved updates to `narzedzia/test_desktop_files_host.cpp`, `narzedzia/desktop_usability_cases.h`, `narzedzia/desktop_persistence_cases.h`, `narzedzia/explorer_launcher_cases.h`, `narzedzia/test_desktop_settings_authorization.cpp`.

Manual acceptance: boot the freshly rebuilt ISO, confirm the Live A/B skip message can still appear, then verify base-tree success, splash/desktop progression, intact files, correct desktop overflow/Explorer behavior and stable reboot. **Runtime PASS is not claimed.**
