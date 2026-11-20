# Sesja natywna – Kalendarz (2026-11-19)

## Warunki sesji

- **Sesja:** jedna, ok. 3 minuty pracy z pulpitem.
- **Obraz:** produkcyjne ISO z `feature/kalendarz` (`__kernel_end` 0xBCC000).
- **Środowisko:** TCG, UEFI, 1280×720, VirtIO tablet.
- **Stan hosta:** puls (`heartbeat.log`) bez przerw > 1,5 s. QEMU zakończone czysto.
- **Log szeregowy:** brak `[USER FAULT]`, `[PF]`, `[GP]`, `PANIC` i `PERMANENT_FAILURE`.

## Wyniki

| Sprawdzenie | Wynik | Zrzut |
|---|---|---|
| Zegar paska | 20:23:54 w gościu przy 20:23 CET na hoście (19.11.2026) | 01 |
| Klik zegara | panel nad paskiem, przy zegarze, w granicach ekranu; zegar podświetlony; nagłówek „czwartek, 19 listopada 2026”; dzień 19 wyróżniony | 01 |
| Siatka | listopad 2026 od niedzieli 1 (ostatnia kolumna), poniedziałek jako pierwsza kolumna, polskie skróty dni | 01 |
| Nawigacja `>` ×2 | styczeń 2027, 1.01 = piątek | 02 |
| Escape | panel zamknięty | 03 |
| Ponowne otwarcie | znowu listopad 2026 | 03 |
| Klik poza panelem | panel zamknięty | 03 |
| Menu Start, „kalend” | wpis „Kalendarz” z ikoną | 04 |
| Aplikacja | okno „Kalendarz”, „Listopad 2026”, dzisiaj wybrane, opis „Dzień roku: 323 · Tydzień ISO: 47 · dzisiaj”; ikona kalendarza na pasku | 05 |
| Wybór 26.11 + `>` | „czwartek, 26 listopada 2026”, „Tydzień ISO: 48 · za 7 dni”; widok grudzień 2026 | 06 |
| „Dzisiaj” | powrót do 19 listopada | 07 |
| Minimalizacja | okno schowane, ikona na pasku zostaje | 07 |
| Ponowne uruchomienie z menu Start | aktywacja istniejącego okna (jedna ikona na pasku) | 07 |
| Zmiana rozmiaru (przeciągnięcie krawędzi) | siatka dopasowana | 08 |
| Panel przy otwartej aplikacji | działa | 09 |

## Usterki wykryte w sesji i poprawione po niej (testy hosta, bez ponownej sesji)

1. **Zegar przy otwartym panelu stał** (20:23:54 na zrzutach 01 i 02). Panel pochłaniał zdarzenia TIMER, a pętla WM pomijała wtedy aktualizację zegara. Teraz TIMER zawsze przechodzi dalej (test w `test-wm-click-routing`).
2. **Przyciski „<<” i „<” zlewały się w „<<<”.** Komponent miał stałe szerokości 20 i 18 DIP, a czcionka systemowa jest szersza. Teraz szerokość wynika z tekstu plus odstęp, z przerwą 3 DIP między przyciskami (test w `test-kalendarz-host`).
3. **Słaby kontrast cyfry wybranego dnia w motywie ciemnym.** Kolor tekstu wybranej komórki pochodzi teraz z `motyw_kolor_tekstu_zaznaczenia()`.
4. **Przy 440 px tytuł wchodził pod przycisk „<”.** Minimalna szerokość okna wynosi teraz 580 px; czcionką BSF „Październik 2026” ma 272 px w kolumnie 342 px.

## Niezweryfikowane natywnie

- Cztery poprawki powyżej.
- Motyw jasny (sesja w motywie ciemnym).
- Skale 150 i 200 %.
- Zmiana daty o północy przy otwartym oknie.
- Brak daty systemowej.
- Klawiatura w panelu i w aplikacji (testy hosta).
- Opóźnienie kursora: ISO produkcyjne nie ma liczników `[GUI PERF]`. Ruch myszy był płynny wzrokowo – to obserwacja, nie pomiar.
