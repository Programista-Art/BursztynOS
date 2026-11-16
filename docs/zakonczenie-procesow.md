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
* Przed poprawką z sekcji „Odroczone tiki” sprzątanie biegło tylko przy tickach, które trafiły w Ring 3 albo w punkt oczekiwania:
  proces w ciasnej pętli **syscalli** (np. samo `BWS_CZAS_MONOTONICZNY_NS`) odsuwał sprzątanie cudzych procesów o sekundy, a licznik
  ticków stał (timeouty zdarzeń i myszy się wydłużały, eskalacja okien podrzędnych nie działała). Scenariusz s13 testu natywnego
  odtwarza ten przypadek.
* Diagnostyka: `make iso BURSZTYN_DIAG_PROC_LIFECYCLE=1` loguje `[PROC-REQ]` i `[PROC-CLEANUP]` (pid, czas HPET `t_ns`, wolne ramki,
  liczba aktywnych procesów, CR3, `tick`, `irq`, `przelaczen`, `odroczonych`). Flaga domyślnie 0; nie wolno jej używać w buildzie
  wydaniowym (harness buduje własny `scheduler_diag.o` wyłącznie w `.zakonczenie-test/`).

## Odroczone tiki i punkt zwrotu syscallu (planista jednordzeniowy)
**Księgowanie ticków.** Przerwanie timera (wektor 32) ma dwie rozłączne ścieżki w `WspolnaObslugaPrzerwan` (`idt.cpp`):
* przełączenie (`PrzelaczKontekst`) — gdy przerwano Ring 3, PID 0 bez zajętego ekranu, proces pusty/kończący, BWS 38 albo jawne
  oczekiwanie sieci; `licznik_tykniec_zegara` +1 wewnątrz przełączenia;
* tick **odroczony** (`scheduler_zarejestruj_odroczony_tick`) — tick trafił w zwykły kod Ring 0 (zwykle syscall); licznik +1,
  `g_liczba_ticktow_odroczonych` +1 i ustawiana flaga `g_tick_odroczony`. Przełączenia tu NIE ma (brak wywłaszczania Ring 0).

Każde IRQ timera zwiększa licznik dokładnie raz. Programowe oddanie CPU (`PrzelaczKontekstProgramowy`) używa tej samej implementacji
z `tick_zegara=false` i licznika nie zwiększa. Logi natywne potwierdzają niezmiennik: we wszystkich 10 przebiegach z poprawką (109
zdarzeń) `tick == irq`; w wersji bazowej deficyt sięgał 2427 ticków. Nominalna częstość timera: ~250 Hz.

**Punkt zwrotu syscallu.** `scheduler_punkt_zwrotu_syscalla()` jest wołany na początku `obsluga_wywolan_systemowych` (po `sti` w
`syscall.S`, na prywatnym stosie jądra procesu, bez żadnych blokad jądra, przed jakąkolwiek pracą syscallu, poza oknem dostępu do
pamięci użytkownika — to ta sama granica, na której tick w Ring 3 przełącza kontekst). Decyzja (`jadro/bws/punkt_zwrotu_syscalla.h`,
czysta logika testowana na hoście):
1. był tick odroczony od poprzedniego sprawdzenia (flaga jest zużywana: najwyżej jedno oddanie na tick);
2. bieżący PID 1..15 i aktywna wielozadaniowość (PID 0 nigdy nie oddaje programowo);
3. IF=1 (nigdy z sekcji z wyłączonymi przerwaniami);
4. inny proces czeka na pracę, którą wykonuje tylko `PrzelaczKontekst`: gotowy proces, odroczone sprzątanie, wygasły termin myszy,
   wygasły timeout zdarzenia (termin 0 = bez timeoutu) albo osierocone okno podrzędne z terminem zamknięcia.

Wtedy proces ustawia zezwolenie (`jawne_czekanie_sieci[pid]`) i wykonuje `int $253`. ISR zapisuje kompletną ramę, `PrzelaczKontekstProgramowy`
robi sprzątanie, timeouty i eskalację okien i wybiera następny proces; `IRETQ` wznawia ten syscall, gdy proces znów dostanie CPU.
Jądro jest kompilowane z `-mno-red-zone -mno-sse -mgeneral-regs-only`, więc `int` wewnątrz funkcji C nie niszczy stanu kompilatora.

**Filtrowanie PID 0.** PID 0 jest pełnoprawnym konkurentem w warunku 4 — to nie jest pusty idle: jego pętla (`kernel.cpp`) obsługuje
USB, wejście (`input_service_all`, `virtio_input_obsluz`), kompozytor (`skladacz_obrazu_obsluz_dirty`), timeout ekranu, odroczoną pracę
HDA i zmianę geometrii. Wykluczenie go zagłodziłoby mysz i kompozycję przy ciągłej serii syscalli (dokładnie błąd sprzed poprawki).
Skutek: proces w serii syscalli oddaje CPU raz na odroczony tick, czyli dzieli procesor tak samo jak proces liczący w Ring 3
(round-robin `znajdz_nastepny_proces` obejmuje PID 0; PID 0 z `HLT` wraca najpóźniej przy następnym ticku). Przegląd z 2026-11-16
rozważał wykluczenie PID 0 i je odrzucił; zmieniono tylko mylący komentarz („nie płaci podatku”).

**Wektor 253 (`IDT_WEKTOR_ODDANIA_CPU`).**
* Brama przerwań 64-bit (`0x8E`: obecna, typ 0xE — IF=0 przy wejściu, przywracane przez `IRETQ`), **DPL 0**: `int $253` z Ring 3 daje
  #GP i kończy proces jak każdy wyjątek użytkownika. IST=0 (zwykły stos jądra procesu). `static_assert` w `idt.cpp` pilnuje flag.
* Poza pulą dynamicznych IRQ (pula 48..252 = 205 wektorów; `test-irq-allocation-host`, `test-virtio-pci` sprawdzają, że 253 nigdy
  nie jest przydzielany ani rejestrowany).
* Przełączenie tylko gdy przerwano Ring 0 procesu 1..15 z jawnym zezwoleniem (`idt_oddanie_cpu_przelacz`); w innym przypadku powrót bez zmian.
* Gdyby kontroler kiedykolwiek dostarczył 253 sprzętowo (błędna konfiguracja MSI/IOAPIC), bit ISR w LAPIC byłby ustawiony; obsługa woła
  `wyslij_eoi_jesli_potrzebne(253)`, które wysyła EOI tylko przy ustawionym bicie ISR — programowe `int $253` (bit pusty) nie dostaje
  fałszywego EOI, a sprzętowe nie blokuje LAPIC (bez EOI zablokowane byłyby wszystkie przerwania o priorytecie ≤ 253, w tym timer).

## Znane ograniczenia planisty jednordzeniowego
* Brak wywłaszczania Ring 0: długi syscall bez punktu oddania (`scheduler_oddaj_cpu_w_syscallu`) trzyma CPU do końca; tick jest
  tylko księgowany. Punkt zwrotu działa wyłącznie na **wejściu** syscallu.
* Licznik ticków rośnie teraz także w trakcie syscallu: timeouty liczone w tickach odpowiadają realnemu czasowi (wcześniej wydłużały
  się), a `scheduler_oddaj_cpu_w_syscallu` w długich syscallach może oddawać CPU raz na tick zamiast raz na przełączenie. Wpływ na
  wydajność GUI/sieci/kryptografii nie został zmierzony.
* Przy pauzie całej maszyny (VirtualBox) QEMU/TCG gubi przerwania timera, a HPET biegnie z zegarem hosta: czas w tickach staje,
  czas HPET skacze. Timeouty liczone w HPET (np. `czekaj_az_pojawi` w fixture) wygasają wtedy fałszywie.
* `praca_oczekuje_na_tick()` czyta stany procesów przy IF=1 bez blokady: wynik jest heurystyką (najgorzej jedno zbędne albo jedno
  pominięte oddanie do następnego ticku), nie warunkiem poprawności.
* Wymagania SMP — zob. nagłówek `zakonczenie_procesu.h`; flaga `g_tick_odroczony` i `jawne_czekanie_sieci` są globalne/na proces,
  nie na CPU.

## Testy
* Host (ASan/UBSan): `make test-zakonczenie-procesu-host` (4063 sprawdzeń: dokładnie-raz, idempotencja, PID reuse, odmowy, oczekiwanie na
  zdarzenie, aktywny CR3, autoryzacja), `test-okna-podrzedne-host`, `test-punkt-zwrotu-syscalla-host` (decyzja punktu zwrotu: PID 0
  jako konkurent, wykluczenie bieżącego, sprzątanie, timeouty myszy/zdarzeń z zawinięciem, leniwe okna podrzędne, IF=0),
  `test-irq-allocation-host` (pula 48..252, wektor 253, EOI przy sprzętowym 253).
* Harness bez QEMU: `make test-zakonczenie-harness-host` — syntetyczne przypadki klasyfikacji (awaria jądra i wyjście QEMU nigdy nie
  są pauzą hosta; pauza hosta wymaga dowodu po stronie gościa i hosta; brak postępu przy dostępnym CPU to porażka) oraz klasyfikacja
  dwóch zachowanych logów z [zakonczenie-procesow-2026-11-16/serie/](zakonczenie-procesow-2026-11-16/README.md).
* Natywnie (QEMU, jednorazowy gość z podmienionymi blobami): `python3 testy/test_zakonczenie_qemu.py` — 15 scenariuszy (kill: pętla,
  oczekiwanie na zdarzenie, sterta 4 MiB, renderowanie; powtórne zabicie; PID niepoprawne/chronione; 40 cykli z reuse PID; zabicie
  dziecka przy żywym właścicielu i ponowne otwarcie; zabicie właściciela przy zawieszonym dziecku; 3 dzieci; brak prawa; 12 cykli).
  Pomiar: wolne ramki po 76 zwolnieniach wróciły do wartości początkowej (469892), bez dryfu.

## Klasyfikacja przebiegów natywnych (`testy/test_zakonczenie_qemu.py`)
| Status | Znaczenie | Wiążący |
|---|---|---|
| `PASS` | `KONIEC`, fail=0, pass ≥ 15 (≥ 1 dla `ZK_TYLKO_FLOOD=1`) | tak |
| `FAIL-JADRO` | panika, sprzątanie procesu bieżącego, żądanie bez sprzątania — nigdy pauza hosta | tak |
| `FAIL-QEMU` | proces QEMU zakończył się przed `KONIEC` (crash, triple fault przy `-no-reboot`) | tak |
| `FAIL` | scenariusz FAIL bez dowodu pauzy hosta albo za mało scenariuszy | tak |
| `FAIL-BRAK-POSTEPU` | timeout: ≥ 30 s czasu, w którym host dawał CPU, a log gościa nie rósł | tak |
| `FAIL-TIMEOUT` | timeout przy postępie i wystarczającym czasie gościa; także timeout bez próbek | tak |
| `STALL-HOSTA` | udowodniona pauza hosta — raportowana osobno, nigdy nie liczona jako PASS | nie |

Dowód pauzy hosta dla scenariusza FAIL wymaga obu sygnałów: w oknie awarii HPET przyrósł o ≥ 2 s przy < 10% nominalnych IRQ timera
(vCPU nie wykonywał się) **oraz** pętla harnessu nie dostawała CPU (przerwy > 5 s) łącznie ≥ 50% tego okna. Żywy gość z IF=0 też gubi
tiki, ale wtedy host nie ma przerw — wynik `FAIL`. Granica: gdy pauzy hosta zjadły prawie cały czas (np. `b1_fix`: 376 s z 402 s),
zawieszenia gościa nie da się udowodnić ani wykluczyć — wynik jest niewiążący, nigdy PASS.

Limity czasu: `--timeout` na przebieg i `--limit-sesji` (domyślnie runs×(timeout+30)) w czasie rzeczywistym hosta (CLOCK_MONOTONIC,
wlicza pauzy VM); QEMU jest zawsze kończony (`terminate`, po 5 s `kill`). Dowody: nowy katalog przebiegów zamiast nadpisywania
istniejącego, `run_NN.log`, `run_NN.qemu.log` (stderr QEMU), `run_NN.klasyfikacja.json`, `FAILED_run_NN.log` dla każdego statusu ≠ PASS.

## Pozostała weryfikacja natywna (wymaga zgody właściciela i stabilnego hosta)
Środowisko: Linux Mint w VirtualBox zamraża się na 20–190 s przy aktywnym QEMU/TCG (także bez QEMU), co potwierdza niezależny puls
hosta. Do czasu stabilizacji środowiska testy natywne są wstrzymane. Oryginalne logi serii z 2026-11-15/16 (w tym przerywanych
porażek planisty): [zakonczenie-procesow-2026-11-16/](zakonczenie-procesow-2026-11-16/README.md). Katalog `.zakonczenie-test/` jest
lokalnym wyjściem harnessu (ignorowany przez Git).

Testy oczekujące:
* `python3 testy/test_zakonczenie_qemu.py --runs 1` (pełne s1–s13), sekwencyjnie, 2–3 przebiegi, każdy z `testy/heartbeat_hosta.py`;
* `ZK_TYLKO_FLOOD=1` A/B (`ZK_BASELINE=1`) dla s13 na większej próbie;
* pierwszy natywny przebieg nowej klasyfikacji harnessu (`--limit-sesji`, `run_NN.klasyfikacja.json`);
* natywne potwierdzenie `wyslij_eoi_jesli_potrzebne(253)` na ścieżce programowego oddania CPU (brak fałszywego EOI);
* testy wydajności GUI/sieci/kryptografii po zmianie księgowania ticków;
* natywny rozruch pulpitu po zmianie warstwy `gpu_cpu_*` w `virtio_gpu.cpp` (kod produkcyjny identyczny bajt w bajt — sprawdzone na hoście).
* Ani jeden pełny przebieg s1–s13 z poprawką nie zakończył się bez pauzy hosta (ostatni, 2026-11-16 `b1_fix`: s1–s11 PASS, przerwany
  w s12 przez pauzy VirtualBox). BWS 97 nie jest uznany za w pełni stabilny.
* Seria 2–3 kolejnych pełnych przebiegów z niezależnym pulsem hosta (`testy/heartbeat_hosta.py`), liczona tylko z przebiegów
  bez pauz; A/B s13 z wersją bazową na większej próbie.
* Pomiar wpływu częstszego oddawania CPU na GUI, sieć i kryptografię.
* Zabicie procesu dokładnie w oknie `int $253` (wynika z analizy kodu — proces `KONCZACY` nigdy nie zostaje wznowiony — brak testu).
* Nowa klasyfikacja harnessu została sprawdzona wyłącznie syntetycznie, bez uruchomienia QEMU.
