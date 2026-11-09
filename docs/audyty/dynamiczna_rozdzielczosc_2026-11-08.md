# Bursztyn OS — dynamiczna rozdzielczość zależna od medium

Data: 2026-11-08
Repozytorium: `C:\Users\UNITRONIX\Documents\GitHub\BursztynOS`

## Problem zgłoszony przez użytkownika

Okno QEMU ustawione na 1920×1080 było za duże przy skalowaniu DPI 150%.
Ręczne zmniejszenie okna psuło jakość interfejsu, bo system renderował
w jednej rozdzielczości, a Windows skalował wynik.

Wniosek: system narzucał własną rozdzielczość zamiast dostosować się do
medium, które go wyświetla.

## Rozwiązanie — dwa filary

### Filar 1: wybór trybu startowego (priorytet host > bootloader > domyślny)

`sterowniki/virtio/virtio_gpu.cpp` — sekcja wyboru trybu.

Wcześniej przy `BURSZTYN_DEFAULT_DISPLAY_720P=1` tryb z bootloadera był
**ignorowany**, a przy buildzie domyślnym nadpisywał tryb hosta. Oba
warianty łamały zasadę „skaluj się do medium".

Nowa kolejność zaufania:
1. `GET_DISPLAYINFO` — realny rozmiar scanoutu (okno QEMU / monitor VM).
2. Tryb z bootloadera (GRUB `gfxpayload`) — fallback.
3. Profil domyślny (`BURSZTYN_DEFAULT_DISPLAY_720P`).

Walidacja `is_valid_dim`: 640..4096 px, ≤64 MiB na bufor.

Log diagnostyczny: `[VIRTIO-GPU] mode source=host-display|bootloader|default`.

### Filar 2: reakcja na zmianę rozmiaru w trakcie pracy

Nowe funkcje:
- `virtio_gpu_pobierz_geometrie_hosta()` — odpytuje `GET_DISPLAYINFO`
  (`sterowniki/virtio/virtio_gpu.cpp`).
- `ekran_zastosuj_tryb()` — publiczny wrapper na wewnętrzną transakcję
  `zastosuj()` (`jadro/gui/ekran.cpp`, deklaracja w `ekran.h`).
- `sprawdz_zmiane_geometrii_medium()` — odpytuje hosta najwyżej raz na
  500 ms, z histerezą 32 px (`jadro/gui/skladacz_obrazu.cpp`).
- `skladacz_obrazu_wykonaj_zmiane_geometrii()` — realizuje odroczone
  zadanie poza blokadą ekranu.

## Dwa błędy znalezione podczas implementacji

### Błąd 1: brak histerezy → pętla sprzężenia zwrotnego

`GET_DISPLAYINFO` zwraca rozmiar scanoutu, który chwilowo obejmuje też
ramkę okna hosta (np. 1054 zamiast 1024). Bez progu kompozytor
przełączałby tryb tam i z powrotem, realokując bufor ramki za każdym razem.

**Poprawka:** ignorujemy różnice < 32 px w obu wymiarach.

### Błąd 2: zakleszczenie na spinlocku ekranu (KRYTYCZNY)

Pierwsza wersja wołała `ekran_zastosuj_tryb()` bezpośrednio z
`skladacz_obrazu_obsluz_dirty()`. Ta funkcja jest wykonywana pod
`zablokuj_ekran()`, a transakcja zmiany trybu **sama bierze tę blokadę**.
`zablokuj_ekran()` to spinlock bez reentrancji → natychmiastowe
zakleszczenie całego systemu.

Objaw: log urywał się na `[GEOMETRIA] Medium zmienilo rozmiar ...`,
CPU QEMU spadało do zera, system stał.

**Poprawka:** dwufazowe odroczenie.
- `sprawdz_zmiane_geometrii_medium()` tylko zapisuje żądanie
  (`g_geometria_oczekuje`, atomowo).
- `skladacz_obrazu_wykonaj_zmiane_geometrii()` realizuje je z pętli idle
  (`jadro/rdzen/kernel.cpp`), **po** `odblokuj_ekran()`.

## Weryfikacja

| Test | Wynik |
|---|---|
| Boot z `virtio-vga,xres=1024,yres=768` | `mode source=host-display`, `selected initial mode=1024x768` |
| Pulpit gotowy | TAK, brak BSOD |
| Maksymalizacja okna (1024×768 → 2293×889) | `Tryb przelaczony`, CPU pracuje, brak zawieszenia |
| Przywrócenie rozmiaru (2293×889 → 1024×768) | `Tryb przelaczony`, CPU pracuje |
| Zbędne przełączenia przy starcie | brak (histereza działa) |

## Wpływ na jakość obrazu (strona hosta)

Przy DPI 150% GTK tworzył okno 853×555 dla gościa 1280×800 — czyli
skalował w dół, stąd rozmycie. SDL przy DPI 96 daje klienta 1054×768
dla gościa 1024×768, czyli piksel 1:1.

**Zalecenie dla testów na tym stanowisku:** używać `-display sdl`, nie
`gtk`, dopóki gospodarz ma skalowanie DPI > 100%. To nie jest wada
systemu — to różnica w sposobie, w jaki backendy QEMU traktują DPI.

## Zmienione pliki

```
sterowniki/virtio/virtio_gpu.cpp   priorytet host>bootloader, pobierz_geometrie_hosta
sterowniki/virtio/virtio_gpu.h     deklaracja nowej funkcji
jadro/gui/ekran.cpp                ekran_zastosuj_tryb (wrapper)
jadro/gui/ekran.h                  deklaracja wrappera
jadro/gui/skladacz_obrazu.cpp      odpytywanie, histereza, odroczenie
jadro/gui/skladacz_obrazu.h        deklaracja wykonaj_zmiane_geometrii
jadro/rdzen/kernel.cpp             wywolanie w petli idle
Makefile                           gfxpayload=keep (8 miejsc)
```

## Pozostały dług

1. **Pola `eventq` w `virtio_gpu.h` są nieużywane.** QEMU z `virtio-vga`
   oferuje tylko 2 kolejki, więc kolejka zdarzeń (indeks 2) nie istnieje.
   Odpytywanie `GET_DISPLAYINFO` to obejście, ale działa na każdym
   urządzeniu. Martwe pola można usunąć.
2. **`gfxpayload=keep` nie pomogło** — OVMF i tak narzuca własny tryb
   `ConOut`. Filar 1 (priorytet hosta) rozwiązuje problem skuteczniej,
   więc `keep` zostaje jako nieszkodliwy dodatek.
3. **Zmiana trybu przy starcie nadal używa trybu bootloadera jako
   fallbacku** — na sprzęcie bez virtio-gpu (samo VESA/GOP) system
   przyjmie tryb firmware. To poprawne zachowanie.
4. **Histereza 32 px jest stała.** Przy bardzo małych oknach (640×480)
   to 5% szerokości. Można by ją uzależnić od rozmiaru, ale nie ma to
   praktycznego znaczenia.

## Zasada do zapamiętania

**Nigdy nie wołaj `ekran_zastosuj_tryb()` ani `grafika_zmien_sprzet()`
pod `zablokuj_ekran()`.** Spinlock nie ma reentrancji. Zmiany trybu
realizuj z pętli idle przez `skladacz_obrazu_wykonaj_zmiane_geometrii()`.
