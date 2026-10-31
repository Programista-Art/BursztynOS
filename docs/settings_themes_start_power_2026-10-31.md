# Settings themes and Start power menu — 2026-10-31

## Themes architecture and migration

The retired Ring 3 `Motywy systemowe` application maintained a hardcoded five-theme list and a simulated window preview. Its useful descriptions and controls now belong to native Settings. Settings navigation is **System, Wygląd, Motywy, Czcionki, Wsparcie**, with System selected on startup. Font and Support callbacks retain their existing models, scrolling and live BSF behavior.

`programy/ustawienia/themes_ui.h` builds the page from BursztynUI `Panel`, `ListaWyboru`, `Etykieta`, `Przycisk` and `WidokPrzewijany`. The page contains the installed-theme list, bounded description, Preview/Apply/Restore controls, current theme and status. The desktop supplies the preview. Larger fonts use the page's own scrolling in addition to the existing Settings viewport.

Enumeration reads `/system/motywy` through existing BWS 6 and validates definitions through the existing kernel parser/loader. Compatibility filenames are de-duplicated using the parsed stable ID. Names are presentation strings. The five bundled descriptions moved from the old UI; additional installed IDs receive a short generic description. There is one theme engine, in `motyw.cpp`.

BWS 87 gains the read-only command `MOTYW_CMD_OPIS = 6`: a readable, terminated 32-byte ID and writable `BursztynMotyw` destination are checked before copying. It returns validated metadata from the existing loader. The 172-byte v2 theme ABI, mutation authorization, existing commands and event format remain unchanged.

Persistence remains `/system/konfig/motyw.cfg`, using the existing `id = ...` format. Opening Settings only reads the current choice. Apply now checks the existing file write and flush results; a failed commit retains the active preview's rollback state.

The retired application source, linker, blob build rule, embedded payload symbols, manifest, desktop launcher, Start entry and bootstrap directory entry were removed. Settings owns its linker script. Installation/bootstrap removes the retired package's three known paths:

- `/programy/motywy.cebula/motywy.bur`
- `/programy/motywy.cebula/opis.aplikacji`
- `/programy/motywy.cebula`

Theme definitions and theme configuration are preserved. Unknown files inside an old package directory are not recursively deleted. [Packaging checks](settings-themes-start-power-2026-10-31/packaging.json) verify the kernel has no retired payload symbols and the catalogs/build have no duplicate launcher.

## Preview lifecycle

Settings owns a small token/deadline adapter. The existing kernel transaction owns the saved theme, PID, token, timeout, rollback and persistence.

- **Preview:** apply the selected ID temporarily and start a 15-second deadline; no configuration write.
- **Another selection during preview:** cancel the current token, restore the committed theme and start a new preview. There is one rollback target.
- **Apply during preview:** confirm the owned token and persist the previewed theme. Successful confirmation clears the rollback deadline.
- **Apply without preview:** commit the selected stable ID through the existing Set command.
- **Timeout or Restore:** restore the committed theme and clear the local token.
- **Close Settings:** cancel before closing the window. Token destruction also cancels. Existing kernel process cleanup restores an owned preview on process termination/fault.

Theme events refresh controls without recreating the page or discarding selection/transaction state. Preview uses the existing animation timeout/wait mechanism; there is no busy wait or full-window countdown repaint.

## Start layout, input and theme roles

The fixed outer Start dimensions, top search and scrolling result region remain. One bottom **Zasilanie** button replaces the two large action buttons. The power list is a separate `WidokMenuPopup` child surface in the existing `ZarzadcaPopupow`; it contains **Zablokuj, Uśpij, Zamknij, Uruchom ponownie**. Small semantic glyphs extend the existing native icon primitive.

Hover opens after **300 ms**. Leaving both the button and submenu closes the submenu after **350 ms**. Motion into either cancels the close deadline. Existing BWS timer events drive both deadlines. Click opens immediately through the standard button activation path. Outside clicks, Escape and capture cleanup use the shared popup manager.

Tab reaches the power button from search; Enter or Space opens the popup. Up/Down skip disabled actions, Enter dispatches the selected enabled action, and Escape returns focus to the power button while keeping Start open. A second Escape closes Start. Power items do not participate in search filtering.

App rows now share normal, hover, pressed and selected menu styles, with an accent keyboard focus outline. Search uses the standard text field. Menu/popup backgrounds use Panel; text uses Text; selected/hover rows use Selection and its contrast text; pressed rows use Active Accent and Accent Text; disabled items use Disabled Text; button backgrounds/hover use Button/Button Hover; separators, popup borders and the result scrollbar use Border; focus uses Accent. No Start-specific palette was introduced.

Placement prefers above the power control and uses the existing work-area clamp. Theme/font refresh updates both surfaces and recomputes popup placement. The existing committed display-generation event closes Start and its transient children before resolution/scale re-layout.

The shared popup row height now uses `max(24 × scale, font_height + 8 × scale)` so native 24 px text has room at every supported scale.

## Power capabilities and trusted paths

**Lock is disabled.** The tree has no complete authenticated session model, trusted lock/unlock screen, exclusive secure input ownership or protection against terminating/bypassing a lock process. A desktop-hiding substitute was not added.

**Sleep is disabled.** The ACPI implementation supports reset and S5 shutdown; it has no validated S3/suspend/resume path, wake trampoline or device/filesystem suspend/resume coordination. Display blanking is not exposed as sleep.

**Shutdown** still dispatches action 19 to `wykonaj_zamkniecie()` and BWS 26. **Restart** still dispatches action 18 to `wykonaj_restart()` and BWS 25. Existing kernel authorization, input/GPU quiescence, filesystem flush, device handling and ACPI/fallback handoff are preserved. The UI contains no reset primitive. Host tests activate both popup actions and call the actual WM dispatcher. A BWS mock intercepts 25/26 before any host power operation. [Routing evidence](settings-themes-start-power-2026-10-31/host-power-routing.log).

## Memory, stack and boundaries

The theme list is bounded to 64 IDs/names, with a 4096-byte retained metadata array. Enumeration uses an 8192-byte temporary heap buffer once when Settings initializes. Definitions remain subject to the existing 4096-byte kernel file bound. There is no per-frame theme allocation or full-screen preview copy.

Measured host object sizes include `ThemesPage = 6240`, `ListaWyboru = 2160`, `Przycisk = 2048`, and `StartMenu::Surface = 4616` bytes. Standard control trees/scrollbars and menu entries add bounded retained allocations. The existing popup manager owns/reuses the backing pixel buffer; the normal power submenu fits within Start's existing registered union.

WM needed 16 KiB additional reserved text space (`0x40000` → `0x44000`); the BUR header and linker agree on data VA `0x1045000`. Its 128 KiB data budget remains. Settings retains its 192 KiB text/128 KiB data segment limits. Removing the old application removes its standalone payload/process requirement.

Production stack-usage records: theme metadata BWS handler **272 B**, existing theme file loader **4272 B**, Settings enumeration **368 B**, Settings entry **1248 B**, Start open **96 B**, power open **64 B**, event handler **96 B**, refresh **32 B**, WM entry **1328 B**. No VLA, unbounded rollback history or user-sized stack allocation was added.

## Host validation

ASan/UBSan passes:

| Coverage | Checks |
|---|---:|
| Native BSF, desktop, Settings, Start, clock/Support and font/scale matrix | 139445 |
| New theme lifecycle, installed aliases/missing/invalid IDs, power input/focus/timing, theme pixel restoration, 100/125/150/200% and both screen bounds | 1790 |
| Theme engine | 155 |
| Slider deterministic pixels | 1705 |
| BursztynUI controls | 1379 |
| Display transaction / immediate physical edges | 304 |
| WM decoration renderer | 3356387 |
| WM routing and trusted power dispatch mocks | 229 |
| Compositor lifecycle / scene | 8677785 |
| Popup background restoration | 73816 |
| Rounded file popup input and pixels | 308494 |
| BSF / BSF UI | 12201 / 5876 |
| Desktop selection theme / Settings authorization | 165 / 209 |
| Desktop capacity, anchors and persistence fuzz | 882605 |
| PSF/bootstrap/Live production paths | 25186130 |
| Explorer | 15517 |
| Husarz runtime / selection and clipboard | 2259 / 987380 |
| User fault isolation policy | 32768 |

Notepad GUI/packaging (14), theme architecture (27), and launcher policy (9) checks pass. A pre-existing launcher assertion expected the old literal SDL setting; it now recognizes the existing optional `QEMU_DISPLAY_ARGS` override with SDL fallback. No launcher behavior was changed.

Evidence: [new/native UI](settings-themes-start-power-2026-10-31/host-ui.log), [regressions](settings-themes-start-power-2026-10-31/host-regressions.log), [controls/display](settings-themes-start-power-2026-10-31/host-display-controls.log), [theme/slider/popup](settings-themes-start-power-2026-10-31/host-theme-slider-popup.log), [launcher](settings-themes-start-power-2026-10-31/launcher-policy.log).

## QEMU acceptance

[Release themes/power acceptance](settings-themes-start-power-2026-10-31/result.json) passes all five bundled previews and rollbacks, Apply with no delayed rollback, Settings restart, close-during-preview rollback, hover, keyboard and outside-click dismissal. [Pixel checks](settings-themes-start-power-2026-10-31/palette-checks.json) confirm both Start and its submenu use the active theme and return to the committed palette for all five rollbacks. Checks cover kernel state and completed WM publication. All tests use the ordinary release ISO, real input and read-only observations.

[Release display regression](settings-themes-start-power-2026-10-31/display-result.json) passes **13** mode/scale transitions across 1280×720 and 1920×1080, including 100/125/150/200%, **19** retained icon anchors and **26** immediate/WM frame observations. Display Settings was placed near the bottom work-area edge. Taskbar pixels are at the committed bottom edge in every observed frame.

[Release clock/font/Support regression](settings-themes-start-power-2026-10-31/font-support-result.json) passes **100 clock frames across eight displayed seconds**, three Support categories, Refresh, live **16 → 24 px** font Apply and Settings reopen. The [24 px power popup](settings-themes-start-power-2026-10-31/font-support-power-popup-native-24px.png) remains readable with distinct rows and correct placement.

All three release runs use the same kernel/ISO hashes and report no Ring 0 panic, unexpected Ring 3 fault or GPU command timeout. [Combined acceptance manifest](settings-themes-start-power-2026-10-31/acceptance.json).

Screenshots: [Settings Motywy](settings-themes-start-power-2026-10-31/settings-themes.png), [persisted selection](settings-themes-start-power-2026-10-31/settings-persisted.png), [Bałtycka Noc](settings-themes-start-power-2026-10-31/power-baltyckanoc.png), [Grafit](settings-themes-start-power-2026-10-31/power-grafit.png), [Bursztyn Klasyczny](settings-themes-start-power-2026-10-31/power-bursztynklasyczny.png), [Bursztyn Jasny](settings-themes-start-power-2026-10-31/power-bursztynjasny.png), [hover](settings-themes-start-power-2026-10-31/power-hover.png), [keyboard](settings-themes-start-power-2026-10-31/power-keyboard.png).

## Artifact hashes and limits

[SHA-256 manifest](settings-themes-start-power-2026-10-31/sha256.json):

```text
fd284af7605ccbc745b0d2416c37ccbc6c47419da1b53b710c8f71aae5f10d04  system_operacyjny.bin
bfa89ff23d59f0b85756fcf63293ad5192c1a4f1a4f54d0b3c97575348488a24  BursztynOS.iso
```

Lock and sleep intentionally remain unavailable. Physical hardware suspend/resume and authenticated locking are not claimed. Theme enumeration has the explicit 64-entry/8192-byte bounds above. Extra installed theme descriptions use the generic fallback. Existing client theme repaints remain event-driven; the acceptance checks await their completed frames. Settings restart persistence is checked; a full installed-system reboot/upgrade migration is outside this UI acceptance run.
