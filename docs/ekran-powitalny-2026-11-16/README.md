# Ekran powitalny: przeprojektowanie i naprawa (2026-11-16)

## Przyczyny problemów z ekranu referencyjnego

| Objaw | Przyczyna w kodzie sprzed zmiany |
|---|---|
| Za duże logo | `LOGO_PREFEROWANA=190` px przy 720p (26% wysokości), dodatkowo powiększane do wolnego miejsca. Maska 294×320 była zmniejszana interpolacją dwuliniową, która przy współczynniku >2 pomija próbki. |
| Rozstrzelony tytuł | `BURSZTYN OS` wersalikami z dodatkowym odstępem `pismo/3` po każdym znaku (`postep_rozstrzelony`). |
| Pikselowa typografia | Napisy rysowane bitmapowym fontem 16 px (`CzcionkaUI`) powiększanym do 212%. |
| Napisy naraz | Zegar animacji startował przy pierwszej klatce. Potem jądro synchronicznie wykonuje DHCP, przygotowanie PSF, instalację aplikacji i tapet (pod QEMU/TCG ok. 9–10 s), a kompozytor (pętla PID 0) jeszcze nie działa. Przy pierwszym odświeżeniu wszystkie etapy były już „zaległe” i pojawiały się w jednej klatce (przebieg A). |

## Rozwiązanie

* `jadro/gui/ekran_powitalny.h`: nowy układ (logo 96 px przy 720p, tytuł, separator, dwa podtytuły) liczony od linii bazowych, filtr obszarowy (box) bez FPU i stały układ dla wszystkich etapów.
* `jadro/gui/napisy_powitania_dane.h`: maski pokrycia zrasteryzowane z Noto Sans Regular (OpenType, SIL OFL 1.1, `czcionki/otf/`) rasteryzatorem BSF (`bsf/vendor/stb_truetype.h`) podczas budowania. Generator to `narzedzia/generuj_napisy_powitania.cpp`. Jądro nie parsuje TTF w Ring 0 i nie zależy od systemu plików.
* Zegar etapów startuje w pierwszym punkcie, w którym animację da się pokazać. Jest to punkt kontrolny startu (`powitanie_punkt_startu` w `kernel.cpp` i `tapety.cpp`) albo pierwszy takt kompozytora. Punkty kontrolne są nieblokujące: najwyżej jedna klatka, tylko przy zmianie obrazu, nie częściej niż co 100 ms, wyłącznie przed włączeniem wielozadaniowości.

Etapy od startu zegara etapów: logo od pierwszej klatki, tytuł 1,0–1,5 s, separator 2,0–2,4 s, „Polski system operacyjny” 3,0–3,5 s, „Niezależny system” 4,0–4,5 s. Zanikanie do pulpitu zaczyna się najwcześniej 1,5 s po pierwszej klatce i trwa 0,6 s. Nie czeka na koniec sekwencji.

## Dowody

* `host/`: podgląd z produkcyjnego renderera (`narzedzia/podglad_animacji_startowej.cpp`), etapy przy 1280×720, klatki końcowe i geometria dla 800×600 … 1920×1080@125%.
* `natywne/`: cztery sekwencyjne przebiegi QEMU (UEFI OVMF, virtio-vga 1280×720, TCG, `-smp 1`, 2 GiB) z `przechwyc_powitanie.py` (zrzut QMP co ok. 0,3 s, zapisywane tylko zmienione klatki):
  * **A**: kod z nowym wyglądem, zegar od pierwszej klatki. Przygaszone logo przez ok. 10 s, potem wszystkie napisy naraz. Potwierdza przyczynę.
  * **B**: zegar etapów od pierwszego taktu kompozytora. Logo w pełnej jasności, ale pulpit był gotowy ok. 1 s po starcie pętli PID 0, więc napisy nie zdążyły się pojawić. Jeden przestój hosta 5,7 s.
  * **C**: z punktami kontrolnymi w instalacji aplikacji, bez przestojów hosta. Logo 7,3 s, wejście tytułu 9,5 s, pełny tytuł 9,9 s. Potem przerwa do 16,3 s (weryfikacja/zapis tapet bez punktu kontrolnego), czyli separator i podtytuły w jednej klatce. Po nim zanikanie do pulpitu.
  * **D**: z punktami kontrolnymi także w pętlach tapet. Przestoje całej maszyny 3,4 s, 8,8 s i 3,9 s, więc taktowanie jest niewiarygodne. Testy natywne przerwano zgodnie z zasadami środowiska.

Niezweryfikowane natywnie: pełna, kolejna sekwencja z punktami kontrolnymi tapet bez przestojów hosta, BIOS/VESA, inne rozdzielczości natywne, sprzęt fizyczny.

## Poprawka: podtytuły pojawiające się naraz (sekwencjonowanie zależne od prezentacji)

**Przyczyna:** krycie każdego elementu było czystą funkcją upływu czasu (`przelicz(t)`), bez informacji zwrotnej o prezentacji. Każda przerwa w odświeżaniu dłuższa niż ok. 1 s przekraczała progi kilku etapów naraz. Taką przerwą jest np. ok. 6 s weryfikacji tapet albo przestój hosta. Oba podtytuły trafiały wtedy do jednej, pierwszej po przerwie klatki.

**Rozwiązanie** (`Powitanie::Sekwencja` w `jadro/gui/ekran_powitalny.h`):
* jawne etapy: `ETAP_LOGO`, `ETAP_TYTUL`, `ETAP_SEPARATOR`, `ETAP_LINIA1`, `ETAP_LINIA2`;
* dla każdego etapu `pokazany_od` (pierwsza udana prezentacja) i `pelny_od` (pierwsza prezentacja w pełnym kryciu). Ustawia je wyłącznie `potwierdz()`, wołane z `powitanie_publikacja()` po udanej publikacji, z maskami etapów klatki faktycznie wyrenderowanej;
* przejście `etap → etap+1` następuje, gdy poprzedni etap był pokazany co najmniej 1 s temu i był w pełni widoczny co najmniej przez 1 s minus czas jego wejścia (0,5 s dla tekstu, 0,6 s dla separatora). Przypada najwyżej jedno przejście na próbkę. Nie zachodzi w trakcie zanikania do pulpitu;
* fade-in liczy się od pierwszej prezentacji etapu. Etap niepotwierdzony stoi na pierwszym kroku wejścia, a kompozytor i punkty kontrolne ponawiają klatkę do skutku.

Dowody: `host/sekwencja-po-przestoju.png` (scenariusz: 8 s bez prezentacji po logo, potem etapy co 1 s) oraz `natywne/przebieg-e/`. Przebieg E to jeden przebieg QEMU UEFI 1280×720 z dwoma krótkimi przestojami hosta (2,7 s i 3,1 s). Logo 7,96 s, tytuł 8,96 s, separator 10,12 s, „Polski system operacyjny” pierwszy raz w klatce 16,51 s (pełny 17,20 s), „Niezależny system” pierwszy raz w klatce 17,85 s, potem zanikanie do pulpitu od 19,1 s.
