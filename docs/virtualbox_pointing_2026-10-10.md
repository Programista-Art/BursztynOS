# VirtualBox pointing devices — engineering report

Date: 2026-10-10. Started from clean commit `721be50` in the current worktree. No VirtualBox, QEMU or nested virtualization was launched. Runtime acceptance is pending.

## Confirmed defects and scope

The PS/2 audit found these concrete defects:

1. `obsluga_przerwania_klawiatury()` read port `0x60` whenever OBF was set, without checking AUX in status port `0x64`. IRQ1 could therefore consume an auxiliary motion byte and pass it to the keyboard decoder. IRQ12 already checked AUX. A lost mouse byte can shift subsequent packet framing. The new controller test executes both production IRQ handlers and proves that neither consumes the other device's bytes.
2. Both the PS/2 and xHCI Boot Mouse paths allocated `InputEvent evs[6]`, but X + Y + wheel + three buttons + SYN_REPORT require **seven** entries. This was a real stack buffer overflow for a wheel-bearing diagonal report. PS/2 now uses a sufficiently sized array; USB pointing reports use the bounded descriptor decoder.
3. IntelliMouse sample-rate negotiation ignored all six transaction results, and ID detection treated **every ID >= 3** as four-byte mode. Failures could leave protocol length uncertain. Explorer's fourth byte was decoded as an entire signed byte, mixing additional buttons into the wheel value. Transactions are now checked, only IDs 0/3/4 are accepted, and ID 4 has its own wheel/button decoding.
4. Initialization discarded keyboard bytes while waiting for mouse ACKs, and flushed both input sources indiscriminately. Keyboard scancodes are now forwarded, with a controlled keyboard-port disable/drain around the controller configuration response.

The original PS/2 X/Y arithmetic **already used header sign bits to extend nine-bit counts**; the audit did not establish a simple standalone unsigned `0xFF -> +255` conversion. Existing first-byte bit-3 synchronization and overflow rejection were also present. They have been retained in a separate, tested state machine, with explicit signed-byte decoding and sign-consistency rejection. The exact contribution of each confirmed defect to the user's particular VirtualBox trace cannot be distinguished without that trace; this report does not claim runtime root-cause verification.

APIC/IDT routing remains IRQ1/vector 33 and IRQ12/vector 44, with central EOI. No routing defect was found in those paths for the existing legacy-IRQ model. No APIC or IDT changes were made.

The USB audit found two independent gaps: the project only had an xHCI transport, and its HID interface selection required Boot subclass/protocol and fixed report layouts. The examined VirtualBox USB Tablet descriptor is HID class 3, subclass 0, protocol 0, with descriptor-defined absolute axes, buttons and wheels; the old Boot filter excluded it. This is visible in [Oracle's UsbMouse.cpp](https://github.com/VirtualBox/virtualbox/blob/main/src/VBox/Devices/Input/UsbMouse.cpp). Recognition now uses USB class descriptors and HID usages, without VID/PID matching.

## PS/2 implementation

`Ps2MouseParser` is hardware-independent and deterministic:

- Command phase: streaming is disabled; ACK, RESEND, BAT and ID bytes are not motion input. Initialization performs F5, F6, acknowledged F3/rate pairs 200/100/80, F2 + validated ID, then F4 + ACK. Each controller wait is bounded to 100,000 polls; RESEND is bounded to three attempts.
- Header phase: accept only AUX bytes with OBF and first-byte bit 3. Keyboard bytes do not advance framing. Controller parity/timeout errors clear the partial packet.
- Payload phase: collect exactly two bytes for ID 0 or three for IDs 3/4. Do not apply header synchronization checks to payload bytes. Incomplete packets produce no report.
- Completion: reject overflow and incompatible axis sign bits; decode X by explicit signed-byte extension and Y by its negative. This conservatively drops nine-bit movement values outside the requested signed-byte subset instead of emitting ±255 from inconsistent data. No malformed packet resets the cursor.
- ID 0: three-byte relative mouse; ID 3: four bytes, signed eight-bit wheel; ID 4: four bytes, signed four-bit wheel and side/extra button bits. IDs 0/3/4 are parsed; the negotiated knock is IntelliMouse 200/100/80, and Explorer is parsed if ID 4 is reported. Unknown IDs fail initialization cleanly.

IRQ12 drains at most 64 AUX bytes. It only decodes and publishes complete reports into a fixed 64-slot SPSC ring; it does not allocate, log, call graphics or dispatch GUI input. Release/acquire publication protects ring entries. A sequence-tagged atomic overflow snapshot preserves the latest real button state if overload drops reports. Deferred service discards obsolete entries after recovery and delivers no fabricated recovery motion. This prevents a dropped button-release packet from leaving a click held.

The kernel's existing idle service now calls `input_service_all()`. PS/2 is registered during controlled initialization, and its poll callback drains the ring outside IRQ context. PS/2 keyboard registration is also completed before USB startup, avoiding a first-use keyboard IRQ mutating the device registry during USB service.

Existing InputDevice relative accumulation saturates through 64-bit intermediates. Graphics already accumulated positions in `int64_t`; it now also rejects nonpositive screen dimensions before clamping. Bounds come from the active display, with X/Y clamped to `[0, dimension-1]`. There is no fixed resolution or backend-wide coordinate compensation.

## USB transport and enumeration

Two real transports feed the same HID parser and InputDevice path:

| PCI USB programming interface | Transport | Work in this change |
|---|---|---|
| `0x30` | xHCI | Existing transport extended for descriptor-defined pointing devices |
| `0x10` | OHCI 1.0 | New root-port low/full-speed transport |

**The tested VM's actual controller cannot be identified from this worktree:** no `.vbox` file, PCI runtime log or controller setting was supplied. The controller question remains unanswered. The implementation supports both paths above; it does not assume xHCI or claim standalone EHCI/UHCI support. OHCI is necessary when the tablet is exposed on an OHCI controller. Startup logs identify which controller was found. No VM configuration or launcher was changed.

OHCI implements controller ownership/reset, MMIO and PCI bus mastering, HCCA, control ED/TD chains, root-port power/reset, address assignment, endpoint-zero packet sizes, and a periodic interrupt-IN schedule. Its wire structures follow OHCI's ED/TD/HCCA model; the hardware layouts can also be compared with the [Linux OHCI definitions](https://github.com/torvalds/linux/blob/master/drivers/usb/host/ohci.h). It uses polled done queues, real control transfers and alternating interrupt TDs, not a synthetic pointer source.

OHCI limits are 15 root ports, 16 device records, 4 KiB for device/transfer DMA pages, 32-bit DMA addresses, and the first matching controller per backend, and one selected HID interface per device. All waits have a one-second HPET deadline and a finite poll ceiling. Descriptor/control transfers remain serialized in the normal USB service. Interrupt reports never block waiting for the tablet; NAKs leave a pending transfer. Invalid completions or controller errors withdraw pointer readiness and restore fallback.

The shared configuration parser validates the received configuration header and total length (maximum 4096), every nested descriptor length, HID interface, HID subordinate descriptor list, report-descriptor length, and interrupt IN endpoint. It supports HID class interfaces beyond Boot devices. It requests the real report descriptor with interface GET_DESCRIPTOR, requires the declared number of bytes, and compiles it before report delivery. Boot keyboards retain their Boot protocol path; Boot-capable pointers explicitly enter Report protocol. The non-Boot tablet is not sent SET_PROTOCOL BOOT.

xHCI additionally fixes control short-packet accounting, associates interrupt completions with the exact outstanding TRB, ignores stale completions, and stops requeueing halted endpoints. Low/full-speed interrupt intervals now accept the full descriptor range and round down to a power-of-two period; high/super-speed intervals retain exponent decoding. This matches the conversion documented in the [Linux xHCI implementation](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-mem.c). Port enumeration cannot wrap an eight-bit loop variable at 255 ports.

## HID compilation and runtime decoding

Parsing follows the short-item and report model of [USB HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf). The implementation compiles only validated runtime fields; it never reparses a descriptor during an interrupt or report completion.

Supported items: Usage Page, Usage, Usage Minimum/Maximum, Logical Minimum/Maximum, Report Size/Count, Report ID, Global Push/Pop, Collection/End Collection, and Input flags for Constant/Data, Array/Variable and Relative/Absolute. Physical/unit metadata is safely ignored. Unsupported long items, local delimiters, invalid stacks and ambiguous duplicate controls fail compilation. Unknown usages consume their declared input bits but do not generate events.

Supported controls are Generic Desktop Mouse/Pointer collections with X, Y and wheel; Button usages 1–5; and Consumer AC Pan for horizontal wheel. Relative and absolute axes are distinguished by Input flags. Buttons use normalized InputDevice key codes; side/extra state is now retained by InputDevice alongside left/right/middle. HID wheels are relative and signed according to their logical range.

Hard limits:

| Resource | Limit |
|---|---:|
| Report descriptor | 4096 bytes |
| Parsed items | 1024 |
| Report Size | 1–32 bits |
| Report Count | 1–128 |
| Input report | 512 bytes, including ID when present |
| Distinct input report IDs | 16, IDs 1–255 when numbered |
| Compiled recognized fields | 64 |
| Local explicit usages | 128 |
| Collection depth | 16 |
| Global Push depth | 4 |

Recognized logical ranges must fit their declared field width and the signed 32-bit InputDevice ABI. Full unsigned 32-bit coordinate ranges above `INT32_MAX`, array-based pointer controls and multi-packet interrupt reports exceeding the selected endpoint's packet size are not supported; such devices fail cleanly. OHCI additionally enforces low-speed 8-byte and full-speed 64-byte endpoint limits. These limits do not affect the examined VirtualBox tablet's eight-byte report.

Bit offsets include constants and unknown fields and are tracked separately for each report ID. ID bytes are removed before bit extraction. Fields may cross byte boundaries and need not be 8/16-bit aligned. Signedness follows Logical Minimum; arithmetic uses wide intermediates. A report shorter than its compiled layout, an unknown ID or invalid relative/button value produces **no partial dispatch**. Extra trailing bytes are ignored. No descriptor-controlled allocation is performed by the HID compiler or decoder.

Absolute raw values are clamped to their logical ranges and passed through InputDevice's existing 64-bit mapping:

```text
pixel = (clamped_raw - logical_min) * (screen_dimension - 1)
        / (logical_max - logical_min)
```

Invalid spans are rejected before device readiness. InputDevice additionally checks ranges and dimensions and clamps mapped pixels. Negative/nonzero minima, 1×1, 1280×720, 1920×1080 and `INT_MAX` dimensions are host-tested. USB absolute Y is delivered directly: the descriptor defines an absolute Y coordinate, and the backend does not apply the PS/2 relative-Y inversion.

## Ownership, lifetime and concurrency

Existing priorities are unchanged: VirtIO 30, USB 20, PS/2 10. USB pointer registration occurs only after transport configuration, successful descriptor compilation and the **first valid real interrupt report**. Registration itself does not move the cursor. The actual first absolute report may position it normally.

Only the current pointer owner commits motion, wheel and button changes. PS/2 reports continue to be consumed while USB/VirtIO owns the pointer, but do not move or click the cursor. Failed USB enumeration/report compilation leaves PS/2 owner. Disconnect/error releases the departing USB owner's buttons through InputDevice, unregisters it and restores the best remaining owner, without resetting the pointer location. No duplicate source is added outside InputDevice.

Pointer registry mutations and USB completion processing occur in the serialized kernel service/startup path. PS/2 pointer IRQs communicate only through the SPSC queue and atomic overflow snapshot. Descriptor parsing and USB report dispatch occur outside hard IRQ. There are no new allocator/controller/PCI/InputDevice locks held across GUI dispatch.

OHCI DMA and device objects have controller lifetime. Failed enumeration and unplug retain them, with skipped endpoints, so stale hardware references cannot access freed memory. A controller failure stops schedules and retains DMA. OHCI disconnect restores fallback; **new attachment/reconnection and external hubs are not implemented**. A guest reboot re-enumerates root ports. xHCI retains its existing root-port reconnect path, unregisters pointer objects before cleanup, and frees DMA only after successful Disable Slot. Failed control-transfer quiescence intentionally quarantines its DMA buffer. These are bounded lifetime policies, not partial unsafe hotplug cleanup.

## Diagnostics

Both flags default to zero:

```text
BURSZTYNDIAGPS2=1
BURSZTYNDIAGUSBHID=1
```

PS/2 records at most 128 byte/framing snapshots, including raw packet bytes, decoded dx/dy, overflow and rejection counts. IRQ only publishes fixed records; deferred service formats/logs them. USB logs interface/endpoint/report metadata, compiled ranges/IDs/offsets and at most 32 decoded reports. VID/PID are diagnostic information only. No diagnostic memory allocation occurs in IRQ.

Diagnostic image: `BursztynOS-pointing-diag.iso`. Normal image: `BursztynOS.iso`, diagnostics disabled. Neither was launched.

## Validation

Final host runs use ASan and UBSan. Pointing runs set `ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1`. Installer and some inherited graphics tests retain their existing leak-detection settings.

| Suite | Result |
|---|---:|
| PS/2 deterministic parser | 330,443 checks, PASS |
| Production i8042 / IRQ1 / IRQ12 / initialization / SPSC overload | 4,132 checks, PASS |
| HID descriptor compiler / report decoder | 6,051 checks, PASS |
| USB configuration descriptors | 48 checks, PASS |
| Production OHCI enumeration / transfers / failure / unplug | 444 checks, PASS |
| Production xHCI pointer / short control / stale completions / intervals | 33 checks, PASS |
| InputDevice ownership / mapping | 99 checks, PASS |
| Same pointing suites, diagnostics enabled | PASS, same totals |
| Existing VirtIO PCI/input suite | 166 PASS, 0 FAIL |
| VirtIO boot-order/caller test | 1 PASS, 0 FAIL |
| Real heap/PMM/PCI initialization locks | 4 PASS, 0 FAIL |
| Existing concurrency audit/host suite | 10 PASS, 0 FAIL |
| Installer Storage/BSP2 | 10,493 PASS, 0 FAIL |
| Native directory/file move | 138 assertions, PASS |
| Production BWS move authorization | 438 assertions, PASS |
| Production keyboard / TEXTINPUT / dialogs | 7,291/7,291 PASS |
| Existing cursor host and diagnostic suites | PASS |
| ASan | No findings |
| UBSan | No findings |
| Final `make -B -j2 all iso` | Exit 0, no compiler warnings or errors |
| Diagnostic ISO build | Exit 0 |

PS/2 includes all 65,536 signed X/Y byte combinations, button combinations, overflow, bad headers/signs, incomplete packets, random garbage/recovery, IDs 0/3/4, command replies, keyboard/AUX routing and a concurrent producer/consumer. HID includes numbered/unnumbered and multiple IDs, unaligned axes, signed/nonzero ranges, 8/16/32-bit fields, malformed/truncated/oversized descriptors, short/extra reports, unknown usages and bounded descriptor fuzzing. The Oracle tablet descriptor wire bytes are an offline regression fixture. OHCI tests execute the production enumeration and transfer code against fake registers/DMA; they are not a hardware or emulator acceptance run.

Logs are preserved in `.pointingtest/logs/`.

## Files

Production files modified: `Makefile`, `mysz.cpp`, `klawiatura.cpp`, `input_device.cpp`, `grafika.cpp`, `kernel.cpp`, `sterowniki/usb/usb.cpp`, `sterowniki/usb/xhci.cpp`.

Production files added: `ps2_mouse_parser.h`, `pointing_diag.h`, `sterowniki/usb/hid_pointer.h`, `sterowniki/usb/hid_pointer.cpp`, `sterowniki/usb/ohci.h`, `sterowniki/usb/ohci.cpp`.

Test files added: `narzedzia/test_pointing.cpp`, `narzedzia/test_ps2_controller.cpp`, `narzedzia/test_ohci.cpp`, `narzedzia/test_xhci_pointer.cpp`, `narzedzia/pointing_host_stubs.h`, `narzedzia/pointing_test_descriptors.h`, `narzedzia/virtualbox_tablet_descriptor.h`. Existing source tests were rerun without modification.

Documentation added: this report. Generated objects, dependencies, stack-usage files, test executables, staging files and ISO images are build artifacts. The previously tracked test binary and tracked generated dependency files are restored after verification; new header dependencies are explicit in Makefile.

No changes were made to `runwindows.ps1`, `rununix.sh`, `rununix_virtio_latest.sh`, disk images, VirtIO input production sources, installer confirmation/storage policy or GUI application code. `grafika.cpp` only gains the invalid-display-dimension guard. No clean/rm build target was invoked.

## Manual acceptance still required

1. VirtualBox PS/2 Mouse: smooth movement in all directions, wheel and buttons, no upper-left jumps, edge teleports or framing loss during movement/clicking.
2. VirtualBox USB Tablet: confirm the detected controller path, real HID enumeration and descriptor logs; verify full-screen absolute tracking, buttons and wheel at multiple resolutions in both Live and Installer.
3. Installer: verify zero disk writes before confirmation and normal installation/restart.
4. Existing unchanged QEMU configuration: verify VirtIO keyboard/tablet behavior remains correct.
5. If checking unplug, confirm fallback pointer use and no held click. OHCI reconnection requires a guest reboot; external hubs and standalone EHCI/UHCI are outside this implementation.

No runtime PASS is claimed.

## Final artifact verification

Both ISO kernels pass Multiboot2 validation. The normal ISO kernel exactly matches local `system_operacyjny.bin`. New PS/2/HID diagnostic format markers are present in the diagnostic kernel and absent from the normal kernel. Both images have identical GRUB configurations.

| Artifact | Bytes | SHA-256 |
|---|---:|---|

| `BursztynOS.iso` | 35416064 | `f7bcc4c28269c3c17aa2699c64021a5f5694ed857c397284830772566d372f13` |
| `BursztynOS-pointing-diag.iso` | 35420160 | `335ce587d484c4b013eab9088a54269dcea9145795d054a4571af0e5146da6f2` |
| `system_operacyjny.bin` | 8178008 | `6f586441fe790fd72d6b8537c7b399abd2185a81a2d8247f406b0c94a832047b` |

`runwindows.ps1`, `rununix.sh` and `rununix_virtio_latest.sh` were compared byte-for-byte against HEAD and are unchanged. Their hashes and image verification results are in `.pointingtest/logs/artifacts.json`.

The existing `disp.img` remains 4,294,967,296 bytes with modification time 1790638047, matching the pre-build snapshot. No `wirtualnydysk.img` was present in this worktree, and none was created, modified or removed. Installer tests used their existing RAM backends.

The final full build log is `.pointingtest/logs/production-build.log`. Diagnostic backend compilation and the final diagnostic link/ISO logs are also preserved there. Final whitespace validation passes.
