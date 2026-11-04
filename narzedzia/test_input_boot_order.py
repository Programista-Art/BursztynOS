#!/usr/bin/env python3
"""Host regression for the production startup barrier and activation call sites.

Checks the actual source call graph; dynamic lifecycle tests check MMIO behavior.
No emulator or disk image is opened.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def code(path):
    source = (ROOT / path).read_text()
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',
                  lambda m: ' ' * len(m[0]), source)


def body(source, name):
    match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', source)
    assert match, f'missing definition: {name}'
    start = match.end()
    depth = 1
    for i in range(start, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if depth == 0:
            return source[start:i]
    raise AssertionError(f'unclosed definition: {name}')


kernel = body(code('kernel.cpp'), 'kernel_main')
driver = code('virtio_input.cpp')
prepare = body(driver, 'virtio_input_prepare')
activate = body(driver, 'virtio_input_activate')
late = kernel.index('virtio_input_activate_prepared(')
early = kernel.index('virtio_input_probe_and_prepare(')
for required in ('InicjalizujPMM(', 'InicjalizujIDT(', 'inicjalizuj_apic(',
                 'InicjalizujMyszPS2(', 'virtio_pci_inicjalizuj('):
    assert kernel.index(required) < early, required
assert kernel.index('InicjalizujPlaniste(') < late
for required in ('inicjalizuj_psf(', 'aktualizacja_przygotuj_system_plikow(',
                 'instaluj_wbudowane_programy(', 'ekran_inicjalizuj(',
                 'PrezentacjaStartu::zakoncz_inicjalizacje('):
    assert early < kernel.index(required) < late, required
assert late < kernel.index('bws_uruchom_program_z_pliku(')
assert late < kernel.index('wielozadaniowosc_aktywna = true')
assert late < kernel.index('virtio_input_stress_test_all(')
assert kernel.count('virtio_input_activate_prepared(') == 1
assert 'VIRTIO_STATUS_DRIVER_OK' not in prepare
assert 'input_register_device(' not in prepare and 'virtqueue_kick(' not in prepare
assert 'virtio_input_activate(' not in prepare
assert 'virtio_input_prepare(' in body(driver, 'virtio_input_init')
assert 'virtio_input_activate' not in body(driver, 'virtio_input_probe_and_prepare')
assert 's_late_activation_allowed' in body(driver, 'virtio_input_test_teardown_reinit')
assert body(driver, 'virtio_input_probe_and_init').strip() == 'return virtio_input_probe_and_prepare();'
assert body(driver, 'virtio_input_init').strip() == 'return virtio_input_prepare(drv, dev);'
assert body(code('virtio_input.h'), 'virtio_input_zainicjalizuj').strip() == 'return virtio_input_init(drv, dev);'
assert driver.count('virtio_input_activate(drivers[i])') == 1
for shutdown in ('virtio_input_reset', 'virtio_input_reset_all', 'virtio_input_quiesce_for_reboot'):
    shutdown_code = body(driver, shutdown)
    assert 'virtio_input_activate' not in shutdown_code and 'VIRTIO_STATUS_DRIVER_OK' not in shutdown_code
assert activate.count('virtio_pci_set_status(') == 1
assert 'virtio_pci_reset(' not in activate and 'virtqueue_create(' not in activate
assert 'input_dma_alloc(' not in activate and 'virtio_pci_negotiate_features(' not in activate
# Both modes have one unconditional barrier. At this source point, the outer
# kernel function's brace depth is zero (no installer/test conditional wrapper).
prefix = kernel[:late]
assert prefix.count('{') == prefix.count('}'), 'late activation must be mode-independent'
production = [ROOT / 'kernel.cpp', ROOT / 'virtio_input.cpp']
for path in ROOT.rglob('*.cpp'):
    # Private build snapshots are test artifacts, not additional boot callers.
    if any(part.startswith('.') for part in path.relative_to(ROOT).parts):
        continue
    if 'narzedzia' in path.parts or 'biblioteki' in path.parts or path in production:
        continue
    assert not re.search(r'\bvirtio_input_(?:activate|activate_prepared|probe_and_init|probe_and_prepare|prepare|init|zainicjalizuj)\s*\(',
                         code(path.relative_to(ROOT))), f'unexpected activation/init caller: {path}'
print('PASS=1 FAIL=0: production boot order, common late barrier and activation call sites')
