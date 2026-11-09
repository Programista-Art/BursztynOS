#!/usr/bin/env python3
"""Guard the hardware handoff order and display publication barrier. No VM."""
import re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
s = (root / 'jadro/rdzen/kernel.cpp').read_text()
s = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', s)
start = s.index('extern "C" void kernel_main')
s = s[start:]
checks = 0

def check(ok):
    global checks
    assert ok
    checks += 1

closed = s.index('input_set_pointer_ready(false)')
usb = s.index('usb_inicjalizuj()')
ps2 = s.index('InicjalizujMyszPS2()')
graphics = s.index('ekran_inicjalizuj()')
finished = s.index('PrezentacjaStartu::zakoncz_inicjalizacje()')
opened = s.index('input_set_pointer_ready(true)')
activated = s.index('virtio_input_activate_prepared()')
dispatch = s.index('input_service_all()')
check(closed < usb < ps2)  # controller/firmware setup precedes streaming negotiation
check(ps2 < graphics < finished < opened)  # hardware readiness cannot publish early
check(opened < activated < dispatch)  # common display barrier preserves VirtIO activation
check(s.count('input_set_pointer_ready(true)') == 1)
check(s.count('input_set_pointer_ready(false)') == 1)
# The barrier is unconditional, shared by production, diagnostics and Installer.
prefix = s[:opened]
check(prefix.count('{') == prefix.count('}') + 1)
print(f'Pointer boot order/barrier: {checks} checks PASS')
