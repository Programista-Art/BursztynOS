# Akceleracja 2D i skalowanie (BWS 98/99) — 2026-11-11

Zakres: warstwa skalowania w jądrze, nowe wywołania BWS, skalowanie powierzchni
w kompozytorze, API Ring 3 i użycie w BursztynUI. To etap 0 planu portu
Chromium: bez tego etapu backend Ozone nie ma na czym prezentować klatek.

## 1. Problem

System nie miał żadnej ścieżki skalowania poza najbliższym sąsiadem w Ring 3
(`probka_obrazu()` w `obrazy/skalowanie.h`) i mnożnikiem metryk UI ograniczonym
do 100–200% (`jadro/gui/ui_skala.h`). Konsekwencje:

- przeglądarka rysująca treść w rozdzielczości źródła musiała skalować
  w Ring 3 i wysyłać gotowy obraz w docelowym rozmiarze — pełny koszt
  arytmetyki per piksel płacony dwa razy (raz przy rysowaniu, raz przy kopii),
- brak zachowania proporcji (letterbox/pillarbox) w kompozytorze,
- brak jakiegokolwiek filtra lepszego niż nearest, więc każda zmiana skali
  (DPI, powiększenie strony, zoom obrazu) dawała aliasing,
- brak sposobu, żeby aplikacja zadeklarowała „mój bufor ma inny rozmiar niż
  okno”, więc każda aplikacja implementowała własne skalowanie od nowa.

## 2. Co zostało dodane

### 2.1 `jadro/gui/skalowanie.h` — jedno źródło matematyki

Czyste, bezalokacyjne funkcje w stałym punkcie 16.16 na `int64_t`. Brak
float/double jest wymogiem, nie preferencją: jądro buduje się z
`-mno-sse -mno-sse2 -mgeneral-regs-only`.

| Funkcja | Rola |
|---|---|
| `skalowanie_pozycja` | środek piksela docelowego w układzie źródła (16.16) |
| `skalowanie_indeks_nearest` | najbliższy piksel, zawsze w `[0, src-1]` |
| `skalowanie_os` | rozkład bilinear na osi: `i0`, `i1`, waga 0..65535 |
| `skalowanie_piksel_nearest/bilinear/box` | próbkowanie jednego piksela |
| `skalowanie_filtr_dla` | polityka: ≥2× w górę → nearest, ≥2× w dół → box, reszta → bilinear |
| `skalowanie_wiersz`, `skalowanie_wiersz_wycinek` | wiersz / przycięty wycinek wiersza |
| `skalowanie_kopia_1do1` | ścieżka 1:1 przez istniejącą kopię blokową (`graphics_copy.h`) |
| `skalowanie_dopasuj` | geometria `rozciągnij` / `dopasuj` / `wypełnij` |

Semantyka koloru jest identyczna ze kompozytorem: `0x00000000` = puste,
`0x01000000` = widoczna czerń, `0x00RRGGBB` = nieprzezroczysty,
`0xAARRGGBB` = częściowe pokrycie. Bilinear liczy w przestrzeni
premultiplikowanej, więc krawędzie kształtu nie ściągają koloru z
przezroczystego tła.

### 2.2 BWS 98 — skalowany blit

`BWS_GUI_BLIT_SKALOWANY = 98`, deskryptor `BwsBlitSkalowany` (56 B,
`static_assert` na rozmiar):

```
wersja, flagi, filtr, tryb,
dst_x, dst_y, dst_w, dst_h,
src_w, src_h, zarezerwowane, zarezerwowane2,
piksele (adres Ring 3)
```

Flagi: `PRZEZROCZYSTE_ZERO`, `ZAMIEN_REGION`, `ALFA_SOURCE_OVER`,
`ZACHOWAJ_ASPECT`. Walidacja w `jadro/bws/bws_nowoczesne.cpp`:

- `PRAWO_GUI`,
- wersja ABI, wszystkie pola zarezerwowane = 0,
- brak nieznanych bitów flag,
- `src_w/src_h` w `1..8192`, `dst_w/dst_h` w `1..8192`,
- znany filtr i tryb dopasowania,
- `src_w*src_h ≤ 4 M` pikseli i `≤ 16 MiB` bajtów (te same limity co BWS57),
- `czy_bezpieczny_zakres_uzytkownika_do_odczytu` + strona-po-stronie
  `bws_vmm_sprawdz_zakres_uzytkownika` na całym buforze źródłowym.

Sam zapis robi `gui_blit_skalowany_region()` w
`sterowniki/grafika/grafika.cpp`: `OknoUzytkownika` na czas całej pętli
(SMAP pozostaje aktywny dla reszty jądra), przycięcie do warstwy wywołującego,
jeden `zapamietaj_dirty_rysowania()` na prostokąt docelowy. Piksel o
częściowym pokryciu jest mieszany z tym, co już jest w warstwie — kompozytor
obcina górny bajt, więc bez tego krawędzie skalowanego tekstu dostawałyby
jasną obwódkę.

### 2.3 BWS99 — skala powierzchni warstwy

`BWS_GUI_SKALA_WARSTWY = 99`, argument 1 = `(src_w << 32) | src_h`,
argument 2 = tryb (`0` rozciągnij, `1` dopasuj, `2` wypełnij).

Kompozytor trzyma deklarację w prywatnej tablicy `skala_warstwy[pid]` w
`jadro/gui/skladacz_obrazu.cpp`, więc **ABI `warstwa_obrazu` pozostaje
zamrożone**. Deklaracja jest przyjmowana tylko wtedy, gdy opisuje dokładnie
istniejący bufor (`src_w*src_h == zaalokowane piksele`) — to wyklucza
czytanie poza alokację po nieudanej zmianie geometrii okna. Przy niezgodności
kompozytor czyści deklarację i składa warstwę starą ścieżką 1:1.

Deklaracja jest zerowana w `usun_warstwe()` (recykling PID).

### 2.4 BWS57 tryb 5 — batch skalowanych regionów

Tryb 4 (istniejący) przyjmuje `BwsGuiRegion` (24 B) i zastępuje regiony bez
skalowania. Tryb 5 dodaje `BwsGuiBatchSkalowany` (24 B) + tablicę
`BwsGuiRegionSkalowany` (40 B, do 64 regionów) z `src_w/src_h` i flagami per
region. Kontrakt bezpieczeństwa jest identyczny: **najpierw walidacja
wszystkich regionów, potem pierwszy zapis** — odrzucony batch nie zostawia
połowicznie zaktualizowanej powierzchni.

### 2.5 Kompozytor — skalowanie bez zmiany ABI

`zloz_warstwe_skalowana()` w `jadro/gui/skladacz_obrazu.cpp` próbkuje bufor
warstwy w cały prostokąt okna, z:

- `skalowanie_filtr_dla()` jako polityką filtra,
- letterboxem/pillarboxem dla trybów `dopasuj`/`wypełnij`,
- pominięciem pikseli pustych (dokładne zero),
- zachowaniem `WindowShape` (zaokrąglenia, pas dekoracji) i
  `motyw_pulpit_skladaj()` dla pulpitu.

### 2.6 Ring 3

`jadro/gui/bursztyn_gui.h` / `.cpp`:

```
bool gui_blit_skalowany(dst_x, dst_y, dst_w, dst_h, piksele, src_w, src_h,
                        filtr, flagi, tryb);
bool gui_blit_skalowany_regionu(piksele, src_w, src_h, dst_x, dst_y, dst_w, dst_h, filtr);
bool gui_ustaw_skale_warstwy(src_w, src_h, tryb);
```

`bursztyn_ui/skalowanie.h` daje te same prymitywy jako inline dla aplikacji —
biblioteki Ring 3 nie mogą includować nagłówków jądra, ale polityka filtra
musi być identyczna, żeby aplikacja i kompozytor nie podejmowały różnych
decyzji o jakości.

`bursztyn_ui/obraz.h` (kontrolka `Obraz`) używa teraz bilinearu w środku
obrazu i nearest na krawędziach — kontrakt „zero = przezroczyste, nigdy
mieszania koloru z tłem” pozostaje nietknięty, w tym samym buforze wiersza
bez alokacji.

## 3. Weryfikacja

### 3.1 Test hosta (gate)

`make test-blit-skalowanie-host` — `testy/test_blit_skalowanie_host.cpp`,
ASan + UBSan, **105 168 asercji, PASS**. Zakres:

- niezależny oracle zmiennoprzecinkowy dla nearest i bilinear
  (tolerancja 1 jednostka na kanał) na siatce 1..20 px,
- mapowanie `1:1` bit-identyczne dla wszystkich wymiarów 1..40,
- wszystkie indeksy w zakresie `[0, src-1]` dla każdej kombinacji `src/dst`,
- box: jednorodny kolor pozostaje jednorodny, mieszanka przy kolumnach
  naprzemiennych, kontrolowana degradacja do nearest,
- piksele puste, marker czerni `0x01000000`, alfa 128 zachowana,
- kanarki po obu stronach wiersza dla wszystkich filtrów, `dst_w == 0`
  i puste źródło nic nie zapisują,
- geometria `dopasuj`/`wypełnij`/`rozciągnij`, w tym zdegenerowane wejście,
- polityka `skalowanie_filtr_dla`.

### 3.2 Test nadużyć w QEMU (gate)

`make test-bws-abuse-qemu` rozszerzone o kategorię 4b: BWS98 z nieznanym
filtrem, nieznanym trybem, zerową i nadmierną szerokością źródła,
niezmapowanym buforem, nieznaną flagą i obciętym deskryptorem; BWS99 ze skalą
większą niż bufor warstwy, jednym zerowym wymiarem i nieznanym trybem.
**PASS** (wszystkie 6 kategorii).

Uwaga: test Bug #1 używał numeru 99 jako „nieznanego”. Numer 99 jest teraz
zarezerwowany, więc regresja sprawdza numer spoza tablicy
(`BWS_PIERWSZY_WOLNY + 1000`), zachowując pierwotny cel testu.

### 3.3 Regresje hosta

PASS: `test-stack-protector-host`, `test-bur-v2-aslr-host`, `test-virtio-pci`,
`test-motywy-unit`, `test-wm-click-routing`, `test-wm-dekoracje`,
`test-compositor-cursor`, `test-shell-host`, `test-desktop-files-host`,
`test-bursztyn-ui-polecenia`, `test-cursor-host`, `test-cursor-hover`,
`test-desktop-capacity-host`, `test-husarz-engine-host`.

`test-explorer-host` FAIL — **regresja zastana**, potwierdzona na czystym
`HEAD` w osobnym worktree (ten sam `FAIL ... :60: opaque`). Nie jest
skutkiem tej zmiany.

### 3.4 Regresje QEMU

Porównanie z czystym `HEAD` (worktree, ta sama maszyna, te same parametry):

| Cel | Baseline HEAD | Ta zmiana |
|---|---|---|
| `test-guard-page` | PASS | PASS |
| `test-bws-abuse-qemu` | PASS | PASS (po dodaniu BWS98/99) |
| `test-virtio-gpu-qemu` | FAIL (31 linii FAIL) | FAIL (31 linii FAIL po powtórzeniu) |
| `test-normal-boot` | FAIL (`Kursor widoczny w (500, 300)`) | FAIL (ten sam) |

Zbiory porażek `test-virtio-gpu-qemu` są identyczne po powtórzeniu
(`comm` pusty poza jednym wpisem, który zniknął w drugim przebiegu —
flaky `Kompozycja i rendering tapety/pulpitu`, zależny od tego, czy
`[TAPETA] Tapeta gotowa.` zdąży pojawić się przed końcem okna obserwacji).

### 3.5 Natywny QEMU na Windows

Zgodnie z regułą projektu (`qemu-system-x86_64.exe` natywnie, nigdy przez
WSL): boot z `virtio-rng-pci` + AHCI na `wirtualny_dysk.img`, `-display none`,
serial do pliku. Wynik: `ready=True wallpaper=True desktop=True`,
`System operacyjny gotowy!`, `Pulpit gotowy`, brak paniki i brak `BUR FAIL`.

Bez `virtio-rng-pci` loader odmawia startu Menedżera Okien
(`bur-v2-secure-entropy-or-layout-unavailable`) — to zachowanie fail-closed,
nie regresja.

### 3.6 Koszt

Mikro-benchmark `make bench-blit-skalowanie` (256×256 → 256/512/128 px,
40 powtórzeń, `-O2`, x86-64 host):

| Przypadek | ns / piksel docelowy | × nearest |
|---|---|---|
| 1:1 nearest (ścieżka kopii) | 3,11 | 1,00 |
| 1:1 bilinear | 14,88 | 4,76 |
| 2× nearest | 3,16 | 1,01 |
| 2× bilinear | 14,88 | 4,76 |
| 0,5× box | 18,27 | 5,84 |
| 0,5× bilinear | 14,81 | 4,74 |
| 0,5× nearest | 3,09 | 0,99 |

To uzasadnia politykę `skalowanie_filtr_dla()`: przy powiększeniu ≥2×
bilinear kosztuje 4,7× więcej, a wizualnie nie różni się od interpolacji
całkowitej; przy zmniejszeniu ≥2× box jest jedynym filtrem bez aliasingu
i kosztuje 5,8× nearest, ale to wciąż mniej niż pełne przerysowanie treści.

Benchmark end-to-end (`narzedzia/graphics_perf_qemu.py`, 1280×720, 24 dragi),
baseline vs zmiana:

| Metryka | Baseline | Zmiana |
|---|---|---|
| frames | 26 | 26 |
| full_frames | 0 | 0 |
| rects | 26 | 26 |
| compose_ns | 310,7 ms | 301,7 ms |
| present_ns | 24,2 ms | 25,2 ms |
| gpu_wait_ns | 24,7 ms | 25,8 ms |
| `frame_total_ns.mean` | 12,88 ms | 12,57 ms |
| host_fps (drag workload) | 9,63 | 9,51 |

Scenariusz drag nie ćwiczy nowej ścieżki (pulpit nie deklaruje skali), więc
te liczby dokumentują **brak regresji**, a nie zysk. Zysk pojawi się, gdy
aplikacja faktycznie zadeklaruje skalę (Husarz z zoomem, Chromium z DPI).

## 4. Dług techniczny

1. **Skalowanie nadal jest software'owe po stronie gościa.** VirtIO-GPU 2D nie
   ma operacji skalowania — GPU dostaje gotowe piksele przez
   `TRANSFER_TO_HOST_2D` + `RESOURCE_FLUSH`. „Akceleracja 2D” w tym etapie
   oznacza: arytmetyka dzieje się raz, w jądrze, wprost w docelową powierzchnię,
   z polityką filtra i bez buforów pośrednich; nie oznacza operacji GPU.
2. **Brak skalowania w backendzie sprzętowym.** `uefi_gop.cpp` i
   `bochs_vbe.cpp` nie mają ścieżki skalowania; kompozytor skaluje do
   backbuffera, więc jest to poprawne, ale nie wykorzystuje potencjału
   sprzętowego skalera (GOP `BLT`).
3. **Bilinear jest ~4,7× kosztowniejszy od nearest.** Dla pełnoekranowego
   skalowania 1280×720 → 2560×1440 to ~23 M pikseli × 15 ns ≈ 0,35 s na
   klatkę w TCG. Dlatego polityka wybiera nearest dla ≥2× — bez tego
   powiększenie na dużym monitorze byłoby nieużywalne.
4. **`SKALACZ_MAKS_WARSTW` = 16 warstw** nadal wynika z `PZB_MAKS_PROCESOW`.
   Chromium z wieloma oknami lub wieloma warstwami na proces tego limitu
   nie przetrwa — to punkt 4 etapu 1 planu (wątki/procesy).
5. **Brak testu pikselowego QEMU dla BWS98.** Test nadużyć pokrywa odrzucanie,
   test hosta pokrywa matematykę; brakuje testu „wyślij obraz, sprawdź
   piksele w zrzucie ekranu”. Do dodania razem z pierwszym konsumentem
   (Husarz z zoomem).

## 5. Gate'y do uruchomienia przed commitem

```
make test-blit-skalowanie-host
make bench-blit-skalowanie        # raport, nie gate
make test-smoke
make test-bws-abuse-qemu
make test-guard-page
make test-virtio-gpu-qemu         # baseline FAIL, patrz 3.4
make test-normal-boot             # baseline FAIL, patrz 3.4
```
