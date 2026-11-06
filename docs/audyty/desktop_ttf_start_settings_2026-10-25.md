# Desktop, Start and Settings after BSF

## Rendering and icon geometry

The halo had two concrete causes. The BWS alpha-blit path blended coverage into
RGB from the retained desktop layer, then forced the resulting pixel opaque.
An empty desktop-layer pixel contains no wallpaper color, so its zero RGB became
a black matte. The software BursztynUI target likewise forced opacity, including
zero-coverage pixels inside a glyph rectangle. The old full-opacity offset label
shadow compounded those edges. The glyph cache itself already held alpha masks;
no colored glyph or wallpaper cache was needed or introduced.

`kolor_alfa.h` now implements straight-alpha source-over for the desktop layer
and software targets. Zero coverage preserves the destination. Opaque sources
and destinations have fast paths. Normal window-layer BWS blits retain their
opaque RGB behavior. The desktop compositor resolves retained coverage against
the current wallpaper at composition time, instead of treating it as a special
pair of selection alpha tags. Legacy RGB pixels and visible-black markers keep
their existing interpretation.

Each selection repaint clears its previous layer region and paints, in order:
selection backdrop and one border, icon, optional normal-label shadow, foreground
text. Selected labels have no shadow. Normal labels use the same TTF coverage
once at a one-pixel offset with opacity bounded to 128/255. The selected backdrop
is a dark theme-derived shade at 192/255; its border blends theme frame/accent at
208/255. The final foreground comes from the selected-text theme role.

Dirty invalidation still uses compact old/new visual bounds, including the icon,
measured label and three scaled pixels of padding. Selection does not clear the
full desktop. Buffered transaction rendering and direct rendering now retain
compatible coverage; existing compositor wallpaper restoration remains in use.

Desktop line height comes from BSF in Ring 3. Wrapping prefers spaces, splits a
word only when that word exceeds the label width, and ends after at most three
lines with a UTF-8-safe ellipsis. Lines remain centered using proportional
advances. The hit target is the padded union of the actual icon and label bounds;
unused grid-cell corners no longer select an item. Existing double-click,
selection groups, drag/drop, placement persistence and file-operation routing
remain in place.

## Settings geometry

The old 950×680 window used nested panels with independent fixed navigation,
selector, preview and button sizes. The new default is 950×540 logical pixels.
`programy/ustawienia/layout.h` owns the measured layout: outer padding 14,
section gaps 12 and label/control gaps 4 logical pixels. Navigation buttons share
width, preferred height and gaps; their heading aligns with the content heading.

The family selector fills the available row, while the size selector measures
its label/value and reserves a narrower column. Both share the same measured
height. Preview, actions, status and help form one vertical sequence. Button
widths follow their text and share one preferred height. Theme radii, borders,
colors and control painting remain BursztynUI defaults.

The preview viewport is bounded independently of its content. Its Regular/Bold
sample follows the selected family, size and UI scale; a vertical scrollbar
makes remaining sample lines available in a smaller viewport. The whole client
also scrolls below the 800×480 logical content minimum, preserving access to
controls and status instead of overlapping them. Resize, scale and font changes
recompute geometry.

Dropdown popup width matches its selector. Popup height includes actual list row
height, list padding, the maximum border reservation across states and its outer
inset. Long family text is clipped within that width without creating a horizontal
scrollbar. Vertical scrolling is used when the visible-row budget/work area
requires it, rather than because border pixels were omitted from sizing.

## Start architecture and input

`start_menu_ui.h` uses the existing BursztynUI popup manager, a `PoleTekstowe`, a
bounded custom result control and two standard buttons. It opens on the left,
aligned to the Start button. The 19-entry app catalog retains existing launch
IDs and activation behavior. Snake is an ordinary searchable app with the alias
“gry”; the former cascading game entry is no longer necessary.

The normal panel height is the smaller of 600 logical pixels, the available
work height minus a safe top margin, and the existing popup transport limit.
Width is measured once on opening from the entire catalog, with a 340 logical
pixel baseline and work-area/transport bounds. Search never changes either
bound. The search field stays at the top; only the result list scrolls. Restart
and shutdown stay below a separator, with “Zamknij” the final bottom control.
Power actions are outside the app catalog and are never filtered.

The search model uses fixed arrays and at most 64 query codepoints. It decodes
UTF-8 and folds ASCII and Polish uppercase letters to lowercase. Relevance is:
exact name, name prefix, word prefix, name substring, then alias match. A bounded
insertion sort orders equal scores by folded name, with catalog order as the
stable final tie-break. The normal empty-query list is alphabetical too. There
is no locale-dependent byte comparison or per-query allocation.

Opening focuses search. Typing filters immediately. Up/Down move selection and
keep it visible; Enter launches the selected result; Escape closes the popup.
Closing clears the query, selection and scroll state. Empty results display
“Brak wyników” inside the same list region. Wheel input over the list changes
its own first visible row; search and power controls stay fixed. Extremely long
result labels receive a measured codepoint-safe ellipsis.

## Live updates and resource bounds

The existing safe font/theme notification refreshes BSF and window-title masks.
The desktop now also recomputes label geometry and reflows invalid positions
before repainting. An open Start retains its outer bounds and query while
recomputing row height and result geometry. Settings refreshes styles and its
entire measured viewport hierarchy. No parser object or glyph pointer crosses
IPC; the BSF cache remains color-independent and bounded to 2 MiB per process.

On x86-64 the search model is 164 bytes. The Start surface is 2,480 bytes; its
search, result and two button objects bring direct UI-object storage to about
14,408 bytes, excluding allocator overhead, shared styles and popup pixels.
The existing popup manager reuses its bounded pixel allocation; a default
340×600 panel plus shadow rounds to a 1 MiB buffer. Dimensions are capped to
fit the existing 2048×2048 transport, including the popup shadow. Filtering
allocates no new result structures. Settings adds ordinary bounded scrolling
controls; it does not add another font cache or wallpaper bitmap.

Native individual stack frames measured from compiler `.su` output include:
search scoring 544 bytes, filtering 336, result painting 288, desktop wrapping
656, Settings body layout 112, font-page layout 176 and viewport layout 80.
These are individual frames, not whole-call-chain peaks. Raster scratch and
large pixel buffers remain on the heap. Alpha composition adds no kernel heap
buffer or TTF parser.

## Validation

Completed host validation under ASan/UBSan unless noted otherwise:

| Area | Result |
| --- | --- |
| New desktop TTF / Settings / Start suite | 109,330 checks PASS |
| Core BSF / BSF UI-browser | 12,201 / 5,876 checks PASS |
| Selection colors and independent alpha arithmetic cases | 165 checks PASS |
| WM routing, real popup text input and keyboard launch | 214 checks PASS |
| Desktop production file/selection/drag/capacity/persistence suite | 152,100 checks PASS |
| Explorer shared behavior | 15,546 checks PASS |
| Production keyboard, TEXTINPUT and dialogs | 7,291 checks PASS |
| BursztynUI commands | 23 checks PASS |
| Compositor lifecycle/scene | 74,293 checks PASS, including 73,816 popup restoration assertions |
| File popups, rounding, shadows, modal input, 100%/125% | 308,494 checks PASS |
| Husarz selection and clipboard | 987,380 checks PASS |
| Husarz CSS / JavaScript | 362 / 10,865 checks PASS |
| User fault policy | 32,768 checks PASS |
| Theme GUI/static suite, Notepad and Calculator checks | PASS; Notepad 14 and Calculator 14 checks |

The new suite covers real TTF sizes 12/16/20/24 at scales 100/125/150/200%,
several client sizes and scrolling below the minimum. It checks aligned
selectors/navigation, containment and non-overlap, popup width and unnecessary
scrollbar absence, Polish/long labels, query matching and stable Start geometry.
Actual desktop painting verifies partial coverage, cached repaint reuse,
selection changes and identical pixels after deselection/reselection on light
and dark wallpaper compositions. Existing desktop tests were updated to click
visual bounds and to expect bounded word wrapping, rather than unused cell
corners or unlimited label lines.

A production QEMU run before the final dropdown inset adjustment passed the
complete desktop UI scenario, including live family changes. Its exact evidence
is `.bsf-test/desktop-ui-balanced-qemu/result.json`, kernel
`393f79eb0698b45917770d76d838334d3d006d070b32a1a3fa5226f73291c209`.
Earlier interrupted or timed-out harness attempts are not PASS evidence. Final
artifact identification and native reruns are recorded below after completion.

Reproduction:

```sh
make test-desktop-ttf-host test-bsf-host test-bsf-ui-host
make test-wm-click-routing test-desktop-files-host test-desktop-selection-theme
make test-compositor-cursor test-file-popup-host
make -j4 iso
python3 narzedzia/verify_bsf_iso.py BursztynOS.iso
python3 narzedzia/test_husarz_qemu.py --desktop-ui-only
python3 narzedzia/test_husarz_qemu.py --fonts-only --native-font-apps
python3 narzedzia/test_husarz_qemu.py --font-fallback-only
```

The desktop file suite was compiled with `-O0 -g0` and the same ASan/UBSan
instrumentation for this validation. Its original monolithic `-O1 -g` compile
was interrupted after the test inputs had changed; it is not PASS evidence.

Remaining scope limits: case folding covers supported Latin/Polish letters,
not full Unicode normalization or locale-specific collation. The Start catalog
is the existing fixed system app catalog, not an installed-app discovery
service. This milestone does not rerun the disk installer/persistence scenario;
host persistence/config checks pass and the BSF persistence protocol is unchanged.

## Final production artifacts and QEMU acceptance

The final ordinary release ISO passes automated comparison of all seven bundled
font/notice files and the Settings payload. No development profile instrumentation
is enabled. SHA-256:

- kernel: `521cc7e00a74ac1d57447c14ead17e83f86c08927161fb1c66baa79c48097046`
- ISO: `95874d3cdbd5224030c4e5b813e0b9dba044f7f25f11aac4b200179eebe4df64`
- Settings: `871b03ff751843437c34754d47588880ac61ea34be2fcceb694320e1498cd189`

All three completed native runs below use that final kernel, ordinary GUI input,
1280×720 VirtIO VGA, OVMF and one TCG vCPU. No user disks are attached.

| Scenario | Evidence under `.bsf-test/` | Result |
| --- | --- | --- |
| Search `hus/usta/diag/kalk/xyz`, stable border/power section, keyboard Calculator launch, query reset, independent scrolling, repeat selection pixels, Settings 12/16/24 and family change | `desktop-ui-accepted-qemu/result.json` | PASS |
| Real TTF alpha and author colors, local preview, Apply 24, browser/Notepad/Calculator/Explorer repaint, large-font Start | `desktop-ui-apps-qemu/result.json` | PASS |
| All four TTF assets invalid, readable bitmap desktop/browser/Settings, local preview and Apply | `desktop-ui-fallback-qemu/result.json` | PASS |

The first two use the exact production ISO above. The third uses a separate
fault-fixture ISO with deliberately corrupt font payloads; production font files
are untouched. Its ISO hash is
`abc8491e8e0125f0405e388a9cc6ee80af0d4e0b5ddfcef486fb755bc81e3262`.
Every run reports no kernel panic and no unexpected Ring 3 fault. Screenshots
include selected icons, empty and matching searches, scrolled Start, each tested
font size, the family popup and post-Apply views. The final size popup has enough
space for its rows and frame without the earlier spurious scrollbar.

`.bsf-test/desktop-ui-evidence/evidence.json` indexes artifacts, native results
and host logs. Generated object files, ELF images and native stack listings are
retained under `.bsf-test/desktop-ui-build-artifacts/`; generated tracked dependency
and test-binary noise was removed from the source diff. Python syntax checks and
`git diff --check` pass.
