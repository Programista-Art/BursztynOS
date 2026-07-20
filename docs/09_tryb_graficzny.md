## 08. Podsumowanie Etapu 7: Tryb Graficzny i Składacz Obrazu

Niniejszy dokument stanowi oficjalne podsumowanie i kamień milowy w rozwoju Bursztyn OS. System z powodzeniem opuścił archaiczny tryb tekstowy (VGA Text Mode) i wkroczył w erę w pełni okienkowego interfejsu graficznego (GUI), zarządzanego przez autorski Składacz Obrazu (Compositor).

# 1. Zrealizowane Cele Technologiczne

Podczas tego etapu udało się z powodzeniem wdrożyć i ustabilizować następujące kluczowe technologie:

## 1.1 Natywny Sterownik Bochs VBE

System nie polega już wyłącznie na łasce bootloadera (GRUB-a) w kwestii grafiki. Zaimplementowano bezpośrednią komunikację ze sprzętem z wykorzystaniem portów I/O (0x01CE i 0x01CF). Pozwala to na:

* Weryfikację obecności kompatybilnej karty graficznej na szynie.

* Sprzętowe wymuszenie rozdzielczości (obecnie 1024x768x32) w dowolnym momencie działania jądra.

* Pobranie bezpośredniego wskaźnika do pamięci karty wideo (LFB - Linear Framebuffer).

# 1.2 Ochrona Pamięci i Double Buffering

Dzięki rozbudowie Menedżera Pamięci Wirtualnej (VMM), system bezpiecznie mapuje ogromne ilości pamięci graficznej.

* Zaimplementowano system Podwójnego Buforowania (Double Buffering). Cały obraz jest najpierw rysowany w ukrytej pamięci RAM (Backbuffer alokowany na adresie 0x80000000), a następnie błyskawicznie kopiowany na ekran. Całkowicie eliminuje to efekt migotania (flickering).

# 1.3 Menedżer Okien i Z-Order (Aktywny Focus)

* Zbudowano od zera podstawy Menedżera Okien:

Okna posiadają strukturę (współrzędne, wymiary, pasek tytułowy, tło).

* Wprowadzono Z-Order. System wie, które okno jest "pod spodem", a które "na wierzchu".

* Po kliknięciu w dowolne okno, algorytm natychmiast przenosi je na pierwszą warstwę widoczności.

# 1.4 Interakcja i Drag & Drop (Przeciąganie)

Sterownik myszy PS/2 został zintegrowany z interfejsem graficznym.

* Zbudowano maszynę stanów (DragStan), która wykrywa wciśnięcie lewego przycisku myszy na niebieskiej belce tytułowej.

* Użytkownik może płynnie przesuwać okna po całym ekranie, a system w czasie rzeczywistym odrysowuje pulpit, zachowując zawartość pod spodem.

2. Architektura Komunikacyjna GUI

Przepływ danych w nowym trybie graficznym Bursztyn OS prezentuje się następująco:

[ Klawiatura/Mysz PS/2 ] ---> [ Przerwania APIC (Wektor 33, 44) ]
                                            |
                                            v
[ Aplikacja Ring 3 (Terminal) ] <---> [ BWS / Jądro ]
                                            |
                                            v
[ SKŁADACZ OBRAZU (Menedżer Okien) ] ---> [ Backbuffer (RAM) ]
                                            |
                                            v
                                     [ Karta Bochs VBE (LFB) ] ---> MONITOR


# 3. Kolejne Kroki (Roadmapa na Etap 8)

Mając tak potężny i stabilny fundament, projekt jest gotowy na przyjęcie kolejnych funkcji interfejsu użytkownika:

1. Routing Klawiatury (Pisanie w oknie): Połączenie wejścia z klawiatury z systemem Focusu. Jeśli Edytor jest na wierzchu, wciśnięte klawisze powinny pojawiać się w nim, a nie w Terminalu.

1. Przyciski i Eventy: Dodanie przycisku [X] (Zakończ/Zamknij) na pasku tytułowym.

1. Pasek Zadań (Taskbar): Zmiana dolnego, szarego paska w interaktywne menu, z którego można minimalizować okna lub uruchamiać nowe programy .bur.

1. Dynamiczne Tworzenie Okien: Rozbudowa BWS, aby programy uruchomione z RAM-dysku (BSP) mogły poprosić system o "Narysowanie własnego okna o wymiarach X na Y" i przejąć nad nim kontrolę.