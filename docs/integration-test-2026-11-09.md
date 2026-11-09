# main + UNITRONIX integration on test — 2026-11-09

The existing conflicts have been resolved in the working tree on `test`. Production build and all focused checks listed below pass, including the final host/audio and expanded native BWS reruns. No commit or push is authorized or performed.

## Scope and preservation

The initial branch was `test`, HEAD `954f5ddf1a44b999bc27ddab5376c430d08a203a`, with an existing merge of `origin/UNITRONIX`, `f28e346c985910f18b145adbc90781355f1c4e9f`. That merge was continued directly; it was neither aborted nor restarted. The protected `main` and `integration/bur-v2-aslr-bsf-unicode` refs were not modified. No KASLR work was started.

The original index (including stages 1/2/3), merge metadata, refs, binary diffs, conflicted working files and all eight original untracked files are preserved outside the repo at `/home/programista/Pulpit/Burstzyn/integration-safety-20261109-075803`. The original untracked files remain at their original paths. Their SHA-256 identities and preservation checks are in [untracked-files-preserved.json](integration-test-2026-11-09/untracked-files-preserved.json). No user disk image is used for the production boot audit; it receives a separate sparse scratch disk under `/tmp/bursztyn-production-*`.

Generated dependency files referred to pre-move source paths. They were preserved in the safety directory and regenerated. Generated executable, object and ISO outputs were removed from the Git index and retained on disk. Source tests, fuzzers, fixtures, diagnostic logs and historical metrics are retained. Generated `.hda-test/repair/*_actual.inc` files are extracted from production sources by the existing test runner; their source of truth remains versioned.

## Conflict resolutions and related integration repairs

| Area | Resolution |
| --- | --- |
| Whole-file conflicts | UNITRONIX used CRLF in several files. Stage 1/2/3 inputs were compared with normalized line endings and merged in place. This recovered compatible independent changes instead of selecting an entire branch. Changed text was normalized for whitespace validation. |
| `jadro/fs/loader.cpp` | Retained v1 parsing and v2 validation, relocations, entropy requirements, separate text/rodata/data/guard pages and protected canary. Integrated signature verification before address-space/stack allocation and UNITRONIX's explicit-PML4 mapping. |
| Stack ASLR | v2 uses its multi-region layout; v1 uses UNITRONIX's randomized legacy stack window and 256 KiB stack. Initial RSP, mapped stack pages and per-PID stack bookkeeping use the same selected base. |
| `jadro/mm/vmm.cpp` | Retained NX and W^X checks, added explicit target-PML4 v2 mapping, kept active-PML4 compatibility wrapper. Both mapping APIs reject W+X, missing NX support, invalid flags/alignment, invalid user addresses and replacement of user mappings. Pages are populated through supervisor aliases before final user permissions are published. |
| `jadro/bws/bws_api.h` | Preserved HTTP64/HTTPS64 numbers 94/95. Allocated process-list/kill to 96/97; first free is 98. Updated callers, documentation comments and ABI assertions. UNITRONIX process-control binaries must be rebuilt against the integrated ABI. |
| `jadro/bws/syscalls.cpp` | Combined BSF trusted-asset and per-process permission checks with UNITRONIX kernel-buffer/copy-to-user reads. Retained 64-bit network buffers. Copied title masks into kernel memory before compositor use to support SMAP. |
| Permissions/process accounting | Preserved manifest-based permissions and privileged-operation checks. Process-list heap accounting uses the per-process v2 randomized heap base instead of the fixed v1 constant. |
| Legacy SSP + SMAP | Scoped legacy user-data canary initialization within the existing SMAP access guard, closing it before restoring the address space and interrupt state. v2 canary remains read-only and NX. |
| `bsf/runtime.cpp` | Retained native BSF asset-path validation and corrected the moved GUI include. Unicode 15.1 tables and cluster-aware fallback remain intact. |
| `Makefile` | Combined moved source/test paths and UNITRONIX fixes with v2 conversion, PIE link flags, shell BSF terminal object and security checks. Fixed a duplicated directory component in the generated release-key dependency. Added the common include directories to affected host targets. |
| App linker scripts | Reconciled source headers with legacy layouts: Explorer gets the compatible larger text budget, browser uses UNITRONIX's larger budget, wallpapers retain the larger main budget. Diagnostic/audio v1 scripts use measured section addresses/sizes and retain the required main budgets. v2 apps continue through the shared v2 linker/converter. |
| GUI | Combined independent GUI/WM changes after line-ending normalization. Preserved Unicode-aware shell/editor behavior. Settings retains font-family deduplication, font refresh/import and UNITRONIX navigation/power fixes. |
| Installer | Retained acknowledged mouse clicks and settling, while using the compatible QEMU PPM screenshot path and optional PNG conversion. Updated moved observer includes. |
| Tests | Preserved the v2 fuzzer and rollback cases, migrated loader/VMM extraction to moved files and explicit-PML4 APIs, retained both trace configurations, fixed Python imports of shared tooling that remains under `narzedzia/`, and kept independent BWS, browser and VirtIO test changes. UNITRONIX splash tests reflect its changed animation contract rather than obsolete main snapshots. |
| Historical fuzz metrics | Both pre-integration runs are stored separately under `docs/security-hardening-2026-11-04/fuzzing-metrics-*-pre-integration.json`. The main metrics file records a new actual integrated run; historical numbers were not combined into a fabricated run. |
| Package signatures | Fixed a discovered `.podpis` suffix bounds error and rejected trailing signature bytes. Added actual RSA/SHA-256 host verification and loader rejection/cleanup coverage. Test keys are generated only under `.signature-test`; production release trust is unchanged. |

## Security invariants

- BUR v1 remains supported alongside relocatable v2. v2 retains required security flags, bounds and checksum validation, overflow checks, restricted relocations and secure entropy failure handling.
- Ring 3 ASLR covers code, stack, heap, canary address and shared mappings. There is no kernel ASLR implementation in this change.
- User code is RX; rodata/canary pages are RO+NX; writable data/heap/stack pages are NX. Mapping helpers reject fresh W+X mappings as well as attempts to overwrite existing user pages.
- SMEP and SMAP feature probing and CR4 activation remain enabled. User-buffer copies and the identified direct-access paths use the established validation/access mechanisms.
- Kernel SSP, per-process SSP, kernel-stack canaries and unmapped guard pages remain enabled. SSP failures preserve the established Ring 3 containment and Ring 0 fail-stop policies.
- BWS validates sizes, integer arithmetic, pointer ranges and access permissions. Process controls require the appropriate trust level and `PRAWO_SYSTEM_CONFIG`.
- App RSA-2048/SHA-256 verification remains fail-closed for required signatures. UNITRONIX's existing policy trusts system paths installed from the system medium and provides `bursztyn.podpis=wymagaj` to require signatures for all apps; this integration does not silently broaden that trust model.
- Unicode 15.1, native BSF, grapheme boundaries, cluster-aware fallback, font limits and corrupt-font fallback remain present.

## Verification record

Logs are collected under [integration-test-2026-11-09](integration-test-2026-11-09/). Some early logs intentionally record failed attempts; see the distinctions below rather than interpreting every historical failure as a final failure.

| Check | Result observed |
| --- | --- |
| Production kernel and ISO; Settings blob | Built successfully; BSF ISO assets verified. |
| SSP host | 2,007 assertions passed in sanitizer and compiler-protector configurations. |
| BUR v1/v2 + ASLR host | Passed, including 10,000 layout trials. |
| Eight security fuzz targets | 45,096 executions, zero crashes, ASan/UBSan/coverage enabled. |
| Production loader/installer/VMM/audio host | Both trace variants passed with 31 scenarios, 11,289,212 checks each and 1,165 frame/mapping failure stages, plus missing-NX/entropy denial, strict-signature rejection before allocation and fresh-page NX/W+X/invalid-flag assertions. The complete GUI/audio/BWS follow-up also passed. |
| Audio GUI/BWS | 29 GUI scenarios/2,662 checks; eight BWS/HDA scenarios/2,161 checks passed. |
| HDA diagnostic/isolation | 241 and 160 tests respectively, zero failures. |
| BSF engine/UI | 12,204 and 9,084 checks passed. |
| Unicode official conformance | 1,187 Unicode 15.1 GraphemeBreakTest cases and 10,000 malformed-input mutations passed. |
| Unicode notepad | 60,662 checks passed, including edit/save/reopen and backward navigation. |
| Font resources | 4,604 checks passed, including expanded catalog, large fonts, budgets and allocation failures. |
| Native shell/keyboard | 2,201 shell checks and keyboard-repeat scenarios passed. |
| WM routing | 229/229 passed. |
| Installer storage | 10,512 checks passed, including fault cases, partition sentinels and large GPT geometry; file/directory move checks passed. |
| App signatures | Valid RSA accepted; altered executable/signature, missing, short and oversized signatures rejected; 520 path boundaries checked under ASan/UBSan. |
| Native QEMU BWS abuse | All six categories passed; expanded rerun also denies unprivileged process listing/termination at new syscall numbers 96/97. |
| Native QEMU SSP | Ring 3 containment/survivor and Ring 0 fail-stop passed. |
| Native QEMU kernel stack guard | Hardware unmapped-page fault and required diagnostic fields passed. |
| Native QEMU BUR v2 | PASS: 47 executions and 47 distinct values in each of code/stack/heap/canary/shared-mapping columns, all six write/execute/SSP negative probes, legacy FPU parent/child isolation, and symbolicated faults. Register assertions verified CR4 `0x00300678` and EFER `0x0d01` (SMEP, SMAP, NX enabled). |
| Production GUI boot/cursor | PASS: normal boot, RNG/BLK/GPU/network, zero-write disk SHA-256 audit, desktop, initial cursor, moved cursor and restored background. Used disposable disks and the ordinary production ISO. |

## Failed attempts and outstanding risks

1. Initial host compilation exposed stale include/build paths after the directory moves. These were corrected, and the affected completed host checks pass. Two manually requested target aliases did not exist (`test-hda-bws-isolation`, `test-hda-diag-host`); the real `test-hda-diag` target was subsequently run successfully.
2. An initial shell test was interrupted while investigating its runtime. A debugger showed normal Unicode layout work; the complete native-BSF shell run subsequently passed. No security check was removed.
3. The initial BUR v2 QEMU launcher could advance its negative-test sequence when BWS delivered an argument to an existing instance. The harness now waits for process exit through the permission-checked process-list ABI; all negative modes are observed. Its 300-second run subsequently timed out waiting for the legacy FPU parent after the child reported success. A longer TCG budget retained every original assertion and the complete run passed.
4. The first production boot audit timed out before kernel output. Its CD boot order was made explicit. The rerun used isolated disks and longer host timing budgets. It then exposed a fixed 0.5-second screenshot assumption: input/compositor processing had not finished. A bounded poll still requires the exact moved cursor and restored old pixels; the final run passed every original device, desktop and cursor assertion.
5. Native verification gates listed above completed successfully. This is focused integration validation; the entire nightly/extended-fuzz matrix, disk-install/reboot font acceptance and physical-hardware matrix were not run. Those remain additional release coverage rather than claimed results. The merge is not committed.
6. The expanded BWS guest rerun reached its original 240-second limit during UEFI boot before any kernel output. It passed with the configurable 900-second TCG budget; all pointer, permission and protection assertions remained enabled.
7. The inherited system-path signature trust model depends on protected system paths and trusted installation media; requiring signatures for every app remains an explicit stricter boot policy. Host signature tests use the compatible host Mbed TLS 2.x library; production links the repository's Mbed TLS implementation.
8. One newly added host assertion initially required that no other process have mapped pages even on successful multi-process launches. It was corrected to check the rejection-before-allocation cases; successful strict-signature launches still require both verification calls. These failed test-development attempts are retained in the evidence logs.
9. Host and emulated device tests do not establish behavior on every physical GPU/USB/audio controller. Available storage space is limited; large duplicate integration workspaces were avoided while preserving the index and all conflict/user files.

## Final repository checks

- No unmerged index entries or unresolved conflict markers remain.
- Both working-tree and staged `git diff --check` pass.
- All refs match the initial snapshot; all eight original untracked files match their preserved SHA-256 hashes.
- The initial conflicted working files were also compared to raw stage-1/2/3 merge output from the saved index: no additional pre-existing edits outside the merge output were found.
- Production artifact hashes and evidence mappings are recorded in [verification-summary.json](integration-test-2026-11-09/verification-summary.json). Logs are normalized to LF with trailing whitespace removed for Git validation; original `/tmp` log hashes are recorded separately.
- Some inherited compiler warnings remain (including misleading indentation and unused parameters); no warning policy or security checks were relaxed.

## Handoff rule

The merge remains in progress on `test` with all conflicts resolved. Committing/finalizing it requires the user's explicit approval. No merge into `main`, push, or update to either protected development branch is part of this work.
