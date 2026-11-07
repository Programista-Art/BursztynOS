"""Integration contracts; pixel/timing execution lives in the C++ suites."""
from test_startup_policy import body, check, root, kernel, wm, gfx, boot, wm_start
import test_startup_policy as base
startup_checks=base.checks
welcome=(root/'jadro/gui/ekran_powitalny.cpp').read_text()
comp=(root/'jadro/gui/skladacz_obrazu.cpp').read_text()
start=boot.index('powitanie_inicjalizuj(')
check(boot.index('InicjalizujGrafike(')<boot.index('hpet_inicjalizuj()')<start<boot.index('InicjalizujPlaniste('), 'start uses graphics/font and monotonic clock before WM')
check('bursztyn.splash=0' in boot and '!zadano_automatyczne_testy(g_multiboot_info_ptr)' in boot, 'diagnostic and automated boot bypass')
check('bursztyn.install=1' in boot, 'installer preserves its behavior')
check('opcja_startowa_obecna(cmdline, token)' in body(kernel,'bool zadano_automatyczne_testy('), 'shared bounded Multiboot parser')
check('skladacz_obrazu_czy_widoczna(pid)' in body(kernel,'bool system_potwierdz_gotowosc_pulpitu('), 'handshake rejects unpublished desktop')
check('powitanie_pulpit_gotowy(grafika_czy_tapeta(),pid)' in kernel,'wallpaper or intentional fallback')
check(wm_start.index('odswiez_wpisy_pulpitu()')<wm_start.index('RysujPulpit(true)')<wm_start.index('gui_potwierdz_gotowosc_pulpitu()'),'catalog and restored positions precede first full paint and handshake')
# Pointer unlock must not wait on hash_pulpitu_wazny; retry until handshake OK.
check('start_pulpitu_gotowy=gui_potwierdz_gotowosc_pulpitu()' in wm_start and
      'if(!start_pulpitu_gotowy)' in wm_start and 'hash_pulpitu_wazny' not in
      wm_start[wm_start.index('gui_potwierdz_gotowosc_pulpitu()'):wm_start.index('if(!start_pulpitu_gotowy)')+len('if(!start_pulpitu_gotowy)')],
      'handshake after first paint; timer retries without catalog hash gate')
check('powitanie_zrodlo(' in body(gfx[gfx.index('// PREZENTACJA EKRANU'):],'void PrzeniesNaEkran('),'full presentation covered')
check('powitanie_zrodlo(' in body(gfx[gfx.index('// PREZENTACJA EKRANU'):],'void PrzeniesFragmentNaEkran('),'partial presentation covered')
check('powitanie_tick();' in body(comp,'void skladacz_obrazu_obsluz_dirty('),'idle desktop still advances state without busy-wait')
check('/shell.bur' not in welcome and 'utworz_warstwe' not in welcome,'no Shell process or normal window')
check('scheduler_' not in welcome and 'asm volatile' not in welcome and 'sleep' not in welcome,'no scheduler/IRQ blocking or delay')
check('BURSZTYN_WERSJA' not in welcome,'optional version omitted; no duplicate version constant')
check('Powitanie::Stan::Zakonczony' in welcome,'resource failure terminates rather than stalls')
print(f'Welcome integration policy: {base.checks-startup_checks}/{base.checks-startup_checks} PASS; existing startup contracts: {startup_checks}/{startup_checks}')

power=(root/'jadro/bws/syscalls.cpp').read_text()
order=(root/'jadro/gui/cykl_wylaczania_grafiki.h').read_text()
check(order.index('zablokuj_ekran();',order.index('inline void'))<order.index('powitanie_wycisz();')<order.index('skladacz_obrazu_wycisz();')<order.index('grafika_wycisz();')<order.index('odblokuj_ekran();',order.index('inline void')),'screen lock serializes release, detached work and graphics')
for signature in ('uint64_t reset_ps2()', 'void wylacz_qemu()'):
    p=body(power,signature)
    check(p.index('wycisz_start_przed_restartem();')<p.index('virtio_gpu_stop_submissions();')<p.index('virtio_gpu_quiesce_for_reboot();')<p.index('psf_flush_cache();')<(p.index('diag_start_reboot("[STARTUP] reboot handoff")') if signature.startswith('uint64_t') else p.index('asm volatile("cli"')),'power stops splash before GPU, filesystem and handoff')
font=(root/'jadro/gui/ekran_powitalny.h').read_text()
check('CzcionkaUI::rysuj_tekst_wygladzony' in font and 'bitmapa_znaku' not in font,'shared Unicode grayscale renderer, no private bitmap rasterizer')
check('WYSOKOSC_TYTULU=56, WYSOKOSC_KOMUNIKATU=36' in font,'larger logical typography')
check('grafika_reset_startu();' in body(gfx,'void InicjalizujGrafike(') and 'powitanie_wycisz();' in body(gfx,'static void grafika_reset_startu('),'graphics reinitialization destroys previous welcome')
check('powitanie_reset_ekranu();' in body(comp,'void skladacz_reset_ekranu('),'compositor reset invalidates readiness and old surface')
print('Reboot / typography integration: 7/7 PASS')
