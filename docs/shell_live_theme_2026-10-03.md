# Shell: wsparcie i zmiana motywu podczas pracy

Polecenie publiczne: `wsparcie`. Dokładny opis pomocy:
`Wyświetla listę wspierających Bursztyn OS.` Nie ma wymaganego aliasu
`sponsorzy`. Pozostałe wystąpienia tej nazwy dotyczą zasobu i jego symboli
linkera, a nie polecenia. Nazwy nadal pochodzą z BSP2
`/dane/sponsorze/sponsorzy`; ścieżka i loader pozostają bez zmian.
Wersja pozostaje `Bursztyn OS v1.0.1.6`.

Przyczyna: `motyw_rozglos_zmiane` wysyła istniejące
`BWS_ZDARZENIE_MOTYW_ZMIENIONY` do procesów. `gui_czekaj_na_zdarzenie`
odświeża `g_lokalny_motyw` w bursztyn_gui.cpp przez BWS_MOTYW_POBIERZ.
Shell nie miał obsługi zdarzenia wymuszającej repaint i aktualizację
snapshotów stylów `Motyw`/kontrolek. Otwarte menu pochłaniało zdarzenie
w shell_context_event. Nie istnieje osobny snapshot BursztynMotyw w Shell.

Obsługa teraz poprzedza dispatch popupu. Korzysta z tych samych API co
BursztynUI::Okno: Motyw::zaktualizuj_z_systemu, aktualizacja kontrolki
scrollbara i ZarzadcaPopupow::zaktualizuj_motyw. Mechanizm jest istniejący,
reusable dla Ring 3; jedynie integracja pętli zdarzeń jest lokalna dla Shell.
Nie dodano pollingu, ABI ani nowego magazynu motywów.

shell_rysuj_terminal rysuje i publikuje cały obszar klienta: wyjście,
wejście, zaznaczenia, caret, separator i scrollbar. Przy niezmienionej
geometrii/skali nie wywołuje rysowania belki WM. Popup manager aktualizuje
style otwartych popupów i wymusza pełne przerysowanie ich powierzchni,
bez zamykania menu ani resetowania aktywnego elementu. Przy następnym
otwarciu MenuKontekstowe ponownie pobiera aktualny styl.

Nie są odtwarzane bufory ani resetowane zaznaczenie, historia, clipboard,
scroll pionowy lub poziomy. Kolory tekstu i zaznaczenia są rozwiązywane
podczas rysowania; geometria i skala DPI pozostają bez zmian. Nie jest
włączane skalowanie legacy. Zminimalizowane okno rysuje aktualny motyw
po przywróceniu.

Test hostowy wykonuje rzeczywisty parser i pomoc oraz pętlę Shell ze
zmienionymi mockowanymi kolorami motywu, również przy otwartym popupie,
przy 100% i 125%. Sprawdza zmianę pikseli klienta/popupu i zachowanie
buforów, wejścia, pozycji caret, obu zaznaczeń, historii, schowka oraz
scrollowania. Jest to test hostowy, nie walidacja runtime systemu.
Końcowa walidacja runtime pozostaje do ręcznego wykonania przez użytkownika;
QEMU nie jest uruchamiane w tej sesji.

Wyniki tej sesji: `make -j2 shell_blob.o` PASS; istniejący rozszerzony
harness `test_shell_terminal.cpp` PASS; ASan+UBSan PASS
(`ASAN_OPTIONS=detect_leaks=0`, zgodnie z obecnym harnessiem);
`make -j2 all iso` PASS (exit 0). `git diff --check` PASS.
Nie wykonano walidacji runtime ani nie uruchomiono QEMU.
