"""Source contracts for boot policy/order; runtime pixels are covered by C++ tests."""
from pathlib import Path
import re
root = Path(__file__).resolve().parent.parent
checks = 0

def check(condition, message):
    global checks
    checks += 1
    assert condition, message

def body(text, signature):
    start = text.index('{', text.index(signature))
    # Preserve offsets while masking comments/strings for brace matching.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: ' ' * len(m.group()), text, flags=re.S)
    depth = 1
    pos = start + 1
    while depth:
        depth += (masked[pos] == '{') - (masked[pos] == '}')
        pos += 1
    return text[start + 1:pos - 1]

kernel = (root / 'jadro/rdzen/kernel.cpp').read_text()
wm = (root / 'programy/menedzer_okien/menedzer_okien.cpp').read_text()
shell = (root / 'programy/shell/shell.cpp').read_text()
gfx = (root / 'sterowniki/grafika/grafika.cpp').read_text()
boot = body(kernel, 'void kernel_main(')
wm_start = body(wm, 'void _start() {')
check('/shell.bur' not in boot, 'boot must not spawn a Shell process')
check('uruchom_powloke(' not in wm_start and 'SCIEZKA_POWLOKA' not in wm_start,
      'desktop startup must not create Shell')
check('"/menedzer_okien.bur"' in boot, 'WM remains the normal initial GUI process')
check(boot.index('InicjalizujPlaniste(') < boot.index('"/menedzer_okien.bur"'), 'scheduler setup precedes WM creation')
check(boot.index('motyw_inicjalizuj();') < boot.index('"/menedzer_okien.bur"') < boot.index('wielozadaniowosc_aktywna = true;'), 'theme resolves before Ring 3 execution')
check(boot.index('motyw_inicjalizuj();') < boot.index('ekran_inicjalizuj();'), 'theme resolves before display restore can compose')
check('tapety_czy_jpeg_do_przywrocenia()' in boot and '"--restore"' in boot,
      'asynchronous Ring 3 wallpaper restoration remains')
check('if (zadano_automatyczne_testy(g_multiboot_info_ptr))' in boot, 'test startup stays explicit')
check('gui_legacy_wlacz(' not in shell and 'gui_rysuj_okno(' not in shell,
      'modern Shell never enters legacy GUI')
check('gui_legacy_wlacz(' not in wm_start and 'gui_rysuj_okno(' not in wm_start,
      'WM startup never enters legacy GUI')
check('uruchom_lub_aktywuj(SCIEZKA_POWLOKA' in body(wm, 'void uruchom_powloke('), 'manual launch remains')
check(wm_start.index('RysujPulpit(true);') < wm_start.index('gui_potwierdz_gotowosc_pulpitu()'), 'desktop paint before readiness handshake')
check(not re.search(r'okna\[0\]\.widoczne\s*=\s*true', gfx), 'input ownership never revives kernel terminal')
check('OdswiezEkran(' not in body(gfx, 'void boot_log_gui('), 'bootstrap diagnostics remain headless')
check('PrzeniesNaEkran(' not in body(gfx, 'void boot_log_gui('), 'bootstrap cannot publish a legacy frame')
check('grafika_odtworz_tlo_skladania();' in body(gfx, 'void InicjalizujGrafike('), 'initial background initialized off-screen')
check('PrzeniesNaEkran();' not in body(gfx, 'void InicjalizujGrafike('), 'graphics setup must retain firmware frame until startup policy')
check('static_cast<uint8_t*>(drv->fb_vaddr)[i]' not in body(gfx, 'bool grafika_aktywuj_virtio_gpu('), 'new hardware surface is not imported as desktop pixels')
ui = (root / 'jadro/gui/bursztyn_gui.cpp').read_text()
check('motyw_odswiez_lokalny();' in body(ui, 'const BursztynMotyw* gui_pobierz_motyw('), 'first Ring 3 theme lookup queries the kernel')
check('BWS_MOTYW_POBIERZ' in body(ui, 'void motyw_odswiez_lokalny('), 'palette comes from the resolved system theme')
print(f'Startup policy/order: {checks}/{checks} PASS (static source contracts, not guest execution)')
