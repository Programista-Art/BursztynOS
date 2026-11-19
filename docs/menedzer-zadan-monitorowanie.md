# Menedżer zadań – monitorowanie dysków, sieci, I/O procesów i grafiki

Dokument techniczny rozszerzenia Menedżera zadań (gałąź `feature/menedzer-zadan`, 2026-11-19).
Uzupełnia `docs/menedzer-zadan.md` (BWS 103/104, CPU, RAM, zakończenie procesów).

## 0. Rozpoznanie architektury (przed implementacją)

Stan kodu sprawdzony bezpośrednio w źródłach, nie w dokumentacji.

### Pamięć masowa

| Element | Stan faktyczny |
|---|---|
| Sterowniki | `sterowniki/magazyn/ahci.cpp` (SATA DMA, IDENTIFY) i `sterowniki/virtio/virtio_blk.cpp`. **Brak NVMe** (typ `BlockDeviceType::NVMe` istnieje tylko w enumie). Brak ATAPI/CD. |
| Rejestr | `sterowniki/magazyn/blokowy.cpp`: tablica 32 urządzeń w kolejności rejestracji, bez wyrejestrowania. Każdy dysk jest owinięty urządzeniem-filtrem (polityka zapisu instalatora), które woła `underlying->read/write`. |
| Ścieżka I/O | `BlockDevice::read/write` (inline, `block_device.h`) → `ops->read/write` sterownika. Oba sterowniki są synchroniczne (polecenie → oczekiwanie na zakończenie, bez kolejki, bez ponowień w sterowniku). Duże transfery są dzielone na porcje `max_transfer_sectors` w `BlockDevice::read/write`. |
| Ścieżki omijające filtr | Sonda `virtio_blk` (`Storage::source_iso`, `gpt_read` na surowym `drv->bdev`) – nadal przez `BlockDevice::read` urządzenia sprzętowego. |
| Istniejące liczniki | `BlockDevice::write_count` (sektory). **Uwaga:** dla AHCI ten licznik jest zwiększany dwa razy (w `ahci_operacja_dysku` i w `BlockDevice::write`) – nie nadaje się do telemetrii; nie jest używany przez nowe liczniki. `virtio_blk` ma prywatne `total_reads/total_sectors_read` (niewystawione). Brak liczników odczytu w warstwie ogólnej. |
| Model / numer seryjny | AHCI: model z IDENTIFY (prawdziwy). VirtIO-BLK: stały napis sterownika „VirtIO Block Disk” – to nie jest model urządzenia. Numer seryjny nie jest potrzebny i nie jest wystawiany. |
| System plików | PSF/BSP2 działa zawsze na ramdysku (`ram_dysk`) z pamięcią podręczną bloków i zapisem zmian (`bsp_zapisz_zmiany`, `psf_flush_cache`) na partycję BSP2, jeśli jest zamontowana. W Live ISO nie ma partycji – zero I/O dysku; operacje plikowe to wyłącznie RAM. |

Wniosek: jedyne miejsce, przez które przechodzi każda fizyczna operacja dokładnie raz, to
`BlockDevice::read/write` **urządzenia sprzętowego** (nie filtra). Tam liczymy; filtr jest wyłączony z liczenia.

### Sieć

| Element | Stan faktyczny |
|---|---|
| Sterowniki | `sterowniki/siec/e1000.cpp` (nazwa `eth0`), `sterowniki/virtio/virtio_net.cpp` (`virtio-netN`). |
| Rejestr | `network_device.cpp`: do 8 urządzeń; `net_unregister_device` przesuwa indeksy (indeks nie jest stabilny). |
| Liczniki | `NetworkDevice::stats`: RX liczone raz w `net_receive_frame` (ramka zaakceptowana przez sterownik, bez FCS), TX liczone w operacji `transmit` sterownika po potwierdzeniu wysłania (oba sterowniki czekają na zakończenie deskryptora). `e1000_netdev_get_stats` miesza pola z liczników sterownika – telemetria czyta bezpośrednio `dev->stats`. |
| Stos | `jadro/siec/siec.cpp`: jedna sesja TCP naraz, wysyłka tylko przez kartę główną; brak retransmisji (RTO). Właściciel operacji: `NetworkLease` (HTTP/HTTPS) albo `socket_stan.owner_pid` (BWS 79–83). |
| Prędkość łącza | Żaden sterownik jej nie raportuje → niedostępna. |

### Grafika

| Element | Stan faktyczny |
|---|---|
| Backend | `grafika_mozliwosci_ekranu().backend`: VESA, UEFI GOP, Bochs VBE albo VirtIO-GPU (2D, bez virgl/3D). |
| Liczniki | `jadro/gui/graphics_perf.h` (zawsze wkompilowane): klatki kompozytora, czas składania i prezentacji, piksele, pełne klatki, kopie do framebuffera (bajty, czas), polecenia VirtIO-GPU i czas oczekiwania, transfery/flush (liczba, piksele), prezentacje kursora. `VirtioGpuDriver`: `error_count`, `recovery.late_completions`. Pamięć buforów grafiki: `graphics_resources.live_bytes`; warstwy okien: `zajete_bajty_warstw`. |
| Obciążenie GPU | **Brak licznika sprzętowego** w każdym backendzie. Nie jest i nie będzie wyświetlany procent GPU. |

### Procesy i BWS

- Tożsamość procesu: (PID, generacja), generacja nadawana przy rezerwacji slotu (`scheduler_generacja_procesu`). 16 slotów.
- Numeracja BWS: 1–104 zajęte, 99 trwale zarezerwowane, `BWS_PIERWSZY_WOLNY = 105`.
  Wybrane: **105** (telemetria urządzeń) i **106** (liczniki I/O procesów).
- Syscall w Ring 0 nie jest wywłaszczany; przerwania tak. Migawki robione są w krótkich sekcjach z wyłączonymi przerwaniami.

### Co da się zmierzyć

| Metryka | Status |
|---|---|
| Bajty/operacje odczytu i zapisu dysku, błędy | dokładne (nowe liczniki w `BlockDevice`) |
| Czas operacji dysku | dokładny dla sterowników synchronicznych przy dostępnym HPET (czas od wysłania do zakończenia polecenia) |
| Zajętość dysku w % | nie wyświetlana (brak kolejki – „czas I/O w ms/s” jest pokazany zamiast procentu) |
| Bajty/pakiety RX/TX, błędy, odrzucone | dokładne (istniejące liczniki sterowników) |
| Prędkość łącza | niedostępna |
| I/O dysku procesu – logiczne bajty plików | dokładne na granicy wywołań plikowych BWS |
| I/O dysku procesu – fizyczne | **nieprzypisywalne** (zapis zmian z pamięci podręcznej PSF jest odroczony i zbiorczy) |
| Sieć procesu – bajty danych TCP | dokładne dla sesji TCP z właścicielem (gniazda BWS 79–83, HTTP/HTTPS) |
| DNS, ICMP, ARP, DHCP procesu | nieprzypisywane (tylko w sumach interfejsu) |
| Obciążenie GPU | niedostępne – brak licznika sprzętowego |

## 1. Etap 1 – liczniki urządzeń i BWS 105

### Dyski (`sterowniki/magazyn/block_device.h`, `BlockIoStats`)

Liczone w `BlockDevice::read/write/flush` **urządzenia sterownika** (AHCI, VirtIO-BLK). Urządzenia-filtry rejestru
blokowego mają `licz_io = false`, więc operacja przechodząca przez filtr i sterownik jest liczona raz.

| Licznik | Dokładne znaczenie |
|---|---|
| `bajty_odczytane` / `bajty_zapisane` | sektory × rozmiar sektora poleceń zakończonych `BlockError::Ok` |
| `operacje_odczytu` / `operacje_zapisu` | liczba udanych poleceń wysłanych do sterownika (porcja ≤ `max_transfer_sectors`) |
| `bledy_*` | polecenia, które sterownik zakończył błędem (IOError, Timeout, …). Żądania odrzucone przed sterownikiem (zły zakres, NULL, tylko do odczytu) nie są poleceniami i nie są liczone. |
| transfer częściowy | porcje udane liczą się jako udane, porcja z błędem jako 1 błąd, porcje niewysłane – wcale |
| ponowienia | sterowniki nie ponawiają poleceń; każde wysłanie = jedno polecenie |
| pamięć podręczna | odczyt z pamięci podręcznej PSF i operacje na ramdysku nie wołają `BlockDevice` – nie są transferem fizycznym |
| asynchroniczność | brak: oba sterowniki kończą polecenie przed powrotem |
| `czas_*_ns`, `operacje_z_czasem` | czas od wysłania do zakończenia polecenia (HPET). Menedżer pokazuje „czas I/O” tylko w przedziale, w którym **każde** polecenie miało pomiar. Procent zajętości nie jest pokazywany. |
| `oprozniania` | polecenia FLUSH |

Liczniki są 64-bitowe i nasycają się (nie zawijają). Reset licznika (nowa struktura sterownika) Menedżer
rozpoznaje po spadku wartości i pomija ten przedział.

### Sieć (`NetworkDevice::stats`)

| Licznik | Dokładne znaczenie |
|---|---|
| `rx_bajty`, `rx_pakiety` | ramki Ethernet przyjęte przez sterownik i przekazane do stosu (`net_receive_frame`); długość ramki bez FCS, z nagłówkiem Ethernet; broadcast/multicast liczone jak inne ramki |
| `rx_bledy`, `rx_odrzucone` | ramki o złej długości i błędy kolejki RX zgłoszone przez sterownik |
| `tx_bajty`, `tx_pakiety` | ramki, których wysłanie sterownik potwierdził (deskryptor zakończony); długość z dopełnieniem do 60 B, bez FCS |
| `tx_bledy`, `tx_odrzucone` | nieudane wysłania (timeout, pełna kolejka, błąd DMA); odrzucone = nie przekazane urządzeniu |

Każdy pakiet jest liczony w jednym miejscu (RX w warstwie ogólnej, TX w sterowniku), bez alokacji i logowania.
Stos nie retransmituje TCP. Stały identyfikator interfejsu: `registration_id` (numer rejestracji; ponowna
rejestracja = nowe id). Prędkość łącza: nieraportowana przez sterowniki → „Niedostępne”.

### BWS 105 – `BWS_URZADZENIA_TELEMETRIA`

- Wejście: `(bufor, rozmiar ≥ 2408 B, wersja = 1)`. Odpowiedź o stałym rozmiarze: nagłówek, do 8 dysków, do 8 interfejsów, 1 wpis grafiki.
- Prawo: `PRAWO_ZARZADZANIE_PROCESAMI` i PZB ≤ ZAUFANE (jak BWS 103) – adres MAC nie trafia do zwykłych aplikacji.
- Walidacja: wersja, rozmiar, zakres Ring 3 do zapisu; przy błędzie bufor nie jest zmieniany.
- Spójność: liczniki są kopiowane w jednej krótkiej sekcji bez przerwań do bufora BSS; kopia do Ring 3 jest wykonywana po niej.
- Bez wskaźników jądra, adresów DMA, numerów seryjnych; nazwy tylko drukowalne ASCII.
- Live ISO: flaga `BWS_URZ_PSF_W_RAM` – pliki żyją w RAM, ruch plików nie jest ruchem dysku.

## 2. Etap 2 – I/O procesów, BWS 106 i kolumny „Dysk” / „Sieć”

### Model własności (`jadro/bws/rozliczanie_io.h`)

| Zdarzenie | Gdzie | Właściciel |
|---|---|---|
| odczyt pliku | `bws_czytaj_plik` (BWS 5), `bws_czytaj_fragment_pliku` (BWS 59) – po skopiowaniu danych do Ring 3 | proces wywołujący (PID + bieżąca generacja) |
| zapis pliku | `bws_zapisz_plik` (BWS 3, także strumieniowo), `bws_zapisz_fragment_pliku` (BWS 98) – po przyjęciu przez PSF | proces wywołujący |
| wysłanie TCP | `tcp_wyslij_internal` – payload segmentu przekazanego karcie | właściciel sesji TCP |
| odbiór TCP | `tcp_przyjmij_payload` – dane przyjęte w kolejności (potwierdzone ACK) | właściciel sesji TCP |

- **Właściciel sesji** to (PID, generacja) zapisane w `tcp_rezerwuj_sesje`. Wywołuje się ją w syscallu procesu, który otwiera połączenie (gniazdo BWS 80 albo HTTP/HTTPS BWS 29/30/94/95). Właściciel jest kasowany przy zwolnieniu sesji.
- **Pakiet odebrany w przerwaniu** albo przy odpytywaniu z innego kontekstu trafia do właściciela sesji, nie do procesu akurat wykonywanego.
- **Proces się zakończył albo jego PID przejęła nowa generacja:** bajty trafiają do kubełka `siec_zakonczone_*`, a nie do nowego procesu. Nowa generacja zaczyna od zera. Gniazdo i dzierżawa sieci są zamykane przy sprzątaniu procesu, więc późne pakiety zdarzają się tylko w wąskim oknie.
- **Sesja otwarta w PID 0** trafia do kubełka „jądro”, wyświetlanego w wierszu „Jądro systemu”. Ruch DNS (UDP), ICMP, ARP i DHCP oraz nagłówki IP/TCP/Ethernet nie są przypisywane procesom. Pozostają w sumach interfejsu (etap 1).
- **Aplikacja nie podaje swojej tożsamości:** jądro bierze ją z `proces.pid` i `aktualny_pid` w syscallu, więc nie da się jej podrobić.
- **Brak wskaźników do `proces_t`:** zapisywane są tylko liczby.

### Dokładne znaczenie kolumn

- **Dysk** = tempo **logicznych** bajtów plików (odczyt + zapis) przekazanych przez wywołania plikowe BWS, liczone w przedziale ~1 s.
  To nie są fizyczne transfery dysku:
  - w Live ISO pliki żyją w RAM;
  - w zainstalowanym systemie PSF zapisuje zmiany z pamięci podręcznej zbiorczo i z opóźnieniem, więc fizycznego I/O nie da się rzetelnie przypisać procesowi.

  Fizyczne I/O dysku jest widoczne w karcie Wydajność.
  Niewliczane:
  - odczyt obrazu programu przez loader;
  - kopiowanie w jądrze (BWS 55);
  - metadane;
  - listowanie katalogów.

  „—” w wierszu jądra: jądro nie używa wywołań plikowych BWS.
- **Sieć** = suma tempa wysłanych i odebranych bajtów danych TCP sesji, której właścicielem jest proces. Szczegóły procesu pokazują wysłane i odebrane osobno oraz wartości skumulowane.
- **„Niedostępne”** = brak przedziału pomiaru: pierwsza próbka, błąd BWS 106 albo brak zegara. Nigdy nie jest pokazywane jako 0.
- **Nowa generacja**, której nie było w poprzedniej migawce, powstała w tym przedziale od zera, więc jej tempo jest prawdziwe.

### BWS 106 – `BWS_PROCESY_IO`

- Wejście: `(bufor, rozmiar ≥ 848 B, wersja = 1)`. Prawo i walidacja jak w BWS 105.
- Zawartość:
  - wpis dla PID 0 (tylko sieć jądra);
  - wpis dla każdego zajętego slotu z bieżącą generacją;
  - kubełki ruchu nieprzypisanego.
- Spójność: tabela i generacje slotów są czytane w jednej sekcji bez przerwań.
- BWS 103 V2 nie zmienił się. Model dopasowuje wpisy BWS 106 do wierszy BWS 103 parą (PID, generacja).

### Tabela procesów

- **Kolumny:** Nazwa, PID, Stan, Procesor, Pamięć, Dysk, Sieć, Czas działania (950 DIP). Każda kolumna sortuje, także Czas działania (dłużej działające pierwsze).
- **Przewijanie poziome** `TabelaWirtualna` jest opcjonalne, włączone tylko w Menedżerze; inne aplikacje bez zmian. Pasek pojawia się, gdy kolumny się nie mieszczą. Obsługa: Shift+rolka, rolka pozioma, klik paska, strzałki ←/→.
- **Przy 1280×720 przy skali 100 %** pasek się nie pojawia.

## 3. Etap 3 – grafika i nowa karta „Wydajność”

### Liczniki grafiki (wpis `BwsGrafikaInfoV1` w BWS 105)

| Pole | Znaczenie | Źródło |
|---|---|---|
| `backend` | aktywny backend: VESA / UEFI GOP / Bochs VBE / VirtIO-GPU 2D | `grafika_mozliwosci_ekranu()` |
| tryb | szerokość, wysokość, bpp, pitch aktywnego trybu | `grafika_tryb_ekranu()` |
| `pci_vendor/device`, `format_virtio` | tylko VirtIO-GPU: identyfikator PCI urządzenia i format zasobu scanout | `VirtioGpuDriver` |
| `klatki`, `pelne_klatki` | klatki kompozytora (prezentacje sceny) | `GraphicsPerf::Frames/FullFrames` |
| `czas_skladania_ns` | czas ścienny programowego składania sceny przez kompozytor (CPU, PID 0) | `GraphicsPerf::ComposeNs` |
| `czas_prezentacji_ns` | czas przekazania klatki do urządzenia | `GraphicsPerf::PresentNs` |
| `kopie_bajty`, `czas_kopii_ns` | GOP/VESA/Bochs: kopie backbuffer → framebuffer | `GraphicsPerf::CopyBytes/CopyNs` |
| `polecenia_gpu`, `czas_oczekiwania_gpu_ns` | VirtIO-GPU: polecenia controlq i czas oczekiwania na ich zakończenie | `GraphicsPerf::GpuCommands/GpuWaitNs` |
| `transfery`, `piksele_transferu`, `flush`, `piksele_flush` | VirtIO-GPU: `TRANSFER_TO_HOST_2D` i `RESOURCE_FLUSH` | `GraphicsPerf` |
| `bledy_gpu`, `spoznione_gpu`, flaga błędu trwałego | VirtIO-GPU: błędy poleceń, zwalidowane spóźnione zakończenia | `VirtioGpuDriver` |
| `prezentacje_kursora` | szybkie prezentacje samego kursora | `GraphicsPerf::CursorPresents` |
| `pamiec_warstw_bajtow` | bufory warstw okien kompozytora | `zajete_bajty_warstw` |
| `pamiec_buforow_bajtow` | bufory grafiki jądra (backbuffer, tapeta) | `graphics_resources.live_bytes` |

**Obciążenie GPU nie istnieje jako licznik w żadnym backendzie.** Karta pokazuje „Brak licznika sprzętowego”.
Czas składania jest czasem CPU kompozytora i jest opisany jako „Składanie programowe (CPU)”, a nie jako
obciążenie GPU. Transfer do urządzenia to bajty, a nie procent wykorzystania.

### Karta „Wydajność”

- **Po lewej:** `ListaWyboru` z kategoriami budowanymi z migawek: Procesor, Pamięć, każdy dysk z BWS 105 („Dysk N (typ)”), każdy interfejs („Ethernet nazwa”) i aktywny backend grafiki. Nie ma pozycji dla nieistniejących urządzeń. Wiersz ma dwie linie: nazwę i bieżącą wartość, rysowane bez alokacji. Wybrana kategoria ma pomarańczowy pasek akcentu.
- **Po prawej:** nagłówek, jeden `Wykres` (60 próbek; dysk: odczyt/zapis, sieć: odbiór/wysyłanie, grafika: klatki/s, CPU/RAM w %) i tabela „Właściwość / Wartość” z pełnymi szczegółami kategorii. Wartości niedostępne są opisane słowem.
- **Koszt rysowania:**
  - lista jest przebudowywana tylko po zmianie zestawu urządzeń;
  - wiersz listy jest przerysowywany tylko, gdy zmienił się jego tekst;
  - wykres tylko, gdy zmieniły się punkty;
  - tabela szczegółów tylko, gdy zmienił się jej tekst.
- **Ukryta karta:** niczego nie rysuje ani nie przelicza. Model zbiera historie (jedno wywołanie BWS 105 na sekundę), więc po powrocie historie są ciągłe.
- **Brak zawijanych etykiet:** szczegóły są w tabeli jednowierszowych komórek, więc regresja kosztu zawijania z `etykieta.h` nie ma gdzie wrócić.

### Status metryk

| Metryka | Status |
|---|---|
| CPU, RAM | dokładne (bez zmian) |
| Dysk: bajty, operacje, błędy, FLUSH | dokładne |
| Dysk: czas operacji | dokładny przy HPET, gdy każde polecenie przedziału miało pomiar |
| Dysk: zajętość % | **nieobsługiwane** (pokazywany jest czas I/O w ms/s) |
| Sieć: bajty, pakiety, błędy, odrzucone, MAC, stan łącza | dokładne (stan łącza – gdy sterownik go raportuje) |
| Sieć: prędkość łącza | **nieobsługiwane** przez sterowniki |
| Proces: logiczne I/O plików | dokładne (definicja w etapie 2) |
| Proces: fizyczne I/O dysku | **nieprzypisywalne** |
| Proces: dane TCP | dokładne dla sesji z właścicielem; DNS/ICMP/ARP/DHCP – nieprzypisywane |
| Grafika: klatki, czasy kompozytora, transfery, polecenia VirtIO-GPU, pamięć | dokładne (definicje wyżej) |
| Grafika: obciążenie GPU | **nieobsługiwane** – brak licznika sprzętowego |

## 4. Poprawki interfejsu (2026-11-19, druga runda)

### Nagłówek „Właściwość / Wartość” zasłaniany przez pierwszy wiersz – `bursztyn_ui/tabela_wirtualna.h`

**Przyczyna (komponent współdzielony):**
- `TabelaWirtualna::rysuj_siebie` rysował nagłówek pierwszy, a wiersze od `y = wysokość_nagłówka − (przewinięcie % wysokość_wiersza)`.
- Wiersz częściowo przewinięty był rysowany w całości, a przycięcie do obszaru danych działało tylko przy poziomym pasku.
- Każde przesunięcie niebędące wielokrotnością wiersza zamazywało nagłówek. Takie przesunięcia dają:
  - przewinięcie do końca (maksimum = `total_h − table_h`);
  - klik w suwak;
  - nieaktualne przesunięcie po podmianie danych (np. przełączenie na kategorię z mniejszą liczbą wierszy);
  - `zapewnij_widocznosc_wiersza` przed pierwszym układem (wysokość 0 dawała dodatnie przesunięcie).

**Poprawka, centralnie w komponencie:**
- wiersze są zawsze przycięte do obszaru pod linią nagłówka;
- przesunięcie jest normalizowane przy każdym rysowaniu;
- przed układem tabela nie przewija.

**Nowe opcje, domyślnie wyłączone (inne aplikacje bez zmian):**
- `ustaw_sortowanie_naglowkiem(false)` – brak strzałki i reakcji na klik w tabelach bez sortowania;
- `ustaw_szerokosc_wg_naglowkow(true)` – kolumna nie węższa niż jej nagłówek zmierzony aktywną czcionką plus odstępy i strzałka. Wynik jest zapamiętywany do zmiany skali, generacji czcionki BSF, rozmiaru UI, kroju (postęp znaku „W”) albo dostawcy danych.

### Nawigacja karty Wydajność – `bursztyn_ui/lista_wyboru.h` + `programy/menedzer_zadan/okno.h`

**`ListaWyboru::ustaw_tryb_nawigacji(true)`** (domyślnie wyłączony):
- śledzi wiersz pod kursorem (`indeks_najechany()`) i przerysowuje tylko stary i nowy wiersz;
- gasi najechanie po wyjściu kursora;
- Spacja/Enter zatwierdzają wiersz z fokusem; Góra/Dół działały wcześniej.

**Wiersz Menedżera to „karta” przycisku na całej szerokości wiersza:**

| Stan | Wygląd |
|---|---|
| zwykły | tło przycisku |
| najechany | tło przycisku najechanego |
| wybrany | tło zaznaczenia + pomarańczowy pasek akcentu (widoczny od otwarcia karty, niezależnie od kursora) |
| fokus | ramka akcentu |
| wyłączony | przygaszony tekst |

Strzałka „>” po prawej sygnalizuje przejście do widoku. Wybór jest trzymany jako stała tożsamość urządzenia. Gdy wybrane urządzenie zniknie, wybór wraca do „Procesor”.

### Kolumna „Czas działania”

**Pomiar czcionką systemową** (DejaVu Sans, 100%):
- „Czas działania” 113 px; z odstępami 2 × 6 px i strzałką kolumna 120 DIP ucinała nagłówek;
- ucinały się też „Niedostępne” (100 px) i „Oczekujący (zdarzenia)” (185 px).

**Nowe szerokości:** Nazwa 205, PID 50, Stan 200, Procesor 90, Pamięć 112, Dysk 112, Sieć 112, Czas działania 140, razem 1021 DIP.

**Standardowe okno:** 1080×600. `Okno` przycina je do obszaru roboczego mniejszych ekranów.

**Inna czcionka lub węższe okno:** przy prawdziwym BSF kolumny zajmują 1036 px przy 1062 px tabeli. Szerokość według nagłówków nie pozwala ich obciąć, a do ostatniej kolumny prowadzi przewijanie poziome.
