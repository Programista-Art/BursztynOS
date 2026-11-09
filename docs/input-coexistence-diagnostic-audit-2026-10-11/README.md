# Compilation and host evidence

`before-compilation/` and `after-compilation/` contain complete production-versus-diagnostic command and preprocessed diffs, with per-flag summaries. The direct DEBUG flags cover their xHCI uses. `baseline-xhci.cpp.txt` preserves the starting physical-mouse controller source for the deterministic before/after benchmark; its `.txt` extension keeps source-call-site audits from treating it as production code.

Reproduce current functional parity, including both sanitizers:

```sh
make test-input-coexistence-host
```

Reproduce the comparison with the saved starting controller:

```sh
python3 testy/test_input_diagnostic_parity.py --baseline docs/input-coexistence-diagnostic-audit-2026-10-11/baseline-xhci.cpp.txt
```

Recreate the current compilation audit:

```sh
python3 narzedzia/audit_input_diagnostic_compilation.py --output .pointingtest/coexistence-validation/reproduced-compilation
```

`parity-final.txt` contains final four-mode results and simulated timings. `input-final-tests.txt`, `virtio-final-tests.txt`, and `splash-scheduler-final.txt` contain successful final suite runs. `graphics-host-first-iteration.txt` includes passing graphics/splash unit suites followed by a resolved host-harness build failure; the repaired production splash target's result is in `splash-scheduler-final.txt`. The main report explains the harness and fixture corrections. No VM was launched.

`iso-verification.json` records fresh kernel extraction checks and ISO/kernel SHA-256 values. Detailed physical timing and physical runtime acceptance remain pending.
