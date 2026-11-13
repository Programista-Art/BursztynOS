# Husarz a responsywność całego systemu — pomiary i poprawki

Gałąź `feature/bsf-2-2-font-formats`. Zmiany niezatwierdzone.

**Ważne zastrzeżenie.** Wszystkie pomiary pochodzą z QEMU/TCG bez KVM, bez dostępu do internetu
(brak realnych stron HTTPS) i z jednym vCPU. Nie udało się odtworzyć dotkliwości opisanej przez
właściciela („cały system prawie nieużywalny”). Poniżej: co zmierzono, co jest przyczyną
udowodnioną, co poprawiono i co pozostaje niezbadane. Brak twierdzenia o gwarantowanym czasie reakcji.

## Architektura (audyt)

* Scheduler jest round-robin z przerwaniem zegara (250 Hz), ale **nie przełącza procesu w środku
  SYSCALL** (wyjątki: BWS 38 i jawne czekanie sieciowe `HLT`). Cały kod jądra wykonywany z
  syscalla jest więc niepreemptowalny, dopóki sam nie odda CPU.
* HTTP/HTTPS (TCP, TLS 1.2 mbedtls, kryptografia) działa **w jądrze**, w syscallu procesu
  `husarz-worker`; sieć jest serializowana dzierżawą (`NetworkLease`).
* Husarz: potok HTML/CSS/styl/layout/paint to korutyny (`work.h`) z budżetem 2048 jednostek i
  2 ms czasu ściennego (zegar RDTSC skalibrowany `WorkClock`), anulowanie przy nawigacji; JS ma
  własny budżet.
* Kompozytor (`skladacz_obrazu`) i sterownik VirtIO-GPU działają w jądrze; zakończenie komendy GPU
  jest oczekiwane aktywną pętlą `pause` (do dziesiątek ms pod TCG).

## Instrumentacja (wyłączona domyślnie; build z `BURSZTYNDIAGGUIRUNTIME=1` itd.)

| co | gdzie | jak włączyć |
|---|---|---|
| opóźnienie wejście→prezentacja, odstęp klatek, GPU, zamki | `[GUI PERF]` co 1 s (istniało) | `BURSZTYNDIAGGUIRUNTIME=1` |
| **tiki, których nie dało się przełączyć w syscallu, gdy inny proces był gotowy** (`deferred_ticks`) i najdłuższy ciągły odcinek (`deferred_run_max_ns`) | `scheduler_zarejestruj_odroczony_tick`, `idt.cpp` | j.w. |
| **który syscall to powodował** (`deferred_by_syscall bwsN=tiki`) | `gui_runtime_diag.h` | j.w. |
| złożenia „rozdartej” klatki / z cienia (`torn_compose`, `shadow_compose`) | `skladacz_obrazu.cpp` | j.w. |
| oddania CPU w kryptografii (`yield_hlt`, `yield_skip`) | `siec.cpp` | j.w. |
| czas fazy potoku i przystanki UI Husarza (`[HUSARZ UI STALL]`, `[HUSARZ PERF WARNING]`) | `perf.h`, `work.h` (istniały) | `HUSARZ_PERF=1` |
| koszt kryptografii w gościu (`[TLS COST]`) | `tls.cpp` | `BURSZTYN_DIAG_TLS_COST=1` |
| start aplikacji (`[LOADER PERF]`, `[APP LAUNCH]`, `[APP START]`) | `loader.cpp`, `bsf/runtime.cpp` | `BURSZTYN_DEBUG_LOADER_PERF=1` |
| przełączniki A/B | — | `BURSZTYN_DIAG_NO_YIELD=1`, `BURSZTYN_DIAG_NO_SHADOW=1` |

Zegar monotoniczny: HPET (`czas_monotoniczny_ns`). W buildzie produkcyjnym nic z powyższego nie
jest kompilowane (puste inline).

Narzędzia hosta: `narzedzia/desktop_responsiveness_qemu.py` (scenariusz: boot, Husarz, ładowanie
strony z lokalnego serwera przez guestfwd, ruch wskaźnika przez QMP, metryki gościa),
`narzedzia/guest_session.py` (+ `guest_ctl.py`), fixtury stron `testy/husarz_load/generate.py`
(10 kategorii: mała, CSS, JS, w stylu WordPressa, obrazy, długa, RTL arabski/hebrajski, tabela,
2500 węzłów).

## Pomiary i wnioski

### 1. Strona w stylu WordPressa (91 KB, HTTP), bez obciążenia kryptograficznego

Pojedynczy przebieg, TCG. Opóźnienie wskaźnika mierzone od wysłania ruchu do zmiany zmiennej
kursora w gościu (sonda HMP, wpływa na QEMU — patrz zastrzeżenie poniżej):

| faza | mediana | p90 | max |
|---|---|---|---|
| pulpit bezczynny | 8,7 ms | 25,5 ms | 41 ms |
| Husarz otwarty, bezczynny | 11,0 ms | 34,1 ms | 63 ms |
| **ładowanie strony** | **14,1 ms** | 35,7 ms | 554 ms |
| po załadowaniu | 12,6 ms | 19,1 ms | 64 ms |

Czas CPU (z `[GUI PERF]`, 79 s): jądro/kompozytor 17,7 s, Diagnostyka systemu 9,5 s (!), Menedżer
Okien 3,7 s, Husarz 3,4 s. **System nie był nasycony** — ładowanie lokalnej strony HTTP pod TCG
pogarsza reakcję wskaźnika o kilka ms, a nie o rząd wielkości. Skoki do ~0,5 s pokrywają się z
`gpu_max_ns ≈ 500 ms` (timeouty VirtIO-GPU, znane z pamięci projektu; sonda HMP obciąża pętlę
główną QEMU, więc to częściowo artefakt pomiaru — dlatego późniejsze pomiary używają wyłącznie
QMP i telemetrii gościa).

Zauważone przy okazji: okno **Diagnostyka systemu**, uruchamiane automatycznie, zużywa ~12% CPU
gościa w tle (to samo przy bezczynnym pulpicie).

### 2. Najpoważniejsza znaleziona przyczyna: kryptografia TLS w jądrze jest niepreemptowalna

Pomiar w gościu (`[TLS COST]`, TCG): wygenerowanie klucza efemerycznego P-256 **26–36 ms**, mnożenie
punktu (shared secret) **13–48 ms**. Jeden handshake TLS 1.2 ECDHE to kilka takich mnożeń oraz
weryfikacja certyfikatu ECDSA/RSA, czyli rzędu **100–250 ms bez oddania CPU**, a przeglądarka otwiera
osobne połączenie dla każdego zasobu. W tym czasie Menedżer Okien (kursor, przeciąganie okien,
pasek zadań) nie dostaje CPU. To pasuje do objawu opisanego przez właściciela (zacięcia „w trakcie
ładowania”, powrót do normy po załadowaniu strony lub zamknięciu Husarza).

**Poprawka:** `bursztyn_crypto_yield_point()` (`siec.cpp`), wywoływane z początku
`mbedtls_mpi_mul_mpi` (poprawka vendored `bignum.c`, aktywna tylko z `BURSZTYN_CRYPTO_YIELD`):
oddaje CPU co tik zegara (nie częściej), **tylko gdy właściciel dzierżawy sieci i tylko gdy inny proces
jest gotowy**, przez istniejące `scheduler_odstap_w_czekaniu_sieci()` (HLT + przełączenie po
przerwaniu; to samo, co robi czekanie sieciowe). Punkt jest przed alokacjami mbedtls, więc nie
przerywa operacji alokatora.

**Weryfikacja A/B (ten sam scenariusz, bez ruchu wskaźnika, ładowanie strony `images.html`, 9 żądań,
każde z syntetycznym kosztem 8 handshake’ów w jądrze — `tls_diag_symuluj_handshake`):**

| | tiki odroczone (40 s) | najdłuższy odcinek bez przełączenia |
|---|---|---|
| bez oddawania CPU (`BURSZTYN_DIAG_NO_YIELD=1`) | 956 | **271 ms** |
| z oddawaniem CPU | 476 | **73 ms** (3198 oddań HLT, 0 pominięć) |

Uwaga: pierwsza para pomiarów A/B z ruchem wskaźnika (QMP co 50 ms) nie wykazała różnicy
(1613 vs 1486 tików), bo ruch wskaźnika sam generuje ~40-ms odcinki w ścieżce GPU/kursora, które
dominują; wpływ kryptografii widać dopiero w scenariuszu bez ruchu wskaźnika. Koszt: czas
handshake’u rośnie o czas, który spędzają inne procesy (to zamierzone).

### 3. Granularność budżetu czasowego potoku Husarza

Przystanki UI (`[HUSARZ UI STALL]`) pokazały plastry po 55–105 ms (do 510 ms) mimo budżetu 2 ms:
zegar ścienny sprawdzany był co `ClockPollWork = 128` jednostek, a pod TCG jednostka kosztuje
~0,1–0,5 ms. Zmieniono (`work.h`): zadania na natywnym zegarze RDTSC (wszystkie zadania potoku)
sprawdzają zegar co **16** jednostek (`NativeClockPollWork`); zadania na zegarze syscallowym
(tylko build perf) zostają przy 128. RDTSC kosztuje kilka cykli, więc koszt jest pomijalny.
Test hosta zaktualizowany (kontrakt zmienił się celowo; `test-husarz-performance-host` 4638
sprawdzeń PASS). **Efekt na czas przystanków nie został zmierzony oddzielnie natywnie** (strony i
buildy w pomiarach różnią się; wartości surowe: 16 przystanków ≥ 50 ms w 79 s przed zmianą na
stronie WordPress; 3–12 w 45 s na innej stronie po zmianie — nieporównywalne).

### 4. Co nie zostało zmienione (i dlaczego)

* **Aktywne oczekiwanie na GPU w jądrze** (`virtio_gpu.cpp`): ~10–30 ms na prezentację pod TCG,
  500 ms w skokach. Oddawanie CPU w tej pętli przy trzymanym zamku sterownika/ekranu grozi
  zakleszczeniem (inny proces w syscallu nie jest wywłaszczany i wiruje na zamku), więc bez
  przebudowy blokad nie jest bezpieczne. To najprawdopodobniej główne źródło tła ~40 ms w
  `deferred_run_max_ns` także na bezczynnym pulpicie.
* Brak testów na realnych stronach HTTPS (brak internetu) i na sprzęcie/KVM.
* Brak testów obrazów rzeczywistych, JS „ciężkich”, WOFF2 itd. w tym scenariuszu — fixtury
  istnieją (`testy/husarz_load/`), ale zmierzono tylko dwie strony (WordPress, obrazy).
* Czas do pierwszej treści / do pełnego załadowania, szczytowe zużycie pamięci — nie zmierzone
  w tej sesji.
* Anulowanie przy zmianie strony/zamknięciu karty oraz budżet JS: istniały i nie były zmieniane.
