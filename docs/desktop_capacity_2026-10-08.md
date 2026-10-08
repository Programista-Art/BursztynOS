# Desktop capacity audit and hardening — 2026-10-08

## Before source changes

The logical desktop limit is **141 = 128 filesystem children + 13 protected virtual launchers** (`PULPIT_MAX_WPISOW`, `DesktopSettings::Capacity`). The launcher catalog has exactly 13 entries, no separate expansion buffer. User entries and launchers use the same visual pool. Model entries and refresh staging are heap allocations; `Layout<141>`, previous positions, restore coordinate arrays, gesture selection and the 16,384-byte listing are static. Batch paths are heap-backed. Clipboard is separately limited to 128 paths of 512 bytes; folder drop registration to 31 folders plus the desktop target (32).

The actual startup origin is scaled **3 px**, set by `rozmiesc_ikony_ekranu`, overriding the scaled 140 px in `metryki_ekranu`. Let S(n)=round(n*dpi/100), W=screen width, B=screen height-S(40), X=S(20), Y=S(3), C=S(120), H=S(100). The legacy nominal grid has max(1,floor((W-2X)/C)) columns and max(0,floor((B-Y)/H)) rows. Reflow additionally searches inclusive column/row endpoints, clips candidate **visual** rectangles at edges, and accepts only collision-free candidates. Its candidate bound is (cols+1)*(rows+1); candidates can duplicate or overlap. There is **no single label-independent visual item capacity**: wrapping can use up to 16 lines, span multiple rows, and actual label widths decide whether partial edge columns fit.

At 1280x720 /100%: C=120, H=100, X=20, Y=3, B=680, cols=10, rows=6; nominal **60** complete cells, **77** reflow candidates. A production run with 128 short filenames `f000`..`f127` and all launchers places exactly **77**, including **zero launchers**. At /125%: C=150, H=125, X=25, Y=4, B=670, cols=8, rows=5; nominal **40** cells, **54** reflow candidates; the same run places exactly **45**, including **zero launchers**. The last edge row collides at this DPI. These exact measured capacities describe that fixture, not all filenames or arbitrary hand-packed positions.

BSP2/PSF allows 490 direct directory blocks * floor(4096/64)=**31,360 entries per directory**, subject to free blocks/inodes/storage. Names have a 55-byte payload maximum. This is independent of display geometry. BWS directory listing is capped at 65,536 bytes; Explorer uses that buffer and an 8,192-entry heap model, with parent+launchers consuming model entries. Thus filesystem capacity exceeds both UI enumeration budgets. Existing listing can report success for a truncated prefix, including at a newline boundary.

Layout v1 uses at most **141 records**, decoded identity <=511 bytes, unsigned decimal coordinates <=1,000,000, document buffer **225,856 bytes**. Unplaced entries are omitted already. First duplicate identity wins; duplicate positions are repaired by reflow. IDs were checked for controls/slashes but **malformed UTF-8 was not rejected**. Unknown launchers never create entries.

Before hardening, `placed=false` already prevents paint, hit tests and ordinary rubber-band selection. Full placement searches are finite. However grid `fits` and reflow use different collision rules; distance arithmetic is 32-bit in place/group search; geometry forces a column even on too-small screens. Ctrl+A selects **every logical entry**, including unplaced entries. Group transforms act only on placed members, but invisible selections leak into filesystem action/protection logic. Moving icons does not retry unplaced entries. Filesystem enumeration order determines placement, files precede launchers, and 129 filesystem children **reject the entire refresh**, retaining a stale previous model or showing no launchers on first load. Filesystem create/copy/move/paste themselves do not check grid capacity, so data remains, although a stale model hides changes. No deletion-on-overflow path was found. Saved placements are not overwritten to make room, but automatic order can starve launchers and bounded listing/model rejection is not explicitly surfaced.

## Implemented behavior

The logical budget remains **128 filesystem items + all 13 launchers**. `DesktopEntries::FileCapacity/ModelCapacity` now define it once. `Layout::set_count` rejects excess counts without changing the model; reflow rejects an invalid count before accessing storage. The WM parses complete listings into a bounded staging model, retaining existing filesystem identities first. At startup it loads v1 settings before admission so previously saved file identities also receive admission priority. Remaining admission is by exact unsigned UTF-8 relative path; committed filesystem entries are sorted by that same path, and catalog entries retain catalog order. Thus a lexically earlier newcomer cannot remove a valid existing icon at the logical limit. Above the limit, omitted entries are explicitly counted by `pulpit_nadmiar_modelu` and a Polish diagnostic is logged when the overflow count changes. They remain untouched in the filesystem and available in Explorer within its established enumeration budget. No modal or repeated per-frame warning is added. No hard model limit was increased.

`Desktop::Position::placed` remains the explicit placement representation. No sentinel coordinates are introduced. Within the logical budget, failed placement leaves the item in the model with `placed=false, selected=false`. Paint, click hit tests, rubber band, Ctrl+A and batch actions exclude it. Unplaced entries are omitted from layout v1 records; absence of a record is backward-compatible and means retry automatic placement. Existing positions are matched by `L` Launcher ID or `F` exact relative UTF-8 path, never by clipped/translated label.

Reflow first preserves all valid non-overlapping visual rectangles. For conflicting invalid saved positions, launchers win over files; ties within a type follow deterministic model order. Only waiting/invalid entries seek new slots: launchers first in catalog order, then files in stable UTF-8 path order. A waiting launcher never displaces a valid file merely for priority. If the screen itself cannot fit all launchers, the remaining virtual entries are logically unplaced and still available through Explorer's virtual catalog and the existing Start menu. No launcher becomes a filesystem object or a deletion candidate.

Actual icons are S(48) x S(48), cells S(120) x S(100), label region S(112) wide, glyph line S(16), line gap S(2), icon/label gap S(6), and selection padding S(3) on each side. Labels wrap using the production Unicode font (up to 16 lines); real visual bounds are the icon/label union plus selection padding. Grid and visual collision paths now both check these real bounds against other placed entries and the work area. Comfortable cell click targets remain established behavior, but only placed entries have active hit targets.

The preserved edge-aware search has **C=max(0,floor((W-2S(20))/S(120)))**, **R=max(0,floor((B-Y)/S(100)))**, B=height-S(40), Y=S(3), with supported geometry clamped to the existing display/settings 8192-pixel bound and scale validated before dividing. Nominal complete-cell count is C*R. Automatic reflow enumerates **at most (C+1)*(R+1) candidates per waiting item**, including partial edge candidates, then rejects oversize/colliding rectangles. Multiplication uses uint64 with an overflow guard; grid coordinate products and squared distances use int64 and checked conversion. Geometry and scale bound all loops/products. `searchIterations` makes the bound observable in host tests; `newlyPlaced` records successful retries. There is no free-slot-dependent while loop and no fallback to occupied (0,0).

| 1280x720 production fixture, short `f000`..`f127` names | 100% | 125% |
|---|---:|---:|
| Cell width/height | 120/100 | 150/125 |
| Work-area bottom | 680 | 670 |
| Origin X/Y | 20/3 | 25/4 |
| Complete columns/rows | 10/6 | 8/5 |
| Nominal complete-cell pool | **60** | **40** |
| Inclusive edge candidate bound | **77** | **54** |
| Before: actual visible, including catalog | **77** | **45** |
| Before: visible launchers | **0** | **0** |
| After: actual visible, including catalog | **77** | **44** |
| After: visible launchers | **13** | **13** |

There is no universal exact item count independent of names or custom positions. The table gives exact reproducible measured capacity for the production fixture; nominal grid and candidate limits give exact architecture formulas. The 125% fixture loses one short-file placement to real launcher label width at an edge, preserving launchers and collision bounds. Longer multiline labels can further reduce occupancy; manual compact positions can change available candidate intersections. Builtins and filesystem items consume the **same visual pool**.

After deletion, rename, move, create, paste or external Explorer mutation, the complete model refresh preserves matching positions and retries waiting entries. A completed visual single/group/launcher drag also reflows once; it does not wait for a changed filesystem hash. Resolution, DPI and work-area changes go through the same reflow. Existing physical positions survive a resolution change if still valid. DPI changes retain the established proportional coordinate scaling and remeasure labels; only newly invalid/colliding entries relocate. Expanding geometry admits more waiting entries without moving valid ones. The generic layout accepts taskbar/work-area changes; the WM's taskbar remains its existing scaled 40 px design.

Group dragging retains the established whole-selection translation and nearest valid shared-delta fallback. A packed destination can yield a clean zero-delta no-op, or rejection if no full group transform fits. It never partially places a group. Deltas are clamped before rounding and all members are checked before mutation. Dropping onto a folder follows the existing bounded BWS filesystem move path, independent of icon capacity; protected launchers cannot be moved as files. Partial filesystem batch failure retains the established completed-prefix semantics and does not turn into a visual partial group transform.

Filesystem creation/copy/move/COPY paste/CUT paste never consult placement capacity. Kernel/system installation of desktop BMP samples likewise operates through PSF, without a grid precondition. Failed COPY/CUT operations preserve source semantics and CUT clipboard state; successful filesystem work stays successful even if the new entry is unplaced or beyond the WM model budget. Explorer integration tests show 150 additional files and creation in the desktop directory independently of the WM limit.

Completed model/layout mutations now mark settings dirty and save after repaint/capture release, pruning departed records. One meaningful transaction writes once; drag motions, selection, theme changes and unchanged refreshes do not write. Raw enumeration hash still skips unchanged listings; a separate canonical model hash prevents layout writes merely because enumeration order changes. Fresh automatic startup without a saved layout still creates no settings file. A completed drag that frees room includes new placements in its same final save.

## Parser, buffers and remaining architecture limits

The persistence format remains **BursztynDesktopLayout=1** at `/ustawienia/uklad-pulpitu.cfg`, with unchanged checksum, typed identities and coordinate representation. Valid existing files remain compatible. Invalid/duplicate positions reflow; unknown launcher IDs/deleted file records never create entries. First valid duplicate identity still wins. Excess unique config records now reject the document safely instead of silently truncating it. IDs reject malformed UTF-8, overlong forms, surrogates, values beyond Unicode, truncated continuations, controls, separators, embedded NUL and overlong identities. Numbers remain bounded unsigned decimals; negative/overflowing/huge values are rejected. Serialization rejects an invalid count/identity/type before writing.

The WM listing buffer changes from 16 KiB static data to **64 KiB heap**, matching the pre-existing BWS/Explorer API budget, without raising logical capacity or placing a large array on the Ring3 stack. PSF `wylistuj_katalog` now returns **false** on every truncation path, including a cut exactly after a newline, so consumers cannot commit a misleading partial list. Existing model state is retained on listing errors; initial listing failure still permits the protected catalog. This changes error reporting, **not BSP2 format or syscall arguments**.

The existing BWS 64 KiB full-list ceiling and Explorer 8192-entry model remain architecture limits, separate from both the WM's 128-file budget and PSF's directory capacity. A directory exceeding the listing byte budget cannot currently be fully browsed by Explorer; it reports the listing failure. Solving that needs a separate paginated directory API/Explorer design. This work does **not** claim arbitrary-size Explorer enumeration, nor does it restrict filesystem creation to an enumeration budget. Tested overflow is well within that budget. Invalid/malformed filesystem enumeration also preserves the prior coherent model rather than committing a partial one. No filesystem format, paging ABI, launcher implementation, scheduler, graphics driver or QEMU launcher changes are made.

## Validation and Ring3 memory

All following host targets passed; no QEMU or reboot loop was run. Existing targets use ASan+UBSan with LeakSanitizer disabled (`detect_leaks=0`); no ASan/UBSan diagnostics occurred.

| Suite | Assertions/checks passed |
|---|---:|
| Production desktop model/file actions, including capacity, gestures and persistence | **131,828** |
| Production geometry/parser/capacity fuzz, 600 seeded layouts | **1,453,528** |
| Explorer, including overflow directory integration | **12,650** |
| Actual PSF/BWS settings, overflow creation/copy/move and truncated listing | **197** |
| Desktop selection themes | **161** |
| Native directory/file moves | **138** |
| Actual BWS move/clipboard authorization | **438** |
| Storage/BSP2 and installer unit prerequisites | **10,493** |

Coverage includes zero/one files; measured visual capacity -1/exact/+1/+many; logical maximum/+1; all slots full; freeing one and several slots; deterministic waiting order; deleted files; single/group/builtin movement and occupied destinations; resolution shrink/expand; DPI increase/decrease; altered work area; long UTF-8 identities/similar prefixes and same-label launcher/file pairs; malformed/duplicate coordinates, duplicate config identities, unknown launchers, stale files and new unsaved entries; overflow save/load/save; Ctrl+A/rubber-band/hit regions; folder drop and creation on a full desktop; COPY/CUT success/failure; source preservation; reversed filesystem enumeration; preservation of valid icons when a lexically earlier newcomer exceeds logical capacity. The fuzz suite asserts every item remains accounted for, no placed rectangle overlaps or crosses any work-area edge/taskbar boundary, no unplaced item remains selected, searches obey the candidate bound, reflow is idempotent and serialization is bounded. Extreme integer inputs are tested.

Compiled `-Os -fstack-usage` comparison against the initial worktree:

| Individual frame | Before | After |
|---|---:|---:|
| WM enumeration/admission | 272 | **288** |
| Geometry/update orchestration | 368 | **304** |
| Layout parser | 176 | **192** |
| Reflow | 160 | **176** |
| Gesture handler | 144 | **176** |
| Serializer | 160 | **160** |
| `_start` | 1280 | **1280** |
| Largest translation-unit frame (unchanged PNG inflater) | 3552 | **3552** |

No large entry/path/record array was added to a Ring3 stack. `Position` remains **168 bytes** (launcher priority uses existing padding); settings remain **300,288 bytes** on the heap; global `Layout<141>` grows **23,728→23,744 bytes** for bounded search diagnostics. The old 16 KiB static listing is removed; incoming-entry and admission scratch are bounded static storage. These are compiler frame measurements, not a guarantee about total nested-call stack use.

`make -j2 all iso` completed with **exit 0**, no compiler warnings, producing the updated **BursztynOS.iso**. Final acceptance remains manual: fill and exceed the desktop on Linux/Windows QEMU, verify files in Explorer, free slots, move groups/launchers, change DPI/resolution, reboot and check responsiveness, stable positions, no overlap/offscreen/taskbar placement and no data loss.

## Files changed

Production: `desktop_entries.h`, `desktop_layout.h`, `desktop_layout_settings.h`, `menedzer_okien.cpp`, `psf.cpp`. Build: `Makefile`.

New tests: `narzedzia/test_desktop_capacity_host.cpp`, `narzedzia/desktop_capacity_cases.h`. Updated tests: `narzedzia/test_desktop_files_host.cpp`, `narzedzia/desktop_usability_cases.h`, `narzedzia/desktop_persistence_cases.h`, `narzedzia/explorer_launcher_cases.h`, `narzedzia/test_desktop_settings_authorization.cpp`. Report: this document.

Artifact verification: the kernel extracted from `/boot/system_operacyjny.bin` in the ISO is byte-identical to the freshly linked kernel. ISO SHA-256: `0c99f5d09c4aaf0079ff94b404abeda4015cdaef4136bc5ae1921f55f8fb5b0b`. Kernel SHA-256: `e360419bba412c8c707a9052dfcd5cc081e2e6c43793bc7d283a6017a44bd0bc`.
