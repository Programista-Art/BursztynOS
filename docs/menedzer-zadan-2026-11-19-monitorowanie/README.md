# Sesja natywna QEMU – monitorowanie urządzeń (2026-11-19)

## Przebieg sesji

- **Liczba sesji:** jedna, kontrolowana.
- **Środowisko:** TCG, UEFI (OVMF), 1280×720, wskaźnik VirtIO tablet, 1 vCPU, 2 GiB RAM.
- **ISO:** diagnostyczne (`BURSZTYNDIAGGUIRUNTIME=1`), skopiowane do `.bsf-test/var-wyd-diag`.
- **Urządzenia:**
  - **dysk 1:** Live ISO jako VirtIO-BLK `readonly=on`;
  - **dysk 0:** jednorazowy, pusty obraz 64 MiB jako SATA/AHCI z `snapshot=on` (`guest_session.py --extra-disk`), więc żaden zapis nie trafia do pliku;
  - **sieć:** VirtIO-Net (QEMU user) z lokalnym serwerem HTTP pod 10.0.2.100 (strona testowa 206 885 B);
  - **grafika:** VirtIO-GPU.

Puls hosta (`heartbeat.log`): **brak przerw > 1,5 s**. QEMU zakończyło się czysto. W logu szeregowym nie było
`[USER FAULT]`, `[PF]`, `[GP]`, `PANIC` ani `PERMANENT_FAILURE`.

## Co potwierdzono

| Sprawdzenie | Wynik |
|---|---|
| Wykrycie urządzeń | lista: Procesor, Pamięć, „Dysk 0 (SATA/AHCI)”, „Dysk 1 (VirtIO Block)”, „Ethernet virtio-net0”, „Grafika (VirtIO-GPU)” – dokładnie podłączone urządzenia (zrzut 02) |
| Dysk AHCI | model nie jest wymyślany: w Live system nie odczytał tego dysku – 0 B, 0 operacji (zrzut 03) |
| Dysk VirtIO (ISO) | 512 B / 1 operacja odczytu przy starcie, potem 0 B/s – Live działa w RAM (zrzut 04) |
| Grafika | „Obciążenie GPU: Brak licznika sprzętowego”, PCI 1AF4:1050, 1280×720×32, B8G8R8X8, kursor programowy, 2–6 klatek/s kompozytora (zrzut 05) |
| Ethernet | po dwóch wczytaniach strony: odebrano 421,1 KiB (296 pakietów), wysłano 18,3 KiB (300 pakietów), szczyt 123 KiB/s na wykresie, 0 błędów (zrzut 08) |
| Sieć procesu | podczas wczytania strony kolumna „Sieć” procesu roboczego Husarza (PID 5) = **199,7 KiB/s**; inne procesy 0 B/s (zrzut 09). Odczyt tabeli jądra (`peek RozliczanieIo::g_tabela`): PID 5 gen. 6 – po 2. wczytaniu 414 148 B odebranych / 212 B wysłanych, po 3. 621 222 B / 318 B (+207 074 B = strona + nagłówki HTTP) |
| Dysk procesu (logiczny) | Husarz (PID 4): „Pliki (logicznie): odczyt 2,0 MiB” (czcionki/zasoby przez wywołania plikowe), 0 B/s w spoczynku (zrzut 07) |
| Kolumny Dysk/Sieć, poziomy pasek | widoczne przy oknie 900 px (zrzut 01) |
| CPU, RAM | działają jak wcześniej |

## CPU Menedżera zadań i opóźnienie kursora (`[GUI PERF]`, fazy ~15 s)

| Faza | Menedżer [ms CPU/s] | PID 0 [ms/s] | Prezentacje kursora | Wejście→prezentacja śr. / maks. |
|---|---|---|---|---|
| A Procesy, spoczynek | 42 | 52 | – | – |
| B Procesy, ruch myszy | 72 | 84 | 263 | 6,0 / 155 ms |
| C Wydajność (Procesor), spoczynek | 53 | 61 | – | – |
| D Wydajność (Procesor), ruch myszy | 93 | 84 | 278 | 6,9 / 157 ms |
| E szybkie przełączanie kategorii (15 kliknięć) + ruch | 129 | 115 | 95 | 32,8 / 585 ms |
| F Wydajność, ruch myszy | 61 | 80 | 253 | 5,7 / 164 ms |

- **Kursor na kartach:** Procesy i Wydajność mają to samo średnie opóźnienie (6–7 ms). Maksimum ~155–164 ms występuje we wszystkich fazach, także na karcie Procesy, więc nie zależy od karty.
- **Brak powrotu zacinania:** średnie opóźnienie jest niższe niż w pomiarze z poprzedniej poprawki (8–33 ms).
- **Faza E:** każde kliknięcie przełącza kategorię i przerysowuje wykres oraz tabelę (host: ~2,3 ms; pod TCG kilkadziesiąt ms). Maksimum 585 ms to pojedynczy przypadek w trakcie kliknięć.
- **Inne obciążenie w sesji:** aplikacja „Diagnostyka systemu” (otwarta automatycznie przy starcie) zużywała 60–86 ms/s przez całą sesję.

## Czego nie zweryfikowano natywnie

- **Etykieta I/O w szczegółach procesu:** miała 40 DIP i natywnie ucinała drugą linię („Sieć…”). Po sesji powiększona do 52 DIP; poprawka jest pokryta testem hosta, nie sesją.
- **Tytuły wykresu dysku i sieci:** były ucięte; po sesji skrócone, legenda przeniesiona do tabeli szczegółów. Bez ponownej sesji.
- **E1000, wiele interfejsów, obciążony zapis dysku, sieć gniazdami BWS 79–83:** niesprawdzone (uprząż ma tylko VirtIO-Net; pokryte testami hosta).
- **Fizyczne odczyty dysku po starcie:** w Live system nie czyta dysków. Instalatora (który skanuje GPT) celowo nie uruchamiano.

Pliki: `perf-*.json` (agregaty `[GUI PERF]`), `faza.sh`, `heartbeat.log`, `serial.log.gz`, zrzuty 01–09.
