# Karta Wydajność a zacinanie kursora — pomiary (2026-11-19)

Jedna sesja QEMU (TCG, UEFI GOP + VirtIO-GPU, Live ISO tylko do odczytu), build z `BURSZTYNDIAGGUIRUNTIME=1`
zawierający poprawkę. Fazy po ~15 s (`faza.sh`, zagregowane linie `[GUI PERF]` przez `guest_session perf`).

| Faza | CPU Menedżera | CPU PID 0 | prezentacje kursora | wejście→prezentacja śr./maks. |
|---|---|---|---|---|
| A Procesy, spoczynek | 53 ms/s | 38 ms/s | — | — |
| B Procesy + ruch myszy (328 ruchów) | 58 ms/s | 70 ms/s | 334 | 30,5 / 109 ms |
| C Wydajność, spoczynek (opisy niewidoczne*) | 33 ms/s | 22 ms/s | — | — |
| D Wydajność + ruch (opisy niewidoczne*) | 47 ms/s | 55 ms/s | 335 | 8,0 / 36 ms |
| E Wydajność z opisami, spoczynek | 44 ms/s | 23 ms/s | — | — |
| F Wydajność z opisami + ruch (331 ruchów) | 60 ms/s | 51 ms/s | 324 | 33,3 / 94 ms |

\* istniejący błąd układu: opisy pod wykresami miały wysokość pustego tekstu do pierwszej zmiany rozmiaru okna
(zrzut 01); poprawiony po sesji (rozciągnięcie na wiersz siatki), natywnie niezweryfikowany. E/F zmierzono po
wymuszeniu układu (maksymalizacja i przywrócenie, zrzut 02).

Oczekiwanie na blokadę ekranu: 0 we wszystkich fazach. Kursor w tej konfiguracji jest programowy (overlay
kompozytora w PID 0), nie sprzętowy plane. Średnia wejście→prezentacja = suma opóźnień / liczba klatek (przybliżenie).

Przed poprawką (natywnie, stary kod): ten sam widok Wydajność — CPU systemu 94,6 % i ciągłe 60–95 % na wykresie
(`docs/menedzer-zadan-2026-11-18/natywne/13-wydajnosc.png`), zgłoszenie: 67,6 %. Po poprawce: 7,5–9,4 % (zrzut 02).
Liczniki `[GUI PERF]` dla starego kodu nie były zbierane (jedna sesja).
