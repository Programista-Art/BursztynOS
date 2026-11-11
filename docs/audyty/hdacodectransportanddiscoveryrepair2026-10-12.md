# Naprawa transportu poleceń i odkrywania kodeków Intel High Definition Audio (HDA)

Data: 2026-10-12
Dotyczy: Sterownik `sterowniki/dzwiek/hda.cpp`, diagnostyka jądra `sterowniki/dzwiek/hda_diag.cpp`, format ABI `sterowniki/dzwiek/hda_diag_format.h`, aplikacja `programy/test_dzwieku/test_dzwieku.cpp`, testy hosta `narzedzia/test_hda_diag.cpp`, `narzedzia/test_dzwieku_hda_bws.cpp`.

---

## 1. Wstęp i streszczenie problemu

W testach aplikacji „Test dźwięku” na maszynie wirtualnej QEMU pod kontrolą Windows (z kontrolerem `intel-hda` i urządzeniem `hda-output`), GUI uruchamiało się poprawnie, raport `hda.txt` był zapisywany na systemie plików, a silnik strumieniowania DMA działał (stan `RUN`, `LPIB > 0`). Jednakże sekcja diagnostyczna zgłaszała:
```text
STATESTS raw 0x0000
Codecs 0
Codec address 0: nie odpowiada
Brak wyjścia Głośniki
```
Jednocześnie tradycyjna ścieżka odtwarzania tonu testowego (`hda_test_ton`) działała, ponieważ opierała się na sztywno wpisanych stałych QEMU (konwerter DAC NID 2, pin Line Out NID 3), podczas gdy nowo wprowadzona procedura generycznego skanowania kodeków i topologii zgłaszała całkowity brak odpowiedzi ze strony kodeka.

Celem prac było przeprowadzenie audytu transportu poleceń HDA, zidentyfikowanie przyczyn braku komunikacji, naprawienie rejestrów i protokołu Immediate Command, wdrożenie aktywnego ograniczonego sondowania kodeków, poprawienie numeracji parametrów w formacie ABI, rozbudowa klasyfikacji urządzeń wyjściowych (z uwzględnieniem Line Out) oraz zachowanie wszystkich dotychczasowych osiągnięć (budżet pliku BUR 119 040 B, BursztynUI, czcionka Unicode, zapis raportu, obsługa myszy VirtIO oraz xHCI).

---

## 2. Zidentyfikowane przyczyny źródłowe (Root Causes)

W toku audytu kodu sterownika i warstwy diagnostycznej zidentyfikowano cztery kluczowe usterki:

### 2.1. Skasowanie rejestru STATESTS podczas rozruchu kontrolera
Rejestr `STATESTS` (offset MMIO `0x0E`) to 16-bitowy rejestr statusu wykrycia kodeków na szynie HDA o semantyce **Write-1-to-Clear (W1C)**.
W procedurze startowej jądra sterownik odczytywał `STATESTS`, a następnie natychmiast wykonywał:
```cpp
mmio_zapisz16(HDA_REG_STATESTS, statests); // skasowanie bitów SDI!
```
W efekcie wszystkie bity informujące o podłączonych kodekach (`SDI0`..`SDI14`) były zerowane. Gdy w późniejszym etapie aplikacja „Test dźwięku” wywoływała syscall `BWS 89` w celu pobrania zrzutu diagnostycznego, sterownik odczytywał aktualny `STATESTS`, który wynosił `0x0000`, i na tej podstawie wnioskował `Codecs 0`.

### 2.2. Błędne identyfikatory parametrów w nagłówku ABI (`hda_diag_format.h`)
W definicjach identyfikatorów `GetParameter` (`HDA_PAR_*`) znajdowały się błędne stałe, rozbieżne ze specyfikacją Intel High Definition Audio 1.0 (Table 72 & Table 140):
- `HDA_PAR_VENDOR_ID` wynosiło `0x02` zamiast **`0x00`** (w specyfikacji `0x00` to Vendor/Device ID, a `0x02` to Revision ID).
- `HDA_PAR_REV_ID` wynosiło `0x03` zamiast **`0x02`**.
- `HDA_PAR_SUB_NODE_COUNT` wynosiło `0x05` zamiast **`0x04`**.
- `HDA_PAR_PIN_CAP` wynosiło `0x0D` zamiast **`0x0C`**.
- `HDA_PAR_CONNLIST_LEN` wynosiło `0x0F` zamiast **`0x0E`**.

Ponieważ generyczny skaner wysyłał polecenia `GetParameter` z błędnymi numerami parametrów, kodek QEMU zwracał odpowiedź `0` lub ignorował polecenie, co uniemożliwiało pobranie Vendor ID, podwęzłów funkcyjnych i widgetów.

### 2.3. Błędna maska długości listy połączeń (`HDA_CONNLIST_LEN_MASK`)
Stała `HDA_CONNLIST_LEN_MASK` była zdefiniowana jako `0xFF`, podczas gdy specyfikacja HDA 1.0 Table 79 definiuje:
- Bity 6:0 (`0x7F`) jako liczbę wpisów połączeń (`Connection List Length`).
- Bit 7 (`0x80`) jako flagę długiej formy listy (`Long Form`, wpisy 16-bitowe zamiast 8-bitowych).
Wartość `0xFF` powodowała błędną interpretację rozmiaru listy przy ustawionej fladze `Long Form`.

### 2.4. Brak odróżniania stanów braku kodeka od braku głośnika oraz nieuwzględnienie Line Out
Logika banera statusu w aplikacji i diagnostyce przyjmowała, że jedynym poprawnym wyjściem jest „Głośnik”. Kodek QEMU (`hda-output`) deklaruje Pin Complex jako **Line Out** (`Default Config` device type = `0x00`, Pin Cap `Output = 1`). W rezultacie aplikacja wyświetlała mylący komunikat `Brak wyjścia Głośniki`, nawet gdy ścieżka do wyjścia liniowego istniała i była w pełni funkcjonalna. Co więcej, komunikat ten pojawiał się również wtedy, gdy kodek w ogóle nie odpowiedział na skanowanie.

---

## 3. Zastosowane rozwiązania techniczne

### 3.1. Przechowywanie trwałego stanu wykrycia kontrolera
W strukturze stanu kontrolera wprowadzono niezależne pola:
- `initial_statests`: wartość `STATESTS` zarejestrowana bezpośrednio po resecie kontrolera (`CRST=1`), przed jakimkolwiek zapisem potwierdzającym (W1C).
- `current_statests`: bieżący odczyt rejestru `HDA_REG_STATESTS`.
- `discovered_codec_mask`: maska kodeków wykrytych podczas startu systemu.
- `responsive_codec_mask`: maska kodeków, które pomyślnie odpowiedziały na aktywne zapytanie `GetParameter Vendor ID`.

Dzięki temu wyczyszczenie rejestru `STATESTS` na poziomie sprzętowym nie powoduje utraty informacji o fizycznie obecnych kodekach.

### 3.2. Naprawa sekwencji transportu Immediate Command (IC)
Zaimplementowano ścisłą i zgodną ze specyfikacją procedurę obsługi interfejsu poleceń natychmiastowych:
1. **Rejestry i szerokości:**
   - `HDA_REG_ICOI` (offset `0x60`, 32-bit): wejście polecenia (Command Out).
   - `HDA_REG_ICII` (offset `0x64`, 32-bit): wyjście odpowiedzi (Immediate In).
   - `HDA_REG_ICIS` (offset `0x68`, 16-bit): status interfejsu (Status/Control).
2. **Obsługa przed wysłaniem:**
   - Sprawdzenie bitu `IRV` (`1U << 1`, Immediate Result Valid). Jeśli bit pozostał z poprzedniej transakcji, następuje jego skasowanie poprzez zapis `1` (W1C: `mmio_zapisz16(HDA_REG_ICIS, HDA_ICIS_IRV)`).
   - Weryfikacja bezczynności interfejsu: bit `ICB` (`1U << 0`, Immediate Command Busy) musi wynosić `0`.
3. **Wysłanie zlecenia:**
   - Zapis 32-bitowego słowa polecenia do `ICOI`.
   - Zapis 16-bitowej wartości `HDA_ICIS_ICB` do rejestru `ICIS` (wystawienie bitu zajętości).
4. **Oczekiwanie i odczyt:**
   - Ograniczone w czasie odpytywanie bitu `ICB` (z limitem mikroisekund).
   - Po opadnięciu `ICB` weryfikacja, czy ustawiony został bit `IRV`.
   - Odczyt 32-bitowej odpowiedzi z rejestru `ICII` i skopiowanie jej do pamięci procesora.
   - Skasowanie bitu `IRV` za pomocą operacji W1C przed zakończeniem transakcji.

### 3.3. Budowanie czasowników (Verb Construction) z walidacją
Wprowadzono bezpieczne funkcje pomocnicze inline:
- `hda_waliduj_cad(cad)`: sprawdza zakres adresu kodeka (0..14).
- `hda_waliduj_nid(nid)`: sprawdza zakres identyfikatora węzła (0..255).
- `hda_zbuduj_verb12(cad, nid, verb12, payload8, out_cmd)`: koduje polecenia z 12-bitowym identyfikatorem i 8-bitowym parametrem (`cad << 28 | nid << 20 | verb12 << 8 | payload8`).
- `hda_zbuduj_verb4(cad, nid, verb4, payload16, out_cmd)`: koduje polecenia z 4-bitowym identyfikatorem i 16-bitowym ładunkiem (`cad << 28 | nid << 20 | verb4 << 16 | payload16`).
- `hda_zbuduj_get_param(cad, nid, param_id, out_cmd)`: buduje bezpieczne zapytanie o parametr węzła (`verb12 = 0xF00`).

### 3.4. Aktywne ograniczone sondowanie kodeków
Wdrożono funkcję `hda_wew_aktywne_sondowanie_kodekow(uint16_t* responsive_mask)`:
- Pętla testuje każdy poprawny adres kodeka `CAD = 0..14`.
- Dla każdego adresu wysyła zapytanie `GetParameter` dla korzenia (`NID = 0`, parametr `HDA_PAR_VENDOR_ID`).
- Jeśli transport zakończy się sukcesem (`HDA_TRANSPORT_SUCCESS`), a zwrócony Vendor ID jest różny od `0` i `0xFFFFFFFF`, kodek jest oznaczany jako odpowiadający w masce `responsive_codec_mask`.
- Dodano jednorazowy, zwięzły log diagnostyczny jądra (`[HDA Diag] BDF 00:02.0 IC InitState:...`) emitowany wyłącznie przy pierwszym skanowaniu, co zapobiega zalewaniu konsoli szeregowej.

### 3.5. Bezpieczna enumeracja topologii w trybie tylko do odczytu
W module `sterowniki/dzwiek/hda_diag.cpp`:
- Enumeracja podwęzłów funkcyjnych (`GetParameter Sub-Node Count`).
- Identyfikacja grup Audio Function Group (typ `0x01`).
- Enumeracja widgetów z twardymi limitami pętli i tablic (`HDA_DIAG_MAX_WIDGETY = 64`).
- Pobieranie możliwości widgetów, możliwości pinów i konfiguracji domyślnej (`Default Config`).
- Bezpieczne przetwarzanie listy połączeń: obsługa formy krótkiej (8-bit) i długiej (16-bit), detekcja zakresów połączeń (`Connection List Range`) oraz ochrona przed pętlami w grafie topologii.
- Diagnostyka pozostaje całkowicie w trybie tylko do odczytu: nie modyfikuje rejestrów wzmacniaczy, przełączników połączeń ani zasilania widgetów.

### 3.6. 11 stanów banera diagnostycznego i obsługa Line Out
Rozszerzono maszynę stanów diagnostyki o 11 odrębnych, precyzyjnych komunikatów:
1. `Brak kontrolera HDA` (`HDA_DIAG_STAN_BRAK_KONTROLERA`)
2. `Kontroler HDA wykryty` (`HDA_DIAG_STAN_KONTROLER_OK`)
3. `Brak odpowiedzi kodeka` (`HDA_DIAG_STAN_KODEK_BRAK_ODPOWIEDZI`)
4. `Kodek wykryty` (`HDA_DIAG_STAN_KODEK_OK`)
5. `Brak Audio Function Group` (`HDA_DIAG_STAN_BRAK_AFG`)
6. `Brak wykrytego wyjścia audio` (`HDA_DIAG_STAN_BRAK_WYJSCIA`)
7. `Wyjście Głośniki` (`HDA_DIAG_STAN_WYJSCIE_GLOSNIKI`)
8. `Wyjście Słuchawki` (`HDA_DIAG_STAN_WYJSCIE_SLUCHAWKI`)
9. `Wyjście Line Out` (`HDA_DIAG_STAN_WYJSCIE_LINE_OUT`)
10. `Wyjście HDMI / DisplayPort` (`HDA_DIAG_STAN_WYJSCIE_CYFROWE`)
11. `Nieznany typ wyjścia` (`HDA_DIAG_STAN_WYJSCIE_NIEZNANE`)

Gdy kodek w QEMU zgłasza pin Line Out (NID 3) podłączony do konwertera DAC (NID 2), aplikacja poprawnie wyświetla zielony wskaźnik `Wyjście Line Out`, zamiast fałszywego błędu o braku głośników.

---

## 4. Weryfikacja i wyniki testów

### 4.1. Budżet rozmiaru pliku BUR
Aplikacja `test_dzwieku.bin` spełnia rygorystyczny limit pamięci:
- **Rozmiar pliku:** dokładnie **119 040 bajtów** (limit: 524 288 B).
- **Zapas:** 405 248 bajtów (77,3% budżetu).
- Weryfikator rozmiaru: `narzedzia/test_dzwieku_size.py PASS`.

### 4.2. Testy jednostkowe i regresyjne
Wykonano kompletny zestaw testów automatycznych na hoście:
- `make test-hda-diag`:
  - 209 testów jednostkowych, 0 błędów.
  - Testy walidacji parametrów, kodowania poleceń 12-bit/4-bit/GetParameter.
  - Testy symulacji sekwencji Immediate Command z obsługą W1C i flagi stale IRV.
  - Testy 11 stanów banera i klasyfikacji urządzeń Default Configuration.
  - Testy dekodowania topologii QEMU (DAC NID 2 -> Line Out NID 3).
- `test-hda-bws`:
  - 148 testów izolacji Ring 3 / BWS 89/90, 0 błędów.
  - Weryfikacja niezmienności struktury zrzutu diagnostycznego.
- `make test-dzwieku-packaging`:
  - 63 testy audytu pakowania i segmentów BUR, 0 błędów.
- `make test-dzwieku-repair`:
  - Wszystkie 30 scenariuszy loadera (724 209 sprawdzeń pod ASan + UBSan) – PASS.
  - 28 scenariuszy GUI, renderowania czcionki i zapisu raportu (2 457 sprawdzeń pod ASan + UBSan) – PASS.
  - 6 rzeczywistych scenariuszy BWS 89/90/HDA (123 sprawdzenia pod ASan + UBSan) – PASS.

### 4.3. Kompilacja systemu i obrazu ISO
Wykonano pełny przebieg kompilacji środowiska produkcyjnego:
- Polecenie: `make -B -j2 all iso` zakończyło się kodem 0.
- Wygenerowano poprawny obraz Multiboot2: `BursztynOS.iso`.
- Obraz zawiera zaktualizowane jądro, plik wykonywalny `test_dzwieku`, czcionki PSF oraz moduł tapet.

---

## 5. Podsumowanie

Wszystkie wymagania stawiane w zadaniu zostały spełnione:
1. Rozwiązano problem braku komunikacji z kodekiem w Immediate Command Interface.
2. Zaimplementowano aktywne, ograniczone sondowanie kodeków z trwałym zapamiętywaniem stanu `STATESTS`.
3. Naprawiono numery parametrów w nagłówku formatu ABI.
4. Uzupełniono rozpoznawanie urządzeń o `Wyjście Line Out` oraz wyeliminowano fałszywy komunikat `Brak wyjścia Głośniki`.
5. Zachowano działanie dotychczasowej ścieżki generowania tonu testowego (`hda_test_ton`).
6. Utrzymano w nienaruszonym stanie integrację BursztynUI, motywy, czcionkę Unicode, zapis raportu `hda.txt`, naprawę myszy VirtIO oraz koegzystencję xHCI.
