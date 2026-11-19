# Sesja natywna – stabilizacja Kalendarza (2026-11-19)

## Warunki sesji

- **Sesja:** jedna, krótka.
- **Obraz:** końcowe produkcyjne ISO (`__kernel_end` 0xBCD000, bez `[GUI PERF]`).
- **Środowisko:** TCG, UEFI, 1280×720.
- **Stan hosta:** puls bez przerw > 1,5 s, QEMU zakończone czysto.
- **Log szeregowy:** brak `[USER FAULT]`, `[PF]`, `[GP]`, `PANIC` i `PERMANENT_FAILURE`.

## Wyniki

| Sprawdzenie | Wynik | Zrzut |
|---|---|---|
| Zegar przy otwartym panelu | 21:15:27 → 21:15:33 → (nawigacja) 21:15:36 → 21:15:41 – zegar chodzi | 01 |
| Przyciski panelu | `<< <` i `> >>` rozdzielone; `>>` przechodzi do listopada 2027 | 01 |
| Kontrast wybranego dnia | cyfra 19 jasna na tle zaznaczenia | 01 |
| Aplikacja | jedna nawigacja w nagłówku (`<< < Dzisiaj > >>`), pasek komponentu ukryty | 02 |
| Wybór 30.11 i `>` | grudzień 2026, szara komórka 30 w ramce, opis „· poza widocznym miesiącem”, „za 11 dni” | 03 |
| `>>`, „Dzisiaj” | grudzień 2027, powrót do 19.11.2026 | 04 |
| Minimalna szerokość | okno zatrzymane na 640 px, tytuł nie zachodzi na przyciski | 05 |

## Niezweryfikowane natywnie

- **Menu przepełnienia z zegarem:** wymaga wielu okien; w sesji nie było przepełnienia. Pokryte testem hosta.
- **Odrzucenie nieznanego `arg2` i bufor na granicy stron w BWS 9:** brak aplikacji testowej w obrazie.
- **Motyw jasny, skale 150/200 %, klawiatura:** tylko testy hosta.
