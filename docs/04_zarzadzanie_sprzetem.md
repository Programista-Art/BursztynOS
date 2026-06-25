# 04. Zarządzanie Sprzętem i Inicjalizacja Podsystemów Procesora

Niniejszy dokument opisuje niskopoziomowe mechanizmy zarządzania sprzętem zaimplementowane w Jądrze Bursztyna. Sekcja ta obejmuje konfigurację deskryptorów segmentów i przerwań procesora, przejęcie kontroli nad asynchronicznością systemu poprzez nowoczesny kontroler APIC oraz implementację wczesnych sterowników wejścia/wyjścia (I/O).

## 4.1 Globalna Tablica Deskryptorów (GDT64)

Mimo że w architekturze x86-64 stronicowanie (Paging) przejmuje niemal całkowitą kontrolę nad izolacją pamięci wirtualnej, a mechanizm segmentacji został w większości zdeprecjonowany (baza segmentu i limit są sprzętowo wymuszone jako `0`), procesor nadal wymaga poprawnej konfiguracji Globalnej Tablicy Deskryptorów (GDT). Jest to niezbędne do zdefiniowania poziomów uprawnień (Privilege Rings) dla kodu i danych oraz przypisania selektorów dla instrukcji sterujących (np. `SYSCALL`/`SYSRET`).

Bursztyn OS implementuje płaską strukturę segmentacji składającą się z 5 deskryptorów (każdy o długości 8 bajtów):

| Indeks | Selektor | Typ Segmentu | Ring | Bajt Dostępu | Flagi Wymiarowe | Przeznaczenie |
| --- | --- | --- | --- | --- | --- | --- |
| **0** | `0x00` | Null Descriptor | - | `0x00` | `0x00` | Wymagany sprzętowo deskryptor zerowy |
| **1** | `0x08` | Kod Jądra | Ring 0 | `0x9A` | `0x20` (Bit L) | Kod jądra w trybie Long Mode |
| **2** | `0x10` | Dane Jądra | Ring 0 | `0x92` | `0x00` | Dane jądra (odczyt/zapis) |
| **3** | `0x1B` | Kod Użytkownika | Ring 3 | `0xFA` | `0x20` (Bit L) | Kod aplikacji użytkownika `.bur` |
| **4** | `0x23` | Dane Użytkownika | Ring 3 | `0xF2` | `0x00` | Dane aplikacji użytkownika (Ring 3) |

### Specyfikacja Bajtów Dostępu i Flag:

* **`0x9A` (Kod Jądra):** Present=1, Ring=00, Wykonywalny=1, Odczyt/Zapis=1.
* **`0xFA` (Kod Użytkownika):** Present=1, Ring=11 (Ring 3), Wykonywalny=1, Odczyt/Zapis=1.
* **`0x20` (FLAGA_64_BIT):** Ustawia tzw. *Long Mode Bit* (Bit L) w wyższym nibble deskryptora, informując procesor, że dany segment zawiera natywny kod 64-bitowy.

Inicjalizacja ładowania realizowana jest poprzez strukturę rejestru `RejestrGDT` (`GDTR`) o długości 10 bytes (2 bajty limitu rozmiaru oraz 8 bajtów adresu liniowego tablicy w pamięci) za pomocą zewnętrznej procedury asemblerowej `zaladuj_zaktualizowane_gdt`.

## 4.2 Tablica Deskryptorów Przerwań (IDT)

Tablica Deskryptorów Przerwań (IDT) odpowiada za obsługę zdarzeń asynchronicznych i wyjątków procesora. In trybie 64-bitowym deskryptory IDT (bramki przerwań) zostały rozszerzone z 8 do 16 bajtów, aby pomieścić pełny, 64-bitowy adres procedury obsługi (ISR).

Bursztyn OS konfiguruje siatkę pierwszych 48 wektorów przerwań z 256 dostępnych, nakładając flagę atrybutów `0x8E` (Bramka Przerwania trybu 64-bit, Ring 0, Present):

* **Wektory 0–31:** Rezerwowane sprzętowo dla wyjątków procesora (np. `0` - Division by Zero, `13` - General Protection Fault, `14` - Page Fault).
* **Wektor 32:** Przypisany sprzętowo do przerwania zegarowego układu APIC Timer.
* **Wektor 33:** Przypisany do obsługi kontrolera klawiatury (IRQ1).

### Wspólna Obsługa Przerwań (`WspolnaObslugaPrzerwan`):

Wszystkie niskopoziomowe procedury asemblerowe zrzucają stan procesora do struktury `RejestryStanowe` na stosie, a następnie przekazują wskaźnik do unifikowanego parsera w C++:

1. Jeśli odebrany wektor przerwania jest mniejszy niż 32, system identyfikuje to jako krytyczny wyjątek sprzętowy, wyświetla komunikat błędu `BLAD SYSTEMOWY: Wystapil wyjatek sprzetowy!` i wprowadza procesor w stan permanentnego uśpienia (`hlt`).
2. Jeśli wektor wynosi dokładnie 32, wywoływany jest moduł asynchronicznego zegara systemowego.

## 4.3 Demontaż PIC i Aktywacja Kontrolera APIC

W celu zapewnienia stabilnej pracy współbieżnej i rezygnacji z archaicznych rozwiązań architektury PC, Bursztyn OS całkowicie wyłącza przestarzałe układy Intel 8259A PIC (Programmable Interrupt Controller) i aktywuje nowoczesny standard APIC (Advanced Programmable Interrupt Controller).

### Procedura Wyciszenia PIC:

Układy Master PIC i Slave PIC zostają całkowicie zamaskowane poprzez wysłanie wartości `0xFF` (1111 1111 - maskowanie wszystkich pinów) odpowiednio na porty danych `0x21` oraz `0xA1`:

```cpp
wyjscie_port_bajt(0x21, 0xFF);

```

### Konfiguracja i Mapowanie Local APIC (LAPIC):

1. **Odczyt z MSR:** Adres bazowy rejestrów sterujących Local APIC jest pobierany z rejestru specyficznego dla modelu (MSR) o nazwie `IA32_APIC_BASE` (adres `0x1B`). Typowy adres fizyczny kontrolera to `0xFEE00000`.
2. **Dynamiczne Mapowanie:** Autorski **Zarządca Pamięci Wirtualnej** wymusza dla tego obszaru mapowanie 1:1, dzięki czemu wskaźnik `baza_lapic_wirtualna` uzyskuje bezpośredni, bezpieczny dostęp do rejestrów sprzętowych pod adresem fizycznym.
3. **Włączenie na szynie:** Bit nr 11 rejestru MSR `IA32_APIC_BASE` zostaje ustawiony na 1 w celu sprzętowego otwarcia szyny jednostki APIC.
4. **Aktywacja programowa:** Do rejestru przerwań rzekomych (*Spurious Interrupt Vector Register* - przesunięcie `0x0F0`) wpisywany jest tymczasowy wektor obsługi 255 połączony bitowo (OR) z maską `0x100` (Bit nr 8 - *APIC Software Enable*).

## 4.4 Konfiguracja Zegara Systemowego (LAPIC Timer)

Sercem asynchroniczności i przyszłego **Planisty Włókiem** w Bursztyn OS jest wewnętrzny zegar kontrolera APIC, tykający na dedykowanym wektorze przerwania nr 32.

Konfiguracja rejestrów LAPIC przebiega następująco:

1. **LVT Timer Register (0x320):** Zostaje załadowany wartością `32 | 0x20000`. Bit nr 17 (`0x20000`) przełącza zegar w stabilny *Tryb Cykliczny* (Periodic Mode), a wartość 32 przypisuje wektor przerwania w IDT.
2. **Divide Configuration Register (0x3E0):** Zostaje załadowany wartością `0x3`, co ustawia dzielnik częstotliwości szyny systemowej przez 16.
3. **Initial Count Register (0x380):** Wpisanie wartości początkowej `0x05FFFFFF` inicjuje odliczanie i uruchamia cykliczne generowanie przerwań.

### Sygnalizacja EOI (End of Interrupt):

Po odebraniu każdego **tyknięcia (ticku)** zegara przez jądro w funkcji `WspolnaObslugaPrzerwan`, system musi jawnie poinformować kontroler LAPIC, że aktualne zgłoszenie zostało przetworzone, oczyszczając bufor. Realizowane jest to poprzez wpisanie wartości 0 do rejestru EOI pod przesunięciem `0x0B0`:

```cpp
baza_lapic_wirtualna[0x0B0 / 4] = 0;

```

## 4.5 Sterownik Klawiatury PS/2 (Wczesne I/O)

Sterownik klawiatury w Bursztyn OS zapewnia interakcję z systemem na etapie powłoki tekstowej, operując bezpośrednio na rejestrach kontrolera i8420 płyty głównej.

### Mechanizm Odczytu i Scancode Set 1:

1. Przerwanie sprzętowe IRQ1 (wektor 33) wywołuje funkcję `ObslugaPrzerwaniaKlawiatury`.
2. Sterownik pobiera binarną wartość (Scancode) ze sprzętowego portu wejściowego `0x60`:

```cpp
uint8_t kod_klawisza = wejscie_port_bajt(0x60);    

```

3. Najbardziej znaczący bit bajtu (Bit nr 7 - maska `0x80`) działa jako flaga kierunku. Jeśli bit jest ustawiony, oznacza to zdarzenie puszczenia klawisza (Break code), które sterownik ignoruje. Jeśli bit wynosi 0, mamy do czynienia z wciśnięciem klawisza (Make code).
4. Kod klawisza po odfiltrowaniu maską `0x7F` jest mapowany na znak ASCII za pomocą wbudowanej tablicy translacji `mapa_klawiatury_set1`. Tablica w tej wczesnej fazie realizuje mapowanie bez znaków diakrytycznych, co stanowi punkt wyjścia przed pełnym wdrożeniem standardu UTF-8 w docelowym trybie graficznym (Linear Framebuffer).

### Zarządzanie Buforem i Echo Ekranu:

Sterownik utrzymuje globalny bufor o długości 80 znaków (`bufor_klawiatury`) oraz wskaźnik pozycji:

* Wykrycie znaku backspace (`\b`) przy `pozycja_kursora > 0` cofa wskaźnik i nadpisuje poprzedni znak spacją (graficzne wymazanie).
* Wykrycie znaku nowej linii (`\n` - klawisz Enter) czyści bufor i resetuje pozycję wskaźnika do 0.
* Standardowe znaki alfanumeryczne są dopisywane do bufora, a terminalny bajt zawsze ustawiany jest na `\0` (Null terminator).

Wynik modyfikacji bufora jest natychmiastowo przekazywany do funkcji `WypiszNaEkranie`, która mapuje znaki bezpośrednio do fizycznej strefy pamięci Legacy VGA pod adresem `0xB8000` (wiersz nr 2), realizując mechanizm lokalnego echa (Echo).