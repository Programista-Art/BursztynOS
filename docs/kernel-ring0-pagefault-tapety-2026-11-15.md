# Przerywany #PF Ring 0 w `tapety_obsluz_obraz` (BWS 69) — analiza i poprawka

## Objaw
1 na ~15 uruchomień (UEFI, QEMU/TCG): `#PF`, kod błędu `0x3` (strona obecna, zapis), RIP `0x117BB1`
(`tapety_obsluz_obraz+0x361`), CR2 `0x120236000`, proces `/programy/tapety.cebula/tapety.bur` (pid 2), `lastbws=69`,
przed jakąkolwiek zmianą rozmiaru okna. Log: `docs/zrzuty-resize-2026-11-15/crash-tapety-boot-serial.txt`.

## Odtworzenie ścieżki awarii
* Rozkodowanie RIP (`objdump`): instrukcja `mov %dl,(%rcx,%rax,1)` w pętli `scratch[i] = stage[i]` — jądro kopiuje
  obraz do bufora `scratch` Tapet. `scratch` = `grafika_scratch_tapety()` = **stałe VA `0x120000000`** (`VADDR_RAW_BMP`,
  32 MiB, mapowane jako strony **jądra** 4 KiB). Stos: `[0]=0x120000000` (scratch), `[6]=0x384000` (1280×720×4).
  Zapis na offsecie `0x236000` trafił w stronę **obecną, ale nie będącą stroną scratch**: kod błędu 3 z U/S=0 to
  naruszenie SMAP/zapisu na stronie użytkownika, czyli stronie obrazu procesu.
* Przyczyna: ASLR BUR v2 losuje podstawę obrazu PIE w oknie `[0x100000000, 0x200000000)` (20 bitów). Okna VA jadra w
  tej samej dolnej połowie (`0x120000000`+32 MiB scratch, `0x130000000` LFB, `0x200000000` RAM-dysk PSF, `0x500000000`
  sterta jadra) są **klonowane do każdej przestrzeni procesu**, a `plan ASLR` ich nie omijał. Gdy obraz (Tapety ≈ 0,47 MiB)
  trafił na `0x120000000..0x122000000`, loader mapował strony użytkownika na adresach zajętych przez PTE jadra, a
  `vmm_mapuj_nowa_strone_procesu_wewnetrznie` odrzucał tylko obecne strony **użytkownika** (`obecna && user`), więc
  PTE jadra były po cichu zastępowane. Potem jądro (w kontekście tego procesu) pisało przez „swoje” VA w strony obrazu:
  SMAP #PF (tu) albo — gdyby strona była zapisywalna — cicha korupcja danych procesu / wyświetlenie jego pamięci
  zamiast tapety/LFB.
* Prawdopodobieństwo: ~0,8% na uruchomienie Tapet dla samego scratcha (+LFB, PSF) i podobnie dla każdego innego procesu
  PIE (WM, Diagnostyka...). Zgodne z obserwacją (1/15) i z wynikiem po poprawce (poniżej: 4 naturalne odrzucenia
  w 20 uruchomieniach, w tym 2× Tapety).
* To **nie** był nieprawidłowy wskaźnik użytkownika (walidacja `czy_bezpieczny_zakres_uzytkownika_do_odczytu`
  przeszła), nie use-after-free ani wyścig w BWS 69 — błąd w układzie przestrzeni adresowej.

## Dowód (odtworzenie deterministyczne)
Flaga diagnostyczna `BURSZTYN_DIAG_FORCE_TAPETY_PIE_BASE=0x1201F0000` (Makefile → `loader.cpp`, domyślnie wyłączona;
pierwsza próba losowania Tapet dostaje wymuszoną bazę): **niepoprawiony** kod → #PF za każdym razem, to samo RIP
`0x117BB1`, kod 3, `lastbws=69`, CR2 `0x1201F1000` (pierwsza strona obrazu). Poprawiony kod → 0 awarii w 3 z 3 uruchomieniach,
w logu `[LOADER] plan ASLR odrzucony (kolizja z oknem jadra), losowanie ponowne: /programy/tapety.cebula/tapety.bur`.

## Poprawka
1. `jadro/mm/zarezerwowane_va.h` — jedno źródło prawdy o oknach VA jadra (+ `static_assert` w `kernel.cpp`, stałe
   `grafika.cpp` z niego pochodzą); `koliduje_z_jadrem()` (fail-closed dla pustych przedziałów).
2. `bur_v2_zaplanuj_aslr` odrzuca plan, gdy obraz, sterta lub SHM przecinają okno jadra; loader (`loader.cpp`) losuje
   ponownie (≤ 64 prób, potem odmowa uruchomienia — fail-closed). Koszt: ~2,3% slotów PIE odrzucanych (obraz 22 stron),
   entropia nadal ≈ 19,97 bitów.
3. Obrona w głąb: `vmm_mapuj_nowa_strone_procesu_wewnetrznie` odmawia mapowania, gdy na adresie ≥ 4 GiB jest już
   dowolna obecna strona (wcześniej tylko strona użytkownika). Identity-mapa < 4 GiB nadal jest współdzielona i
   zastępowana jak dotąd (RING3_BASE = 16 MiB leży w niej).
Nie zmieniano BWS 69 ani ochrony pamięci; nie wyłączono obsługi wyjątków.

## Testy
* `make test-bur-v2-aslr-host` (ASan/UBSan): wymuszona baza z awarii i brzegi okien odrzucane, brzegi dozwolone, wyczerpująco
  wszystkie 2^20 slotów PIE (odrzucone = dokładnie te przecinające okna; 24 639 slotów), sterta/SHM nigdy nie kolidują,
  10 000 losowań (odrzucone ⇔ kolizja).
* Natywnie (build produkcyjny, bez flagi): UEFI tablet 10/10, UEFI mysz USB 5/5, BIOS tablet 3/3 uruchomień bez
  paniki, tapeta zainstalowana. Poprzednia seria (build z poprawką ASLR bez poprawki BWS 19): 20/20.
