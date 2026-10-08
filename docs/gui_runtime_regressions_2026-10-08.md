# Desktop and Explorer runtime regression fixes — 2026-10-08

This report covers the current worktree based on `55d7266`. Host verification
and the forced ISO rebuild are recorded below. Guest runtime acceptance is
still manual; no QEMU boot or disk-image operation is part of this work.

## Numeric input: cause and shared fix

The keyboard producer already emits distinct physical-key, Unicode TEXTINPUT,
and legacy KEY events. The fault was downstream: GUI command handlers compared
raw physical scan codes with logical character/control values. The popup
adapter normalized some keys but passed printable physical scans through
unchanged. Text fields and containers then interpreted them as commands.

| Input | Physical PS/2 scan | Incorrect old interpretation |
| --- | --- | --- |
| 8 | `0x09` | Logical Tab, moving focus from the field to OK |
| 9 | `0x0a` | Logical LF/Enter, prematurely attempting confirmation |
| 1 | `0x02` | Old pseudo Left command, moving the caret left |
| 2 | `0x03` | Old pseudo Right command, moving the caret right |
| 7 | `0x08` | Logical Backspace, removing the preceding character |

Consequently, `17` lost its `1` on the physical event for `7`; `21` inserted
`1` before the existing `2` after the physical Left collision. `18`/`28`
moved focus on `8`; `19`/`29` attempted confirmation on `9`; `27` erased its
`2` on `7`. No filename-specific substitutions are used.

Physical KEY events were interpreted as control input while TEXTINPUT inserted
the actual printable character. There was also a legacy printable-byte input
fallback in the popup adapter. The correction gives shared BursztynUI one
physical-to-command decoder, tags otherwise printable physical keys internally,
and inserts printable Unicode exclusively through TEXTINPUT. Controls consume
normalized commands instead of comparing raw scans with ASCII or old pseudo
command numbers. The BWS event structure remains 40 bytes.

Once a process receives physical keyboard events, shared GUI dispatch ignores
their legacy KEY echoes. The state survives popup/window focus transfers: an
Enter opening a new dialog cannot confirm that dialog through its subsequent
legacy echo. Legacy-only control streams still support editing commands; their
printable bytes do not form another text-input source. Explorer's physical
navigation and inline Escape cancellation are handled explicitly.

The real keyboard producer, production BWS structures, popup dispatch, ordinary
window dispatch, and shared text fields are exercised by the host tests. They
cover every requested name, Polish UTF-8, mixed text, caret movement, selection,
middle insertion, Home/End, Backspace/Delete, Tab/Shift+Tab, Enter/Escape,
repetition, keypad NumLock, invalid Unicode, UTF-8 capacity boundaries, and
one insertion for the physical → TEXTINPUT → legacy-echo sequence. Desktop
and Explorer creation are checked before and after actual confirmation.
Save/Open and installer fields reuse the repaired ordinary window/widget path;
installer application logic is unchanged. Shell editing has its own host
regression suite.

## Desktop refresh: cause, transaction and publication

Previously a direct filesystem action forced `hash_pulpitu_wazny=false`,
rebuilt the live model, and called `RysujPulpit(true)`. That full redraw used
BWS 19 to clear the desktop layer before issuing separate drawing calls.
The clear could be published before all icons were drawn, exposing wallpaper
and an incomplete icon surface. A file notification also called the context
close/refresh path even when no context menu was open.

Now listing and validation happen while the old complete model remains live.
Heap-backed replacement entries and layout retain identities, valid positions,
selection and anchor; protected launchers are merged before placement is
resolved. Only then does the WM event thread replace the entries and layout.
No renderer or persistence call occurs within that logical replacement.

Dirty bounds contain removed/added/changed icons. The complete replacement
pixels are prepared off-screen, then copied with transparency using BWS 57's
opt-in replacement mode. BWS 19 is absent from mutation repainting. Large dirty
bounds use tiles within the existing 2048×2048 syscall limit; every tile is
prepared before submission, followed by one explicit BWS 17 publication.
The cached wallpaper and desktop layer allocation are retained.

Publication tests record model generation and the full placed identity set.
For a visible single create or delete they observe exactly one resulting
desktop publication: old complete generation → new complete generation.
No intermediate missing-launcher, empty-filesystem or duplicate-identity model
is observed. The full-desktop deletion test removes a nested folder and admits
a waiting item into the freed slot without moving valid icons. Pixel tests
also exercise the production transparent-region replacement and verify that
replacement itself does not invalidate or publish before BWS 17.

Direct actions and deferred notifications use the listing hash; repeated
unchanged notifications cause no second model generation, publication or
persistence write. Notifications received during a modal/gesture retain the
old complete model and are applied after release. Closing an already closed
context menu causes no refresh. A changed layout transaction writes persistence
at most once. The layout path and v1 format remain
`/ustawienia/uklad-pulpitu.cfg`; capacity remains 128 filesystem items plus
13 protected launchers. Visual overflow remains explicitly `placed=false`;
launcher priority and deterministic UTF-8 file ordering are preserved.

## Recursive deletion: service, bounds and failure semantics

The old GUI called the low-level `usun_twor` primitive, which correctly refuses
a non-empty directory. That primitive retains its empty-directory semantics.
Desktop and Explorer now call the shared `usun_drzewo_uzytkownika` service,
implemented by BWS 7 and PSF's `psf_usun_drzewo` in `psf_usuwanie.inc`.

Under the PSF lock, an explicit heap-backed breadth-first plan is validated
before mutation; reverse plan order deletes children before parents. There
is no recursive C++ call chain. The bounds are 4096 total objects (including
the root), child depth 32 (root depth 0), 511 path bytes, and PSF's existing
55-byte component-name limit. Excessive depth, count, joined-path size,
invalid names, root deletion, duplicate/cyclic inode references, outside
subtree aliases, shared data blocks and invalid block references fail closed.
BSP2 has no parent links or supported hardlinks; path lookup is checked against
each expected inode and an ownership scan rejects aliases outside the subtree.
Unsupported triple-indirect block traversal is refused safely.

The original caller's process identity and PZB are passed into authorization.
Every object and parent label is checked during preflight and again immediately
before deletion. There is no PZB-0 escalation. `/system`, `/jadro`,
`/sterowniki`, `/programy` and `/sloty` are refused by the GUI tree service;
other namespaces, including `/ustawienia`, retain the applicable caller and
parent/object PZB checks. Protected launchers cannot become filesystem deletion
targets.

Preflight policy, corruption and bound failures remove nothing. PSF does not
provide an atomic subtree transaction. After deletion starts, the first error
stops the operation; if earlier objects were removed, the result is explicitly
`Czesciowo`. The existing primitive can alter RAM before a persistence failure;
the service checks the last flush result and reports partial failure honestly,
including a failed first persistence commit after a RAM removal. It does not
continue to another sibling after that failure.

One coalesced subtree notification is sent on success or partial failure;
preflight refusal sends none. Descendant watchers receive the subtree hint
without a new userspace event field. Polish GUI messages distinguish existing
name, denied permission, non-empty directory, missing object, invalid name,
full filesystem, I/O error, protected object, partial deletion, corruption,
limit and memory failure. Single-file and non-recursive boolean APIs remain
available.

## Compatibility and memory audit

BSP2 disk format and desktop persistence v1 are unchanged. The BWS ABI has
backward-compatible opt-in extensions: BWS 2/46 accept status mode 2; BWS 7
accepts recursive mode 1 or status mode 2; BWS 57 accepts transparent-region
replacement mode 2. Existing mode-0 boolean calls and modes 0/1 of BWS 57
retain their contracts. No syscall number, event size or ABI version changes.
These modes provide shared recursion, useful error reporting, and a dirty
replacement that does not require the old immediately visible full-layer clear.

PSF bootstrap still uses the fixed inode-lookup path. Truncated listing still
fails, and bootstrap code is unchanged. Installer mode startup, automatic app
launch, confirmation/write gating, splash, GPU startup/timeouts and Linux/
Windows launcher source are unchanged. Host checks cover those existing
contracts; installer and Live guest acceptance remain manual.

Buffers are bounded and NUL terminated; text cursor/selection offsets follow
UTF-8 boundaries; replacement truncation does not split a codepoint. Event
normalization preserves modifiers and source tags. Traversal validates names,
inode/block ownership, path joining and depth/count before deletion. Desktop
replacement arrays respect the existing 141-entry bound. Large model, traversal
and pixel buffers are heap backed.

Compiler `-fstack-usage` measurements, using the saved pre-change baseline:

| Affected Ring3 translation unit | Before | After |
| --- | ---: | ---: |
| Desktop WM | 3552 B | 3552 B |
| Explorer | 2688 B | 2688 B |
| Shell | 6896 B | 6896 B |

The largest frame among these affected Ring3 units remains 6896 B. These are
individual frame measurements, not a claim about total nested stack use.
The new PSF traversal's kernel frame is 768 B; its BWS wrapper is 608 B.

## Final validation

All requested host suites passed with ASan/UBSan and no sanitizer findings.
Per-suite results from the finalized runs are recorded below; numbers represent
the checks/assertions/test cases printed by each suite, rather than guest runs.

| Suite | Passed count |
| --- | ---: |
| Production keyboard, TEXTINPUT, text fields and dialogs | 7291 |
| Shared BursztynUI widgets, Save/Open dialogs and editing | 1379 |
| Explorer integration, dialogs and physical navigation | 15459 |
| Desktop production file actions/publication/overflow | 147918 |
| Desktop settings BWS/PZB | 197 |
| Desktop capacity/layout/persistence fuzz (600 layouts) | 1453528 |
| Desktop selection themes | 161 |
| Rounded file popups and modal input | 308494 |
| BursztynUI commands/shortcuts | 23 |
| Recursive deletion, PZB, corruption and I/O (29 fixtures) | 9193 |
| File watching/subtree notification coalescing | 28 |
| BWS move authorization | 438 |
| Storage/BSP2 (includes 138 native file/directory move assertions) | 10493 |
| PSF bootstrap, diagnostics disabled (21 scenarios) | 25185564 |
| PSF bootstrap, diagnostics enabled (21 scenarios) | 25185314 |
| Shell Unicode editing and existing shell behavior | 1913 |
| Startup compositor visibility | 86 |
| Production graphics/frame commits/region replacement | 73 |
| Welcome lifecycle/rendering/options | 13703 |
| VirtIO PCI/BLK/NET/GPU/input | 146 |
| Startup source-policy checks (counted once) | 20 |
| Welcome integration source-policy checks | 15 |
| Reboot/typography source integration checks | 7 |

Aggregate of the independent printed suite counters: **52,341,443 passed**.
Native-move and repeated startup-policy subtotals are not counted twice.
There are no unresolved test failures. Successful finalized suites are in
`.installertest/gui-resume-final-tests.log` and
`.installertest/gui-resume-final-remaining.log`; the latter run exited 0.
The Explorer fixture explicitly separates a legacy-only process from a process
that has already received physical events, exercising both supported contracts.
ASan leak detection is disabled by the established host targets; address and
undefined-behavior instrumentation are enabled.

The complete focused target list used for verification is:

```sh
make -j2 test-kontrolki4 test-gui-text-input-host test-desktop-files-host \
  test-explorer-host test-desktop-settings-host test-desktop-capacity-host \
  test-filesystem-move-host test-psf-bootstrap-host test-shell-host \
  test-desktop-selection-theme test-file-popup-host test-bursztyn-ui-polecenia \
  test-startup-visibility test-powitanie test-virtio-pci \
  test-recursive-delete-host test-file-watch-host
```

`make -B -j2 all iso`, run after completion of all host checks: **exit 0**,
no compiler warnings or errors. The existing wallpaper-packer warning was
emitted twice: it skips the tracked asset `Zamek w Pszczynie 2026r..jpg` because
of its unsafe name. That asset and packing policy are unchanged. The final log
is `.installertest/gui-resume-build-final.log`. The final ISO is
`BursztynOS.iso`, 35,256,320 B.
The extracted ISO kernel is byte-identical to `system_operacyjny.bin` (`cmp`
passed). SHA-256:

```text
ISO:    c0a402b3e139cf05419aa6d81e9bc7b3819c96096e71f0eb84cee205e1fc8da7
Kernel: 05b05261d93e0086c8dc2057ecc760e55a19474488f2fe73a41c16ff13a0cfba
```

## Manual guest acceptance still required

Create folders named `8`, `9`, `17`, `18`, `19`, `21`, `27`, `28` and `29`
exactly as typed, through both desktop and Explorer dialogs. Ordinary typing
must retain text focus and leave OK inactive. Check Polish text, editing keys,
Tab/Shift+Tab, Enter and Escape, including keyboard activation from context menus.

Create/delete visible desktop entries and a nested non-empty user folder;
check that icons retain positions without a visible empty/partial desktop or
wallpaper flash. On a full desktop, removal must allow a waiting item into its
freed slot while protected launchers and overflow semantics remain correct.
Protected system paths/children must remain protected and failures must use the
specific Polish messages.

Boot the Live ISO and verify the PSF bootstrap panic does not return. Verify
installer mode still launches the Installer app automatically, performs no
disk writes before GUI confirmation, reaches 100%, completes verification and
leaves a usable desktop with the established VirtIO devices. This report
claims host/build PASS only. QEMU was not launched; the absent
`wirtualnydysk.img` was not created or modified. No clean/rm target was run.

## Changed files

All source changes in the current worktree relative to `55d7266` are listed
below; generated dependencies and build/test binaries are excluded.

Production/build files (52):

- `Makefile`
- `bursztyn_gui.cpp`
- `bursztyn_gui.h`
- `bursztyn_ui/dialog_koloru.h`
- `bursztyn_ui/dialog_pliku.h`
- `bursztyn_ui/drzewo.h`
- `bursztyn_ui/inspektor_wlasciwosci.h`
- `bursztyn_ui/kalendarz.h`
- `bursztyn_ui/karty.h`
- `bursztyn_ui/lista_rozwijana.h`
- `bursztyn_ui/lista_wirtualna.h`
- `bursztyn_ui/lista_wyboru.h`
- `bursztyn_ui/menu_glowne.h`
- `bursztyn_ui/okno.h`
- `bursztyn_ui/okno_dialogowe.h`
- `bursztyn_ui/pasek_przewijania.h`
- `bursztyn_ui/pole_czasu.h`
- `bursztyn_ui/pole_daty.h`
- `bursztyn_ui/pole_numeryczne.h`
- `bursztyn_ui/pole_sugestii.h`
- `bursztyn_ui/pole_tekstowe.h`
- `bursztyn_ui/pole_wielowierszowe.h`
- `bursztyn_ui/pole_wyboru.h`
- `bursztyn_ui/pole_wyszukiwania.h`
- `bursztyn_ui/popup.h`
- `bursztyn_ui/przelacznik.h`
- `bursztyn_ui/przycisk_opcji.h`
- `bursztyn_ui/przycisk_podzielony.h`
- `bursztyn_ui/rozdzielacz.h`
- `bursztyn_ui/rysowanie.h`
- `bursztyn_ui/selektor_koloru.h`
- `bursztyn_ui/suwak.h`
- `bursztyn_ui/tabela.h`
- `bursztyn_ui/tabela_wirtualna.h`
- `bursztyn_ui/widok_drzewiasto_kolumnowy.h`
- `bursztyn_ui/widok_ikon.h`
- `bursztyn_ui/widok_menu_popup.h`
- `bursztyn_ui/widok_miniatur.h`
- `bursztyn_ui/zdarzenia.h`
- `bws_nowoczesne.cpp`
- `bws_nowoczesne.h`
- `bws_pliki.h`
- `grafika.cpp`
- `menedzer_okien.cpp`
- `plik_operacje_ui.h`
- `plik_popup_ui.h`
- `plik_status.h` — added
- `programy/eksplorator/eksplorator-plikow.cpp`
- `psf.cpp`
- `psf.h`
- `psf_usuwanie.inc` — added
- `syscalls.cpp`

Test sources/fixtures (12):

- `narzedzia/bursztyn_ui_host_fixture.h`
- `narzedzia/desktop_mutation_cases.h` — added
- `narzedzia/explorer_input_cases.h` — added
- `narzedzia/explorer_regression_cases.h`
- `narzedzia/file_operations_host_fixture.h`
- `narzedzia/instalator/test_storage.cpp`
- `narzedzia/test_boot_graphics.cpp`
- `narzedzia/test_desktop_files_host.cpp`
- `narzedzia/test_explorer_host.cpp`
- `narzedzia/test_file_watch_host.cpp` — added
- `narzedzia/test_gui_text_input.cpp` — added
- `narzedzia/test_recursive_delete_host.cpp` — added

Documentation: `docs/gui_runtime_regressions_2026-10-08.md` (added).
