# Test dźwięku — diagnostyka HDA (2026-10-11)

Dokumentacja wdrożenia aplikacji **Test dźwięku** oraz kernelowej warstwy
diagnostycznej Intel High Definition Audio w Bursztyn OS.

Cel pierwszego uruchomienia fizycznego: **Acer Nitro AN515-54**
(kontroler Intel 8086:A348 w PCI 00 1F.3, klasa 04:03; dodatkowo NVIDIA HDA
10DE:0FB9 w 01:00.1 — HDMI/DP). Implementacja pozostaje generyczna: brak
twardych kodowań BDF, device ID, vendor ID kodeka ani NID-ów QEMU w warstwie
diagnostycznej.

---

## 1. Architektura HDA przed zmianami

Plik: `sterowniki/dzwiek/hda.cpp` (ok. 3300 linii) + `hda.h`.

### 1.1 Transport verbów

**Istniejący sterownik używa Immediate Command (ICW / ICIS / ICOI / ICII),
NIE CORB/RIRB.**

Rejestry CORB/RIRB są zdefiniowane i wyłączane przy starcie (CORBCTL/RIRBCTL
RUN=0), ale nie są używane do transportu. Każdy verb jest wysyłany przez
`wyslij_immediate()`:

1. czekanie aż ICB=0,
2. wyczyszczenie IRV (RW1C),
3. zapis komendy do ICOI,
4. ICB=1,
5. oczekiwanie IRV=1 i ICB=0,
6. odczyt ICII.

Decyzja: **nie wprowadzamy drugiego transportu.** Immediate Command działa
na QEMU i wystarcza do diagnostyki. CORB/RIRB byłby preferowany na sprzęcie
fizycznym ze względu na przepustowość, ale dodanie go to osobne zadanie —
wymaga DMA dla CORB/RIRB, obsługi RIRBWP i timeoutów. Diagnostyka używa
tego samego, sprawdzonego transportu co playback.

### 1.2 Odkrywanie kontrolera

`znajdz_hda()` skanuje PCI class 0x04 / subclass 0x03 (High Definition Audio).
Nie ma twardych kodowań BDF ani device ID. Pierwszy trafiony kontroler jest
mapowany przez BAR0 (32/64-bit MMIO), włączane są Memory Space + Bus Master.

### 1.3 Odkrywanie kodeka

`znajdz_kodek()`:
- czeka ≥521 µs (WALCLK) po CRST=1,
- czyta STATESTS (bity 14..0 = obecność CAD 0..14),
- wybiera pierwszy ustawiony CAD,
- fallback do CAD=0 gdy STATESTS puste (QEMU jednokodekowy),
- wysyła GetParameter Vendor ID (0x00).

**Ograniczenie przed zmianami:** jeśli vendor ≠ 0x1AF4 (QEMU hda-output),
inicjalizacja jest przerywana z komunikatem „topologia nieobsługiwana”.
To blokowało Acer z Realtek.

### 1.4 Enumeracja Function Group / widgetów

**Przed zmianami nie było enumeracji FG/widgetów.** Istniały twarde NID-y
QEMU (`QEMU_NID_AUDIO_FUNCTION_GROUP=0x01`, `QEMU_NID_DAC=0x02`,
`QEMU_NID_LINE_OUT=0x03`) używane wyłącznie do `konfiguruj_kodek_qemu()`.

### 1.5 Playback

- jeden Output Stream (tag 1), 48 kHz / 16-bit / stereo,
- DMA przez PMM (BDL do 48 wpisów, bufory 4 KiB),
- generator trójkąta (bez FP), ~75% amplitudy,
- `hda_test_ton(hz, ms)` synchroniczny, BWS 27.

---

## 2. Nowa warstwa diagnostyczna

### 2.1 Pliki produkcyjne

| Plik | Rola |
|------|------|
| `sterowniki/dzwiek/hda_diag_format.h` | czysty format/parsowanie (host-testowalny) |
| `sterowniki/dzwiek/hda_diag.h` | API jadra (snapshot, skan, test) |
| `sterowniki/dzwiek/hda_diag.cpp` | inkrementalny skan + publikacja atomowa |
| `sterowniki/dzwiek/hda_wewnetrzny.h` | wewnętrzny transport (nie dla Ring3) |
| `sterowniki/dzwiek/hda.cpp` | rozszerzony o `hda_wew_*` i test sweep/kanały |
| `bws_hda_diag.h` | publiczne ABI BWS 89/90 |
| `bws_api.h` | BWS_HDA_DIAG=89, BWS_HDA_TEST=90 |
| `bws_nowoczesne.cpp` | dispatchery BWS 89/90 |
| `programy/test_dzwieku/test_dzwieku.cpp` | aplikacja Ring3 |
| `programy/test_dzwieku/test_dzwieku_linker.ld` | linker .bur |
| `desktop_entries.h`, `menedzer_okien.cpp`, `psf_bootstrap.h`, `kernel.cpp` | rejestracja |
| `Makefile` | budowa + `test-hda-diag` |

### 2.2 Skan inkrementalny (bez blokowania bootu)

**Pełna enumeracja NIE jest wykonywana podczas bootu.** Splash i desktop
startują bez skanowania codeców.

Skan startuje z Ring3 (`Odśwież raport` / otwarcie aplikacji):

1. `BWS_HDA_DIAG_SKAN_START` — start,
2. `BWS_HDA_DIAG_SKAN_KROK` — maks. 16 transakcji verb na wywołanie,
3. status: `Skanowanie kontrolera` → `Skanowanie kodeka` →
   `Skanowanie widgetów` → `Budowanie ścieżek` → `Gotowe` / `Błąd skanu`,
4. po zakończeniu — atomowa publikacja migawki.

Aplikacja polluje co ~20 ms (ZegarSkanu). Desktop nie zamarza.

### 2.3 Limity migawki (HDA_DIAG_WERSJA_ABI = 1)

| Limit | Wartość |
|-------|---------|
| kontrolery | 2 |
| kodeki / kontroler | 2 |
| Function Groups / kodek | 2 |
| widgety / FG | 48 |
| wpisy connection list | 12 |
| ścieżki | 16 |
| długość ścieżki (NID) | 8 |
| slowa na krok skanu | 16 |

Przekroczenie ustawia `HDA_DIAG_FLAGA_OBECIETA` i raportuje obcięcie —
nie przelewa pamięci. Rozmiar `HdaDiagSnapshot` ≈ 32 KiB (statyczny,
podwojny bufor w jądrze). Brak nieograniczonych alokacji.

### 2.4 Dekodowanie (HDA 1.0)

**Audio Widget Capabilities** (GetParameter 0x05):

- Widget Type w bitach 23:20 (potwierdzone zrzutami /proc/asound:
  `wcaps 0x0040018F` → type 4 = Pin Complex).
- Flagi: Stereo(0), InAmp(1), OutAmp(2), AmpOvr(3), FormatOvr(4),
  Stripe(5), Proc(6), Unsols(7), ConnList(8), Digital(9).
- Typy: Audio Output, Audio Input, Audio Mixer, Audio Selector,
  Pin Complex, Power Widget, Volume Knob, Beep Generator, Vendor Defined.
- Surowa wartość hex zawsze obok dekodowania.

**Bezpieczeństwo verbów wg typu** (nie wysyłamy verbów nieobsługiwanych
przez typ — QEMU toleruje nadmiar, prawdziwy sprzęt nie):

- Pin Caps / Default Config → tylko Pin Complex,
- PCM size/rate → tylko Audio Output/Input,
- Connection List → tylko gdy ConnList w AWCAP,
- Amp Caps → tylko gdy InAmp/OutAmp w AWCAP,
- Get Power State → read-only, bezpieczny dla każdego widgetu.

**Pin Default Config** (Get Configuration Default 0xF1C):

- Port Connectivity, Location (Internal/External/Separate/Other chassis),
  Default Device (Speaker, Headphone Out, Line Out, Mic In, Line In,
  SPDIF Out, HDMI, Not Connected, …), Connection Type, Color,
  Misc, Association, Sequence.
- Klasyfikacja pinu **wyłącznie** z Default Config (port + device) —
  **zero zgadywania po numerze NID**.

**Connection List**:

- short form (1 B) i long form (2 B),
- wpisy range (bit 15, NID start..end) rozwijane,
- walidacja NID ≤ 0x7F, end ≥ start, zgodność liczby wpisów,
- surowe slowa i rozwinięta lista NID-ów w snapshotach,
- malformed list → `HDA_DIAG_BLAD_LISTA_POLACZEN`, nie crash.

### 2.5 Graf topologii i ścieżki

Budowany w jądrze (czysta funkcja `hda_zbuduj_sciezki`, host-testowalna):

- krawędź: źródło → odbiorca (kto ma źródło na liście połączeń),
- DFS z każdego Audio Output, **visited set w bieżącej ścieżce**,
- **detekcja cykli** (flaga, bez zawieszenia),
- limit długości ścieżki i liczby ścieżek z obcięciem,
- pośrednicy: Audio Mixer i Audio Selector,
- wynik: konwerter → … → Pin + Default Config pinu.

Klasyfikacja ścieżek (z Default Config pinu):

- **Speaker** — Default Device = Speaker,
- **Headphone Out** — Default Device = Headphone Out,
- **Line Out** — Line Out,
- **HDMI** — Digital Other Out / Digital Other In / Optical,
- **Not Connected** — Port = No Physical Connection.

Wiele kandydatów głośnika jest pokazywanych — **nie wybieramy cicho jednego**.

### 2.6 Routing fizyczny

**Routing fizyczny NIE został zaimplementowany — świadomie pozostaje read-only.**

Powód: nie znamy topologii Realtek na Acerze. Zgadywanie NID-ów i pisanie
verbów routingowych byłoby niebezpieczne. Aplikacja raportuje aktualną
ścieżkę odtwarzania (stała QEMU: konwerter/pin) i jawnie informuje
`routing QEMU, brak auto-routingu fizycznego`.

Architektura jest gotowa na przyszły `route config`: ścieżki z grafu są
w snapshotach (konwerter, pin, hop NID-y). Przyszły kernel-owned
`controlled route config` może użyć zwalidowanej ścieżki i zapisać
poprzednie stany (D0, Connection Select, Pin Widget Control, Converter
Stream/Channel/Format, mute/gain) z restore. **Nie piszemy verbów
vendor-specific, nie robimy jack retasking, nie nagrywamy mikrofonu.**

---

## 3. ABI BWS (zamrożone)

### 3.1 Nowe numery

| Numer | Nazwa | Znaczenie (zamrożone) |
|-------|-------|------------------------|
| **89** | `BWS_HDA_DIAG` | diagnostyka: status, skan, snapshot, strumień, stan testu |
| **90** | `BWS_HDA_TEST` | kontrolowany test odtwarzania (start/stop) |

`BWS_PIERWSZY_WOLNY = 91`. **Istniejące numery 1..88 bez zmian**
(w tym BWS 27 = `BWS_DZWIEK_TON` działa jak dotąd).

**Nie istnieje syscall z surowymi verbami HDA.** Żaden numer BWS nie
przyjmuje slowa komendy CAD/NID/verb.

### 3.2 Struktury (każda ma `wersja` + `rozmiar`)

```c
// arg1 = 1..6
BWS_HDA_DIAG:
  1 BWS_HDA_DIAG_STAN      arg2=HdaDiagSkan*
  2 BWS_HDA_DIAG_SKAN_START
  3 BWS_HDA_DIAG_SKAN_KROK   (zwraca 1 = koniec)
  4 BWS_HDA_DIAG_SNAPSHOT    arg2=BwsHdaSnapshotZadanie*
  5 BWS_HDA_DIAG_STRUMIEN    arg2=HdaDiagStrumien*
  6 BWS_HDA_DIAG_TEST_STAN   arg2=HdaDiagStanTestu*

// arg1 = 1..2
BWS_HDA_TEST:
  1 BWS_HDA_TEST_START     arg2=BwsHdaTestStart*
  2 BWS_HDA_TEST_STOP
```

```c
struct BwsHdaTestStart {
    uint32_t wersja;            /* HDA_DIAG_WERSJA_ABI */
    uint32_t rozmiar;           /* sizeof */
    uint32_t tryb;              /* HdaDiagTrybTestu */
    uint32_t czestotliwosc_hz;
    uint32_t czas_ms;
    uint8_t  kanaly;            /* bit0=L, bit1=R */
    uint8_t  rezerwa[3];
};

struct BwsHdaSnapshotZadanie {
    uint32_t wersja;
    uint32_t rozmiar;
    uint64_t bufor;             /* user ptr na HdaDiagSnapshot */
    uint32_t bufor_maks;
    uint32_t rezerwa;
    uint32_t generacja;         /* OUT */
    uint32_t zapisano;          /* OUT */
};
```

### 3.3 Uprawnienia PZB

- Odczyt diagnostyki i test tonu: **`PRAWO_GUI`** (jak BWS 27).
- Zapis raportu: **`PRAWO_PLIKI_ZAPISZ`** przez BWS plików (osobno).
- Reset/shutdown: wymagają `PRAWO_SYSTEM_CONFIG` + poziom ≥ PZB_ZAUFANE
  (bez zmian).
- Niskie poziomy PZB nie dostają praw automatycznie.

### 3.4 Izolacja Ring3

- Ring3 **nie** ma dostępu do MMIO, DMA ani verbów.
- Wszystkie wskaźniki user → kernel przez `skopiuj_z/do_przestrzeni_uzytkownika`
  + `czy_bezpieczny_zakres_uzytkownika_do_odczytu/zapisu`.
- Twardy limit bufora snapshotu (`sizeof(HdaDiagSnapshot)+4KiB`).
- Zła wersja/rozmiar struktury → odmowa (0).
- BDL: tylko zamaskowane bity fizyczne (`maska_fizycznego`), bez uzytkowalnego
  adresu. BAR0 jest wartością diagnostyczną, nie mapowaniem.
- Snapshot publikowany atomowo (podwójny bufor + generacja) — Ring3 widzi
  albo starą, albo nową kompletną migawkę, nigdy pół na pół.
- `hda_diag_wyslij_verb` istnieje tylko w jądrze; **nie jest eksponowane
  przez BWS**.

---

## 4. Testy tonów

Wszystkie generowane w jądrze (`hda_wew_test_start`), istniejącą ścieżką
playback (format 48 kHz / 16-bit / stereo). Bez FP (trójkąt + accumulator
fazy). Amplituda **~50%** (`HDA_TEST_AMPLITUDA=16384`) — konserwatywna,
bez przycięcia. Gain kodeka bez zmian (nie vendor coefficients).

| Przycisk | Tryb | Częstotliwość | Kanały | Czas |
|----------|------|---------------|--------|------|
| Test 440 Hz | ton | 440 Hz | L+R | 1.5 s auto-stop |
| Test 1000 Hz | ton | 1000 Hz | L+R | 1.5 s |
| Lewy kanał | ton | 440 Hz | L | 1.5 s |
| Prawy kanał | ton | 440 Hz | R | 1.5 s |
| Oba kanały | ton | 440 Hz | L+R | 1.5 s |
| Krótki sweep | sweep | 200→4000 Hz | L+R | ~1 s |
| **Stop** | — | — | — | natychmiast |

Zasady:
- **Auto-stop** po `czas_ms` (WALCLK). Bez ultradźwięków i sub-basów.
- **Stop** → `hda_diag_test_stop()` → `zatrzymaj_stream_wewnetrznie()`
  (bezpieczny, idempotentny, nie resetuje kontrolera).
- Sweep: bezpieczny zakres **200–4000 Hz**, liniowy narast fazy.
- Przed startem UI pokazuje CAD, konwerter NID, pin NID i czy routing
  jest fizyczny czy QEMU.

---

## 5. Raport

- Przycisk **Odśwież raport** — nowy skan + formatowanie.
- Przycisk **Zapisz raport** — `/dane/diagnostyka/hda.txt`
  (katalog tworzony przez `utworz_katalog_uzytkownika` / BWS 46,
  z poszanowaniem PZB/PSF).
- Przycisk **Kopiuj raport** — `clipboard_set("text/plain", …)`
  (gdy schowek dostępny; nie jest wymaganiem v1).
- Format: UTF-8, deterministic, surowe wartości z sufiksem `raw`
  obok dekodowania. Wzorzec:

```
HDA Controller 0
PCI 0 31 3
Vendor 8086
Device A348
GCAP raw 0x...
STATESTS raw 0x...
Codec address 0
Codec Vendor 10EC
Codec Device ...
AFG NID 0x01
Widget NID 0x02
Type Audio Output
AWCAP raw 0x...
Widget NID 0x14
Type Pin Complex
DefaultConfig raw 0x...
DefaultDevice Speaker
Location Internal
Connections 0x0C 0x02
```

Zawartość: wersja OS, schema version, kontrolery (PCI, GCAP, GCTL, STATESTS,
OSS/ISS/BSS, DMA64), kodeki (Vendor/Device/Revision), FG, widgety (typ,
AWCAP, PinCaps, DefaultConfig, amp, PCM, connection list), ścieżki,
ścieżka testu, stan strumienia (tag, format, CBL, LVI, LPIB, CTL, STS,
BCIS/FIFOE/DESE/RUN, BDL zamaskowany), błędy.

Obcięcie raportu jest **jawne** (`UWAGA: raport obcięty…`).

---

## 6. Zakres nagrań / mixer

- **Mikrofon / nagrywanie: poza zakresem.** Wejściowe widgety i piny Mic In
  są raportowane w diagnostyce, ale nie ma capture.
- **Mixer systemowy: poza zakresem.**
- **Publiczne ABI streamingu audio: poza zakresem.**

---

## 7. Bezpieczeństwo — podsumowanie

| Wymaganie | Realizacja |
|-----------|------------|
| brak MMIO w Ring3 | tak — wszystko przez BWS |
| brak surowych verbów z Ring3 | tak — brak takiego syscalla |
| brak swobodnych adresów fizycznych | tak — BDL maskowany |
| walidacja wskaźników/limitów | tak — copy + range + hard cap |
| PZB | PRAWO_GUI (diag/test), PRAWO_PLIKI_ZAPISZ (raport) |
| wersja+rozmiar w strukturach | tak |
| atomowy snapshot | podwójny bufor + generacja |
| bounded enumeration | limity + flaga obcięcia |
| brak blokady bootu | skan startuje z Ring3 po desktopie |
| brak zgadywania NID / routingu | tak |

---

## 8. Testy hostowe

Pliki:
- `narzedzia/test_hda_diag.cpp` — parsowanie, topologia, raport, limity.
- `narzedzia/test_hda_bws_isolation.cpp` — izolacja BWS, PZB, atomowość.

Uruchomienie: `make test-hda-diag` (ASan + UBSan).

### 8.1 Wyniki

| Zestaw | Testy | Błędy |
|--------|------:|------:|
| test_hda_diag | 143 | 0 |
| test_hda_bws_isolation | 148 | 0 |
| **Razem** | **291** | **0** |

ASan: **PASS** (brak błędów). UBSan: **PASS** (brak błędów).

Pokrycie (m.in.): STATESTS (0/1/wiele kodeków — symulowane), adresy kodeków,
GetParameter (Vendor/Rev/SubNode/FGType/AWCAP/PCM/ConnList), zakresy FG i
widgetów, Audio Output, Pin Complex, Mixer, Selector, typy nieznane, złe
zakresy podwęzłów, overflow, connection list short/long/range/malformed,
cykle grafu, piny niepodłączone, wiele ścieżek głośnika, jedna ścieżka,
headphone, HDMI, Default Config, amp caps, PCM caps, format raportu,
znaczniki obcięcia, maks. rozmiar snapshotu, złe odpowiedzi kodeka,
propagacja timeoutu (kody błędów), publikacja atomowa, wielokrotny refresh.

Testy izolacji: brak surowych verbów, odrzucanie złych wskaźników i
za dużych buforów, wymóg PRAWO_GUI, brak ujawniania mapowań MMIO/BDL,
wielokrotne odświeżanie.

### 8.2 Testy istniejące

- `make test-bursztyn-ui-polecenia` — 23/23 PASS.
- `make test-explorer-host` — 15488/15488 PASS.
- Brak osłabienia istniejących testów HDA (BWS 27 / `hda_test_ton`
  bez zmian API).

QEMU/VirtualBox **nie były uruchamiane automatycznie** — walidacja ręczna.

---

## 9. Budowa końcowa

```
make -B -j2 all iso
```

Wynik: **SUCCESS** (kernel + aplikacja + ISO).

- `test_dzwieku.bin` — 659 456 B
- `BursztynOS.iso` — 36 130 816 B
- **SHA256 BursztynOS.iso:**
  `765ffffecccad18e001bc054c2b855e1f1a84c4614034fbd02bbe67d007206ac`

---

## 10. Akceptacja Acer Nitro AN515-54 (planowana)

Pierwszy test fizyczny wymaga wyłącznie:

1. Bursztyn startuje normalnie, desktop bez nowego opóźnienia splash.
2. **Test dźwięku** otwiera się z launchera / Menu Start.
3. Wykryty kontroler Intel 8086:A348 (i ewentualnie NVIDIA HDA).
4. Pokazany STATESTS.
5. ≥1 kodek odpowiada — Vendor ID i Device ID.
6. Odkryte Audio Function Groups.
7. Widgety wyliczone; Pin Complex z dekodowanym Default Config.
8. Listy połączeń widoczne.
9. `/dane/diagnostyka/hda.txt` zapisuje się.

**Nie twierdzimy, że odtwarzanie na Realtek działa** — wymaga ręcznego
testu i potwierdzenia słyszanego tonu. Po zebraniu raportu zdecydujemy
o konfiguracji ścieżki głośnika.

---

## 11. Zachowane obszary

Wejście fizyczne Acer, koegzystencja touchpad/USB, prace xHCI, splash
(behawior i opóźnienia), sieć, storage, instalator, pozostałe aplikacje —
**bez zmian**.
