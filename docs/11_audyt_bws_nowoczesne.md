# Audit and extension of the BWS foundation

Date: 2026-08-30

This document records the state found before the change and the additive ABI
implemented afterwards. Existing BWS numbers are frozen. BWS 57 (`gui_blit`)
and BWS 60 (HPET monotonic nanoseconds) were not changed. BWS 0 was not
implemented, therefore the first free number after the existing contiguous
range was 61.

## Initial audit: requested features

| # | Feature | Initial status | Exact finding / missing part |
|---:|---|---|---|
| 1 | Window resize / geometry | PARTIAL | BWS 33 could recreate a layer and BWS 34 could move it, but there was no transactional own-window `x/y/w/h` operation and applications recreated layers to maximize. |
| 2 | Work-area / monitor information | MISSING | Only BWS 23 returned the framebuffer resolution; no monitor identity or work area existed. |
| 3 | DPI / scaling | MISSING | No scale value was exposed. |
| 4 | Monotonic timer | EXISTS | BWS 60 already returns the HPET monotonic time in nanoseconds, or zero when HPET is unavailable. It does not expose MMIO. |
| 5 | Shared memory | MISSING | No object, mapping, lifetime, or handle model existed. |
| 6 | IPC | MISSING | The per-process GUI event queue was not a message-channel API. |
| 7 | File watching | PARTIAL | Event 22 was a global “something changed” broadcast. It had no path, handle, mask, owner, or unwatch operation. |
| 8 | Async I/O / completion notification | PARTIAL | A bounded event queue exists, but PSF reads/writes and the scheduler have no asynchronous request lifecycle or cancellable worker mechanism. |
| 9 | Rich clipboard MIME/types | PARTIAL | BWS 52–55 handled one legacy file-operation clipboard payload only; there was no format enumeration or bounded arbitrary data. |
| 10 | System-dialog backend | MISSING | There was no IPC broker foundation. No kernel-rendered dialog existed, which is the desired architecture. |
| 11 | Notifications | MISSING | There was no service transport or Ring3 notification service. |
| 12 | Audio stream API | PARTIAL | HDA had a private, polling, fixed 48 kHz/S16/stereo output path and BWS 27 test tone. Missing: owned stream handles, frame writes, capacity query, mixing, and cleanup. |
| 13 | Sockets | PARTIAL | DNS/HTTP/HTTPS BWS 28–31 and internal TCP stream functions existed. The stack supports only one global TCP session and had no owned BWS socket handle. |
| 14 | mmap / shared buffers | MISSING | The loader mapped private BUR/heap/stack pages only; there was no native shared-buffer mapping. |
| 15 | Cursor shapes | PARTIAL | The compositor had one hard-coded arrow overlay only. |
| 16 | Custom cursor | MISSING | No copied, bounded, per-process cursor surface existed. |
| 17 | Text input / IME foundation | MISSING | Legacy KEY events carried character bytes; physical keys and Unicode text were not separate. |
| 18 | Keyboard modifiers | MISSING | Events exposed no SHIFT/CTRL/ALT/SUPER/CAPS/NUM mask. |
| 19 | Mouse wheel | MISSING | No signed horizontal or vertical wheel event existed. |
| 20 | High-resolution pointer events | PARTIAL | Legacy MOVE already contained absolute `x/y`, relative `dx/dy`, buttons and a timestamp and was coalesced. It had no explicit precision event contract, modifiers, or higher-resolution HID decoding. |
| 21 | Application settings / storage | PARTIAL | PSF and PZB can store files, but there was no per-application namespace helper, kernel-enforced application identity, or atomic key/value update primitive. |

## Resulting additive ABI

The canonical number list is in `bws_api.h`; shared public structures and
limits are in `bws_nowoczesne.h`.

| Feature | EXISTED | ADDED | BWS | Notes |
|---|---|---|---|---|
| Own-window geometry | BWS 33/34 foundations | Transactional geometry wrapper and compositor resize | 61 | `PRAWO_GUI`, current PID only, checked dimensions, allocate-before-publish, old/new dirty regions and deferred old-buffer free. |
| Monitor/work area/DPI | BWS 23 framebuffer size | Versioned, multi-monitor-ready record and trusted work-area publisher | 62–63 | One monitor today; scale is 1000. Only a trusted `PRAWO_SYSTEM_CONFIG` GUI service may publish work area. |
| Monotonic time | HPET nanoseconds | Ring3 wrapper only | 60 | Reused; no duplicate syscall. |
| Native SHM / mapped buffers | — | Create/map/unmap/close objects | 64–67 | 4 KiB pages, 2 MiB/object, 8 MiB global, zeroed frames, generation handles, per-PID access, explicit IPC transfer, no PA/MMIO exposure. |
| IPC ports | Event queue | Bounded endpoints and message queues | 68–71 | 512-byte messages, 16 queued/endpoint, 32 endpoints, generation handles, owner-only receive/close, optional SHM transfer and event 28 readiness. Service discovery/protocols remain Ring3 work. |
| File watching | Global event 22 | Watch/unwatch handles, masks and event 27 | 72 | PARTIAL: covers successful mutations made through current BWS file operations, rechecks PZB for objects that still exist, and uses the bounded scheduler queue. Central PSF-internal hooks, destination-path payload for rename/move, and retained pre-delete metadata for final PZB filtering are still missing. |
| Async I/O | Synchronous PSF plus event queue | ABI deliberately not faked | — | PARTIAL: request submission, worker/cancellation lifetime and completion records remain missing. |
| Clipboard MIME | Legacy BWS 52–55 | Enumerate/size/read/set formats | 73–76 | Eight formats, 1 MiB/format, 2 MiB total, kernel copies. Legacy ABI is unchanged. |
| System dialogs | — | IPC transport foundation | Ring3 component | PARTIAL: a broker protocol and Open/Save/Folder/Message service UI are still missing. No kernel UI syscall was added. |
| Notifications | — | IPC transport foundation | Ring3 component | PARTIAL: notification protocol, broker and renderer are still missing. No kernel renderer was added. |
| Audio stream | HDA fixed stream and BWS 27 tone | — | 27 | PARTIAL: owned streams, format negotiation, writes, available-frame query and mixing remain missing. |
| TCP/IPv4 sockets | DNS/HTTP/HTTPS and internal TCP | Owned create/connect/send/recv/close facade | 79–83 | PARTIAL: the underlying stack still allows one system-wide session; no concurrent sockets or readiness event exists. Legacy HTTP is excluded while the socket facade owns that session. |
| Cursor shapes | Arrow overlay | Eight additional system shapes | 77 | Per-process selection; compositor tracks dynamic overlay bounds/hotspots. |
| Custom cursor | — | Copied BGRA/RGBA cursor | 78 | Maximum 64×64; kernel-owned allocation; checked hotspot; old image freed only after replacement succeeds. |
| Text/IME foundation | Legacy character KEY | Physical-key event 23 and Unicode TEXT_INPUT event 24 | Event ABI | Full layouts, dead-key composition and IME service remain future Ring3/system-service work. |
| Keyboard modifiers | — | Six-bit modifier mask | Event ABI | Stored in existing event fields; `bws_zdarzenie` remains exactly 40 bytes. |
| Mouse wheel | — | Signed vertical/horizontal event 25 | Event ABI | PARTIAL hardware coverage: USB HID reports with wheel bytes work; PS/2 wheel negotiation and arbitrary HID report-descriptor parsing remain missing. |
| High-resolution pointer | Legacy MOVE payload/coalescing | Explicit event 26 with modifiers, same timestamped coordinates and coalescing | Event ABI | PARTIAL hardware coverage: current PS/2 and boot-HID reports are still 8-bit deltas; high-resolution HID descriptor decoding remains missing. |
| Application settings | PSF/PZB files | Safe Ring3 namespace-path helper | Ring3 helper | PARTIAL: kernel-enforced app identity, automatic directory provisioning and atomic replacement remain missing. No registry syscall was added. |

## Complete BWS number map after the change

| BWS | Name |
|---:|---|
| 0 | unassigned / unknown |
| 1 | print |
| 2 | create file |
| 3 | write file |
| 4 | get character |
| 5 | read file |
| 6 | list directory |
| 7 | delete object |
| 8 | rename object |
| 9 | RTC string |
| 10 | execute BUR program |
| 11 | ping |
| 12 | legacy DNS |
| 13 | legacy HTTP to file |
| 14 | draw window |
| 15 | draw text |
| 16 | clear GUI area |
| 17 | refresh GUI |
| 18 | read mouse state |
| 19 | refresh desktop |
| 20 | colored/scaled text |
| 21 | rectangle |
| 22 | mouse ownership |
| 23 | screen resolution |
| 24 | glyph width |
| 25 | reset |
| 26 | shutdown |
| 27 | HDA test tone |
| 28 | DNS |
| 29 | HTTP |
| 30 | HTTPS |
| 31 | TLS certificate status |
| 32 | process exit |
| 33 | create/replace own layer |
| 34 | move own layer |
| 35 | grow private heap |
| 36 | shell closed state |
| 37 | event poll |
| 38 | event wait |
| 39 | input capture |
| 40 | system overlay |
| 41 | minimize own layer |
| 42 | window snapshot |
| 43 | activate window |
| 44 | file size |
| 45 | startup argument |
| 46 | create directory |
| 47 | file metadata |
| 48 | move object |
| 49 | drop-target registration |
| 50 | drag update/drop |
| 51 | font height |
| 52 | legacy file clipboard set |
| 53 | legacy file clipboard get |
| 54 | legacy file clipboard clear |
| 55 | copy object |
| 56 | application popup |
| 57 | GUI blit (unchanged) |
| 58 | window title |
| 59 | ranged file read |
| 60 | monotonic HPET nanoseconds (unchanged) |
| 61 | own-window geometry |
| 62 | monitor/work-area/DPI info |
| 63 | trusted work-area publish |
| 64 | SHM create |
| 65 | SHM map |
| 66 | SHM unmap |
| 67 | SHM close |
| 68 | IPC endpoint create |
| 69 | IPC send |
| 70 | IPC receive |
| 71 | IPC endpoint close |
| 72 | file watch/unwatch |
| 73 | clipboard format enumeration |
| 74 | clipboard format size |
| 75 | clipboard format read |
| 76 | clipboard format set |
| 77 | system cursor shape |
| 78 | custom cursor |
| 79 | TCP/IPv4 socket create |
| 80 | TCP connect |
| 81 | TCP send |
| 82 | TCP receive |
| 83 | TCP close |
| 84 | first free number |

## Security and limits

- All Ring3 structures and data are copied through the existing checked
  user-copy helpers. Network result buffers are validated before side effects.
- Size additions/multiplications and page rounding are checked. No syscall
  returns a physical address. SHM uses HHDM only while zeroing frames in Ring0.
- SHM, IPC, watcher and socket handles contain a non-zero generation and reject
  stale generations. SHM additionally has an explicit per-PID access set.
- Dynamic allocations remain bounded by fixed object/queue/byte caps; they
  replace large always-resident BSS reservations and guarantee the
  kernel-below-`0x00E00000` (RING3_BASE 16 MiB - 2 MiB guard) contract.
- Process exit removes layers/cursors, mappings, SHM access, IPC messages and
  endpoints, file watches and the owned socket. Shared PTE leaves carry a
  software marker so generic address-space destruction does not double-free
  SHM frames.
- Boolean Ring3 wrappers test `result == 1`; they do not treat the unknown-BWS
  value `UINT64_MAX` as success.

## Verification matrix

The bundled `test.bur` now runs a Ring3 smoke suite at startup and displays
`BWS 61-83: PASS` only after checking monitor/work-area permissions,
monotonic ordering, valid and invalid geometry, SHM map/zero/unmap/stale
handle paths, IPC send/receive/oversize/bad-pointer/stale-handle paths,
missing file/network rights, MIME clipboard copies, and system/custom cursor
boundaries. The remaining multi-process and destructive boundary cases below
still require a dedicated QEMU run before the ABI is declared
production-stable:

| BWS group | Valid/boundary cases | Negative cases to execute |
|---|---|---|
| 61 geometry | move-only, resize, maximum legal surface | zero/negative/overflow dimensions, no layer, no `PRAWO_GUI`, other PID impossible by ABI |
| 62–63 monitor | count query, record v1, full work area | invalid output, invalid index, bad work rectangle, untrusted/no config right |
| 64–67 SHM | 1 byte, one page, 2 MiB, RO/RW, transfer, unmap/close | zero/overflow/over-limit, invalid flags, stale handle, ungranted PID, duplicate/unmapped VA, process-death cleanup |
| 68–71 IPC | zero/512-byte messages, queue 16, SHM transfer | bad descriptor/data/output, 513 bytes, queue full, wrong-owner receive/close, stale handles, endpoint-owner death |
| 72 watcher | each action and mask, unwatch | bad path/pointer/mask, missing read right, wrong owner, stale handle, queue pressure |
| 73–76 clipboard | empty and 1 MiB formats, enumerate/read/replace | malformed MIME, bad pointers, small output, ninth format, total >2 MiB, missing GUI right |
| 77–78 cursor | every shape, 1×1 and 64×64 custom | invalid type/format/hotspot/pointer, dimensions 0/65, overflow, missing GUI right, exit cleanup |
| 79–83 socket | create/connect, zero/64 KiB send, recv statuses, close | invalid pointers/port/type, 64 KiB+1, wrong owner, missing network right, stale handle, process-death cleanup |

Build and QEMU execution results belong in the change handoff; this document
does not claim that an interactive test was run.
