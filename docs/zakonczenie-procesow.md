# Cykl życia i zakończenie procesu (BWS 32 / BWS 97)

## Przyczyna wady BWS 97 (ustalona przed zmianą kodu)
Sprzątanie zasobów procesu jest sterowane **danymi**, nie stanem: `posprzataj_zakonczone_procesy()` (scheduler, wołane co tick
w `PrzelaczKontekst`) zwalnia zasoby wyłącznie dla slotów, w których `zasoby_do_zwolnienia[pid].oczekuje == true`. Jedyne miejsce, które
to ustawia, to `zakoncz_aktualny_proces()` (samozakończenie: BWS 32, błąd Ring 3), bo tylko proces bieżący zna swój `przestrzen_adresowa`
i `baza_stosu_jadra` i nie może ich sam zwolnić. `proces_zabij()` (BWS 97) robił tylko: obniżenie zaufania, `uprawnienia = 0`,
`scheduler_usun_zdarzenia_procesu`, stan `KONCZACY` — **bez** zapisania zasobów do zwolnienia. Skutek dla zabitego procesu:
* slot zostaje w `KONCZACY` na zawsze (nigdy `PUSTY`): wyciek slotu PID (tablica ma tylko 16 slotów);
* nigdy nie wołane: `bws_gui_usun_stan_procesu` (fokus, kursor, cień), `usun_warstwe` (warstwa kompozytora, wpis paska zadań zostaje),
  `bws_nowoczesne_posprzataj_proces` (IPC, SHM, obserwatorzy plików, sesja TCP, motyw, ekran, instalator), `okna_podrzedne_przed_sprzataniem`;
* wyciek PML4 + stron użytkownika + stosu jądra (`stos_jadra_zwolnij`), pamięci kernela (bufory grafiki).

## Dlaczego zabicie nie-bieżącego procesu jest bezpieczne w tym jądrze (jednordzeniowym)
Timer przełącza kontekst tylko gdy przerwano Ring 3, proces jest pusty/kończący albo zablokowany w BWS 38 / HLT sieci
(`idt.cpp`). Kod Ring 0 nie jest wywłaszczany w środku sekcji krytycznych, więc proces nie-bieżący jest zawsze w Ring 3 albo w jawnym
punkcie oczekiwania (bez blokad jądra). Można go więc zakończyć bez czekania na jego współpracę.

## Poprawka
`scheduler_zazadaj_zakonczenia(pid)` (nowe API) wykonuje dla procesu NIE-bieżącego to samo, co `zakoncz_aktualny_proces()`: zapisuje zasoby
do `zasoby_do_zwolnienia`, odbiera prawa, usuwa zdarzenia i ustawia `KONCZACY` — niszczenie robi później, **w kontekście timera**,
istniejące `posprzataj_zakonczone_procesy` (pomija proces bieżący i przestrzeń aktualnie w CR3, więc nigdy nie zwalnia aktywnego CR3 ani
stosu w użyciu). Operacja jest idempotentna (powtórne żądanie = „w trakcie”), nie rusza slotów rezerwowanych przez loadera
(`ZABLOKOWANY`), odmawia samobójstwa i PID 0. Ten sam kod (`zakonczenie_procesu.h`) jest testowany na hoście.

## Eskalacja z kontekstu timera i opóźnienie sprzątania (ustalenia z testów natywnych)
* `scheduler_zazadaj_zakonczenia_z_timera(pid)` — wariant dla `okna_podrzedne_tick()`: cel może być procesem **przerwanym tickiem**
  (typowo zawieszona pętla Ring 3, więc zwykle to właśnie on jest „bieżący”). Przejście w `KONCZACY` następuje przed wyborem następnego
  procesu, więc nigdy więcej nie dostanie CPU; zasoby zwalnia dopiero następny tick, już na stosie innego procesu. Wariant syscallowy
  (BWS 97) nadal odmawia dla procesu bieżącego (= wywołującego). Gdy żądanie z timera zostanie odmówione przy żywym procesie,
  termin jest ponawiany po 50 ms (wcześniejsza wersja zużywała termin jednorazowo i zawieszone dziecko mogło nie zostać zakończone).
* Sprzątanie biegnie tylko przy tickach, które trafiają w Ring 3 albo w punkt oczekiwania. Proces w ciasnej pętli **syscalli** (np. samo
  `BWS_CZAS_MONOTONICZNY_NS`) odsuwa sprzątanie cudzych procesów o sekundy (tick trafia w Ring 0). To opóźnienie, nie wyciek: po
  zmianie launchera testowego na blokujące oczekiwanie sprzątanie następuje w ciągu kilku ticków.
* Diagnostyka: `make iso BURSZTYN_DIAG_PROC_LIFECYCLE=1` loguje `[PROC-CLEANUP] pid=N wolne_ramki=M` przy zwolnieniu slotu.

## Testy
* Host (ASan/UBSan): `make test-zakonczenie-procesu-host` (4063 sprawdzeń: dokładnie-raz, idempotencja, PID reuse, odmowy, oczekiwanie na
  zdarzenie, aktywny CR3, autoryzacja), `test-okna-podrzedne-host`.
* Natywnie (QEMU, jednorazowy gość z podmienionymi blobami): `python3 testy/test_zakonczenie_qemu.py` — 15 scenariuszy (kill: pętla,
  oczekiwanie na zdarzenie, sterta 4 MiB, renderowanie; powtórne zabicie; PID niepoprawne/chronione; 40 cykli z reuse PID; zabicie
  dziecka przy żywym właścicielu i ponowne otwarcie; zabicie właściciela przy zawieszonym dziecku; 3 dzieci; brak prawa; 12 cykli).
  Pomiar: wolne ramki po 76 zwolnieniach wróciły do wartości początkowej (469892), bez dryfu.
