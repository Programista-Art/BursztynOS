# Modern laptop input: independent xHCI controllers and composite HID

Scope: native Bursztyn OS, including ASUS GA401IV/Renoir diagnostics. No physical GA401IV or Acer is attached to this environment. Hardware compatibility beyond the recorded QEMU and host tests remains a retest requirement.

## Findings and implementation

1. **One controller owned the entire driver.** PCI discovery returned its first xHCI function. MMIO, rings, command/control completion identities, slots, root ports, enumeration and IRQ state were global. Discovery now examines every PCI function for class/subclass/prog-if `0C/03/30`, retains up to eight controllers, and initializes each independently. Excess controllers are logged with BDF. There are no AMD, ASUS or NVIDIA ID shortcuts.
2. **One HID endpoint represented a whole USB device.** The configuration parser validated trailing descriptors but retained only its first match. The new bounded parser returns every usable active HID interface, with independent report metadata, transfer rings, DMA buffers, pointer layouts/devices and keyboard histories. At most 16 interfaces are retained; USB's 15 distinct interrupt-IN endpoint numbers further bound a valid configuration. Additional interface state is allocated only for composite devices, constructed with non-allocating placement new, and retained until successful Disable Slot. The first interface remains a base subobject to preserve the established single-interface fixtures.
3. **USB keyboard history was global.** xHCI now calls the existing boot keyboard decoder with a device/interface-specific `InputDevice` and previous report. The legacy wrapper remains available for existing callers. Rollover/error reports and truncated reports do not release held keys. Boot keyboards are registered before arming their transfer; registry exhaustion is reported as interface unavailability. Dispatch continues through `input_dispatch_events` to the same keyboard/GUI code as PS/2 and VirtIO.
4. **Event-ring DMA was published while PCI bus mastering was disabled.** Actual QEMU raised `USBSTS.HCE=1` and both controllers timed out on Enable Slot. Bus mastering is now enabled after successful halt/reset and allocation, before ERSTBA publication. A host regression asserts this ordering. QEMU 8.2.2 reads the ERST entry when ERSTBA high is written; its failed-DMA path sets HCE. See the [primary QEMU implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/usb/hcd-xhci.c#L752) and [register write path](https://github.com/qemu/qemu/blob/v8.2.2/hw/usb/hcd-xhci.c#L2919). HCE is now checked alongside HSE and unexpected halt, with controller identity and USBSTS logged.
5. **IRQ handling ignored its controller argument.** Each vector now acknowledges only its own controller. IRQ context never parses reports, allocates memory, scans PCI or advances enumeration. Normal deferred service owns rings and contexts; an atomic per-controller service guard prevents reentry. Completion matching checks the owning controller's ring, slot, DCI and exact outstanding TRB address. Event-data completions are rejected by this pointer-based path. HID servicing rotates both slots and interfaces, including devices with more than eight busy interfaces.
6. **The old MMIO allocation was unsafe to extend.** Eight disjoint BAR windows per controller now start at virtual address `0x2000000000`, separate from OHCI/HPET/ACPI mappings. The small checked VMM mapping entry point returns actual mapping success; the existing void API remains compatible. BAR width, size, address/alignment, register windows, xHCI version, page size, context size, slots, ports and scratchpads remain bounded/validated. Producer rings additionally reject corrupt count/index/size and are checked for alignment and 64-KiB segment crossing.
7. **PS/2 readiness was unconditional.** The kernel no longer registers a keyboard merely by obtaining its device pointer or prints “PS/2 ready” after unsuccessful initialization. Keyboard initialization preserves the shared controller, preserves firmware scancode translation and enables IRQ1, requires acknowledged streaming enable, and reports actual detection/unavailability. Mouse initialization returns its streaming status and logs independently; all waits/retries remain bounded. USB initialization still precedes PS/2 negotiation. `input_set_pointer_ready` is a display/geometry publication barrier, opened independently of PS/2 success.

Configuration parsing validates the whole descriptor chain before accepting a result: header/total length, each descriptor's minimum and remaining length, HID subordinate descriptor sizes, endpoint direction/type/number, packet size/reserved bits/transactions, interval, duplicate interface/endpoint identity and SuperSpeed companion structure. Unknown classes and unsupported endpoints are skipped. Zero/one-byte lengths or overruns reject the configuration without exposing a partial result.

Endpoints are configured one at a time on the existing serialized command lane; the slot's largest configured DCI is preserved. SET_CONFIGURATION is followed by per-interface protocol/report setup. Standard boot keyboards and boot mice, including the established optional mouse wheel handling, retain their paths. Generic pointer descriptor compilation remains intact. Report usage hints identify consumer control, keyboard, pointer and other roles separately. Unsupported report layouts are logged and skipped while other interfaces continue. A shared EP0 timeout or unrecoverable EP0 failure retires the device, because subsequent interface control requests cannot safely reuse that lane. A timed-out Configure Endpoint operation also retires the device instead of reusing an input context that hardware might still read. Explicit completed endpoint failures can skip that interface.

## Code map and changed files

| Area | Existing path and change |
|---|---|
| PCI | `pci.cpp`, `pci.h`: existing configuration access, MSI/MSI-X validation and vector setup reused; all-controller scan in `sterowniki/usb/xhci.cpp` |
| xHCI state, DMA, IRQ, events | `sterowniki/usb/xhci.cpp`, `xhci.h`, `xhci_ring.cpp`: controller collection, explicit context passing, independent interface state, fatal status and ring checks |
| USB enumeration | `sterowniki/usb/xhci_enumeration.inc`, `usb.cpp`: per-controller state machine, all-interface setup/activation and per-interface continuation |
| HID parsing/decoding | `sterowniki/usb/hid_pointer.h/.cpp`, `usb_hid.h/.cpp`: configuration collection, role hints, existing pointer decoder, per-source boot keyboard history and registration |
| Common input | `input_device.cpp`, `input_device.h`: existing registration, `input_dispatch_events`, pointer arm/prove/remove and geometry gating; registry capacity increased from 16 to 64 |
| PS/2 | `mysz.cpp`, `klawiatura.cpp`, `ps2_mouse_parser.h`: existing bounded i8042 transactions/scancode and packet paths; actual keyboard probe and mouse result/logging added in `mysz.cpp` |
| Startup | `kernel.cpp`: remove premature keyboard registration and false readiness log; invoke acknowledged PS/2 keyboard initialization |
| Mapping | `pamiec.h`, `vmm.cpp`: checked equivalent of `ZmapujStrone`, preserving its existing API |
| Existing transports | `virtio_input.cpp`, `virtio_pci.cpp`, `sterowniki/usb/ohci.cpp`: inspected and reused, no transport rewrite |
| GUI delivery | `input_device.cpp` → `klawiatura_obsluz_klawisz`/`klawiatura.cpp` → GUI physical/text events; pointer publication → `grafika.cpp` → compositor/window-manager input (`wm_input.h`, `menedzer_okien.cpp`, BWS events) |
| Tests | `narzedzia/test_usb_composite.cpp`, `test_xhci_multi.cpp`, `test_xhci_multi_qemu.py` added; `test_ps2_controller.cpp`, `test_xhci_physical_mouse.cpp`, `test_xhci_pointer.cpp` adapted/extended; targets in `Makefile` |

No STL, exceptions, RTTI, C++ runtime allocation or host APIs were added to kernel code. Host test code uses ordinary host facilities, as existing fixtures do. DMA buffers are never freed on a mere timeout; confirmed Disable Slot is required. Initialization failures conservatively retain controller allocations where hardware ownership is uncertain.

## Validation

Detailed command output and screenshots are under `.input-test/`. New repeatable targets are:

```sh
make test-usb-composite-host test-xhci-multi-host
make test-pointing-host test-xhci-physical-mouse-host test-input-coexistence-host
make test-xhci-multi-qemu
```

- Composite parser/keyboard isolation: **253 checks, PASS**, ASan/UBSan. Includes every truncated buffer, zero/one/overrun descriptor lengths, malformed trailing descriptors, multiple HID interfaces, IN/OUT/bulk/reserved endpoints, unknown classes, alternate settings, independent keyboard histories, rollover handling and the full 20-event key/modifier transition batch.
- Multiple controllers/composite endpoints: **10,770 checks, PASS**, ASan/UBSan. Exercises simultaneous controllers with the same slot IDs, distinct rings/DMA/vectors, IRQ isolation, alien completion pointers, first-controller failure, unsupported first HID interface followed by keyboard/mouse, disconnect/freeing, 15-interface fairness and corrupt producer ring metadata.
- Physical-controller host model: **24,089 checks, PASS** in both production and physical-diagnostic builds; **248 allocations / 248 releases**. Includes Intel-style initialization, USB2/USB3 reset, stalls/recovery, short transfers, hotplug and actual production endpoint/report code.
- Existing pointing suite: PS/2 packet parser **330,443**, HID **6,051**, configuration **48**, ownership **146**, PS/2 controller/keyboard probe **4,165 PASS**, OHCI **5,855**, shared pointer streams **5,400/4,800**, xHCI IRQ/context/wrap/completion regressions **PASS**.
- Diagnostic functional parity: **12 comparisons PASS** across incremental enumeration, input coexistence and boot fixtures. Diagnostic builds preserve ordered DMA/MMIO/report/GUI effects.
- Actual UEFI QEMU on the final kernel: two xHCI controllers, keyboard on controller 0 and mouse on controller 1, both using slot 1 / DCI 3; both enumerate, configure and decode input. Visible GUI cursor moves **(500,300) → (520,310)** after an injected USB mouse report. The fixture waits for the actual cursor after the splash. The common Shift state follows press/release, and the PS/2-specific key state stays unchanged. Recorded result: `.input-test/qemu/result.json` and `serial.log`. A second real boot with VirtIO keyboard/mouse and two idle xHCI controllers also passes the same modifier/source and visible cursor checks; evidence is `.input-test/qemu-virtio/`.
- Final VirtIO host suite: **167 PASS / 0 FAIL**; heap/PMM/PCI locks and IRQ restoration: **4 PASS / 0 FAIL**.
- Full production build and ISO: PASS, no compiler warnings introduced in changed production files. Final artifact hashes are recorded below. Full-suite failures are reported separately.

The 56-target wider existing suite completed: **52 targets passed, four failed**. Additional image and whole-tree stack checks also failed. Failures observed outside the changed input paths: an unresolved scaled-text stub in the click-routing fixture; a launcher test expecting an older Makefile variable spelling; the 16-KiB BWS test obtaining no `[STACK METRICS]`; a desktop-file fixture expecting `matchedFiles==17`; and a PNG test requiring `obrazy/tapety/nowatapeta.png`. The full stack audit also reports pre-existing frames above 6,144 bytes in PSF move, VirtIO block stress and Shell. These are not suppressed or changed to manufacture passing input results.

The existing QEMU recipes were also run: Kontrolki 4 passed; VirtIO block reached the 650-second harness bound; the other twelve legacy groups failed boot/setup, serial evidence or visual expectations. `test-rozruch-qemu` is the same recipe as the failed OTA acceptance test. Per-group status, duration and output are preserved in `.input-test/full-qemu-final/results.json` and its logs. The test runner used snapshots of the newly created blank VM data disk and host watchdogs (210/360 seconds per VM, 650 seconds per group, 1,250 for the differential matrix) to bound older blocking readers. These failures are not evidence of a clean full suite, and their underlying production causes have not all been established here.

Parallel verification also hit the new USB fixture's host watchdog after both interfaces were armed, and the HDA preferred-medium fixture observed an incomplete codec snapshot (`CODECS=0`). Sequential reruns on the exact final kernel passed **USB input**, **VirtIO input** and **both HDA WAV/null backends**. The HDA tone stops through normal deferred service with `RUN=0`. Parallel failures remain recorded; no tolerance was raised to turn them into passes. The successful input runs use a 240-second boot budget; the HDA rerun retains the existing 180-second command budget and 100-ms stop tolerance. See `.input-test/validation/input-usb-final-runtime.log`, `input-virtio-final-runtime.log` and `input-hda-isolated.log`.


## GA401IV test procedure and exact evidence

Boot the final production ISO with external USB mouse/keyboard available. Capture the complete serial log from controller discovery through desktop input, including failure lines; `wypisz_log` uses the existing serial sink. `/logi/pci.txt` is the existing supplemental PCI inventory, not a replacement for the USB enumeration log.

Look for this sequence:

1. `[xHCI controller] index=N BDF=... vendor=0x1022 device=0x1639 ... result=detected` for Renoir, followed by its nonzero BAR, ports, slots, version and `result=ready`. Every additional controller, including NVIDIA if present, must have a separate index/result. IDs are diagnostic references, not selection rules.
2. `[USB root port] controller=N port=P` and `connected PORTSC=...`. Disconnected change bits may also be logged once on startup.
3. `[USB device] controller=N port=P slot=S VID=... PID=... class=... configuration=... interfaces=... HID interfaces=...`. ASUS N-KEY, if present, may appear as `VID=0xB05 PID=0x1866`.
4. `[USB interface]` for **all** interface descriptors, including non-HID classes and alternate settings. `[USB HID descriptor]` gives each collected interface's subclass/protocol, endpoint address, MPS, bInterval and report length. Keyboard boot capability is subclass 1 / protocol 1; mouse boot capability is subclass 1 / protocol 2. Do not assume interface 0 is the keyboard.
5. `[USB HID] ... interface=I endpoint=... DCI=... ... result=endpoint configured`, then `result=interrupt IN armed`. These lines must exist independently for useful interfaces; unsupported report/endpoint errors must not erase the later ones.
6. Press/release a letter and move/click each external mouse. The first four reports per interface produce `interrupt completion`, completion code/residual and `keyboard report decoded` / `pointer report decoded`. Keyboard reports are not dumped as raw key bytes. `[INPUT REGISTER]` must identify the USB keyboard/mouse, and pointer activity may produce `[INPUT OWNER]` and `[MOUSE BOUNDS]`.
7. Preserve `[xHCI timeout]`, `[USB command timeout]`, `[USB enumeration failure]`, `fatal controller status` / `USBSTS`, `endpoint rejected`, `unsupported report descriptor` and `interface unavailable; continuing` lines. Include the controller/port/slot/interface identity immediately before each error.
8. Check external devices on every physical USB port; disconnect/reconnect them. Confirm no stuck modifier or mouse button, no duplicate stale reports, and continued operation of devices on the other controller. Repeat PS/2/Acer tests and QEMU VirtIO input tests before asserting physical compatibility. The complete test suite is not green; review the compatibility failures above separately.

## Remaining limits and HID-over-I2C integration

- Fixed bounds: eight controllers, 64 enabled slots/controller, 255 root ports/controller, 16 retained HID interfaces/device and 64 registered input sources. Excess capacity is logged/rejected safely. Only the first advertised configuration, default alternate setting zero and the first usable interrupt-IN endpoint of each interface are activated. The root-port driver still has no downstream USB hub enumeration.
- Generic HID pointer layouts retain their existing supported subset. Consumer control and non-boot keyboard roles can be identified, but unsupported report layouts are skipped rather than decoded as boot input. This change does not implement consumer key delivery or a new general NKRO keyboard parser.
- A mouse can legitimately leave interrupt IN pending without reports; it must not be timed out solely for inactivity. Controller reset, ownership, command, control and port-reset operations have bounded deadlines; startup register waits additionally have a spin bound. HPET remains required for safe xHCI initialization.
- No physical ASUS/Acer claim is made. Successful QEMU input and modeled AMD PCI discovery do not establish electrical/firmware behavior of the GA401IV, internal hubs, firmware routing or power management.
- The codebase has ACPI RSDP/RSDT/XSDT, HPET and limited FADT/DSDT shutdown/reset handling, but no I2C host driver or ACPI device/resource namespace evaluator. HID-over-I2C is therefore deliberately not implemented.
- A later I2C transport should enumerate ACPI HID devices/resources, obtain the HID descriptor and report descriptor, manage power/reset and IRQ GPIOs, then publish through `InputDevice`, `input_register_device`, `input_arm_pointer` / `input_prove_pointer`, `input_dispatch_events` and `input_unregister_device`. Its DMA/report lifetime and decoding state must remain per source, like the new USB interfaces. `hid_pointer_compile` / `hid_pointer_decode` are transport-independent; `hid_pointer_register` currently supplies USB identity and should be split from bus identification before I2C reuse. `input_set_pointer_ready` must remain the common display barrier, independent of any transport probe. The GA401IV touchpad may consequently remain unavailable in this update.

## Artifact identity

The kernel extracted from the final ISO compares byte-for-byte with the kernel used by the passing USB and VirtIO runtime checks.

- `system_operacyjny.bin`: SHA256 `f26c8e3601100a14f5ae224e3820ebec0da84df29687c4e7312176251ed9d331`.
- `BursztynOS.iso`: SHA256 `c7ca61d8508cdaf49e1383934e2c48c55f5a940079ffcdcc7803cc43f97a2abd`.

The largest changed xHCI stack frame is 1,840 bytes in deferred enumeration, followed by 1,440 bytes in HID report service. All 157 expected kernel C/C++ translation units have stack-usage coverage; the three whole-tree threshold failures are outside these changes.
