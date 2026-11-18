# Dowody: planista / BWS 97 — serie natywne i hostowe (2026-11-15/16)

Wyselekcjonowana kopia katalogu `.zakonczenie-test/serie/` (znaczniki czasu zachowane `cp -a`), wykonana przed wyjęciem
`.zakonczenie-test/` spod kontroli Git. Zawiera wyłącznie tekst: logi szeregowe gościa, podsumowania harnessu (`*.out`), próbki
rejestrów vCPU (`*.regs`), logi pulsu hosta, logi testów hostowych i skrypt serii — bez binarek, ISO, blobów i kopii źródeł
(te są odtwarzane przez `testy/test_zakonczenie_qemu.py`).

* [MANIFEST.sha256](MANIFEST.sha256) — 78 plików; każda suma jest identyczna z oryginałem (`cd serie && sha256sum -c ../MANIFEST.sha256`).
* [DUPLIKATY.sha256](DUPLIKATY.sha256) — 12 plików `FAILED_run_NN.log` z oryginału pominiętych jako bajtowe duplikaty `run_NN.log`
  (harness kopiuje log przy każdej porażce). Status porażki widać w `*.out` danej serii.
* Puste pliki w `m_C/` (`FAILED_run_01.log`, `run_01.regs`, `run_02.log`) są celowo zachowane: to stan po zawieszeniu całego systemu
  w trakcie serii (`FAILED_run_01.log` nie zdążył zostać zapisany, choć `run_01.log` ma 12 KiB).

Opis mechanizmów i wniosków: [../zakonczenie-procesow.md](../zakonczenie-procesow.md).

| Katalog / plik | Zawartość |
|---|---|
| `serie/seria_poprawka_v1/` | pierwsza seria z poprawką (2 porażki z 4) |
| `serie/regs/`, `regs.out` | seria z próbkowaniem rejestrów vCPU (5 timeoutów; pauzy hosta do 400 s) |
| `serie/v2_oddanie/`, `v2_oddanie.out` | wersja z programowym oddaniem CPU: FAIL s12 w `run_01.log` (62 s HPET przy 26 IRQ = pauza hosta) |
| `serie/flood_base/`, `flood_fix/` (+ `.out`) | A/B s13 (zalew syscalli): baseline vs poprawka |
| `serie/m_A/`, `m_B/`, `m_C/`, `macierz.sh`, `macierz.out` | macierz przyczynowości; `m_C` urwana zawieszeniem całego systemu (puste pliki to oryginalny stan) |
| `serie/b1_fix/`, `b1_fix.out` | ostatni pełny przebieg (s1–s11 PASS, przerwany w s12 przez pauzy VirtualBox 58–108 s) |
| `serie/heartbeat_*.log` | niezależny puls hosta (`testy/heartbeat_hosta.py`): pauzy całej maszyny, także bez QEMU |
| `serie/host_2026-11-16/`, `host_2026-11-16b/` | logi testów hostowych (ASan/UBSan) z przeglądów 2026-11-16 |

Logi starego formatu (bez pola `irq`) pochodzą sprzed rozszerzenia diagnostyki `[PROC-REQ]`/`[PROC-CLEANUP]`.
