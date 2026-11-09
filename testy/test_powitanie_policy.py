"""Kontrakty integracji; wykonanie pikselowe/czasowe zyje w zestawach C++."""
from test_startup_policy import body, check, root, kernel, wm, gfx, boot, wm_start
import test_startup_policy as base
startup_checks=base.checks
welcome=(root/'jadro/gui/ekran_powitalny.cpp').read_text()
welcome_h=(root/'jadro/gui/ekran_powitalny.h').read_text()
comp=(root/'jadro/gui/skladacz_obrazu.cpp').read_text()
logo=(root/'jadro/gui/logo_maska_dane.h').read_text()
gen=(root/'narzedzia/generuj_maske_logo.py').read_text()
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

# Typografia i rendering: wylacznie wspolny renderer grayscale Unicode.
check('CzcionkaUI::rysuj_tekst_wygladzony' in welcome_h,'shared Unicode grayscale text renderer')
check('static_cast<uint32_t' not in welcome_h and 'new uint8_t' not in welcome_h,'no private rasterizer or heap allocation in the renderer')
check('CzcionkaUI::bitmapa_znaku' in welcome_h,'glyph metrics come from the shared font table, not a copy')

# Nowa animacja: czarne tlo, logotyp, napis, pasek i dwa komunikaty.
check('constexpr uint32_t TLO=0x000000' in welcome_h,'brand background is black')
check('BursztynLogo::MASKA' in welcome_h and 'BursztynLogo::POSWIATA' in welcome_h,'logo is a compiled coverage mask with soft glow')
check('logo_maska_dane.h' in welcome_h,'mask header included by the renderer')
check('png_dekoder' not in welcome_h and 'png_dekoder' not in welcome,'no PNG decoder on the boot path')
check('stbi_load' not in welcome_h and 'tinf_uncompress' not in welcome,'no DEFLATE/inflate on the boot path')
check('Sekwencja' in welcome_h and 'void przelicz(uint64_t t)' in welcome_h,'single absolute-time sample drives all visible content')
check('constexpr uint64_t KROK_US=80000' in welcome_h,'intro is quantized so idle frames do not re-render')
check('postep' in welcome_h and 'PASEK_PELNY_US' in welcome_h,'loading bar progress is time-driven and bounded')
check('constexpr uint64_t MINIMUM_US=6400000, FADE_US=600000' in welcome_h,'minimum sequence time and fade are unchanged')
check('"Polski system operacyjny"' in welcome_h and '"Niezale\\u017cny system"' in welcome_h,'both existing messages are preserved')
check('"BURSZTYN OS"' in welcome_h,'brand wordmark under the logo')
check('wygladz(' in welcome_h and 'liniowo(' in welcome_h,'smoothstep timing without FPU')
check('float' not in welcome_h and 'double' not in welcome_h,'no FPU in the renderer')
check('malloc' not in welcome_h and 'new ' not in welcome_h,'no per-frame allocation in the renderer')
check('wysrodkuj(' in welcome_h and 'x_dla_atramentu(' in welcome_h,'every element is centered on visible ink')
check('granice_atramentu(' in welcome_h,'ink bounds, not advance widths, drive horizontal centering')
check('obszar_w_ekranie(' in welcome_h,'all draws are clipped to the screen')

# Generowana maska jest wersjonowana razem ze zrodlem.
check('PLIK GENEROWANY' in logo and 'namespace BursztynLogo' in logo,'mask header is generated, not hand-edited')
check('SUMA_KONTROLNA_ZRODLA' in logo,'mask header records the source checksum')
check('MASKA_ATRAMENT_OD' in logo and 'POSWIATA_ATRAMENT_OD' in logo,'mask header records ink bounds for layout')
check('def maska(' in gen and 'def rozmycie(' in gen,'generator produces coverage and blurred glow')
check('obrazy/logo.png' in gen,'generator documents its source asset')

# Diagnostyka: etapy, nie klatki.
check('nazwa_fazy(' in welcome_h,'diagnostics log animation phases, not frames')
check('poprzedni_krok' in welcome,'dirty is scheduled only when the sampled state changes')
print('Reboot / typography / new splash integration: PASS')
