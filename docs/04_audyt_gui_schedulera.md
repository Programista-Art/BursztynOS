# Audyt GUI, wejscia i schedulera

## Przyczyny zastane

- BWS 18 (`gui_pobierz_mysz`) wywolywal `ZablokujAktualnyProcesNaMyszy()`.
  Wszystkie aplikacje czekaly na jeden globalny licznik, a IRQ12 budzilo
  wszystkie procesy w `PROCES_ZABLOKOWANY_MYSZ`.
- `pid_przejmujacy_mysz` byl nadpisywany przez kazda aplikacje wywolujaca
  BWS 22. Nie byl to hit-test ani focus; ostatni syscall wygrywal.
- Kazdy proces widzial ten sam globalny bit przycisku i sam odtwarzal zbocze
  `down` z poprzedniej probki. Proces, ktory nie dostal kwantu przy zboczu,
  mogl zgubic klik, zas kilka procesow moglo uznac ten sam stan za klik.
- `zaktualizuj_klawiature_gui()` zawsze zwracalo `false`, wiec znaki trafialy
  do wspolnego bufora zamiast do aktywnego okna.
- BWS 17 wykonywal natychmiast pelna kompozycje. Aplikacje dodatkowo wolaly
  go przy ruchu/drag, co mnozylo pelne przebiegi po framebufferze.
- Menedzer Okien sprawdzal RTC w petli pollingu myszy. Bez wybudzenia przez
  mysz jego kod aktualizacji zegara nie wykonywal sie regularnie.
- LAPIC jest periodic, divider 16, initial count `0x05ffffff`; nie ma
  kalibracji ani znanej czestotliwosci. Scheduler nie przelacza zwyklego
  procesu, gdy timer przerwie Ring 0, poniewaz rama SYSCALL nie jest rama ISR.
- HTTP/HTTPS/TLS sa synchronicznymi syscallami. Handshake i odbior maja
  ograniczone, lecz dlugie petle pollingowe w Ring 0; timer wtedy obsluguje
  IRQ/EOI, ale celowo nie zmienia procesu. To pozostaje zrodlem dluzszych
  zatrzyman GUI.
- EOI jest wysylane centralnie w `idt.cpp`; sterowniki PS/2 nie wysylaja
  drugiego EOI.
- `SYS_EXIT` juz stosowal deferred cleanup kernel stack/PML4. Warstwa byla
  usuwana przed oznaczeniem procesu pustym, lecz brakowalo czyszczenia
  niezaleznego focus/capture i kolejki zdarzen.

## Wprowadzony model

IRQ PS/2 wykonuje hit-test najwyzszej warstwy, ustawia focus na `MOUSE_DOWN`
i dodaje bounded event tylko do kolejki focusowanego/capture PID. Ruch i timer
sa scalane. Proces czekajacy na pusta kolejke przechodzi w osobny stan
`PROCES_ZABLOKOWANY_ZDARZENIE`, a enqueue budzi tylko jego.

`gui_odswiez()` jedynie ustawia dirty. Pelen compositor jest wykonywany przez
PID 0 poza IRQ. Timer IRQ dodaje pulpitowi scalany `TIMER`; pulpit odczytuje
RTC i sklada klatke tylko po rzeczywistej zmianie czasu.

## Ograniczenia

- LAPIC wymaga kalibracji z PIT/HPET/PM timerem przed deklarowaniem 100/250 Hz.
- Siec/TLS wymaga osobnej, stanowej maszyny pracy lub WAITING_IO. Bez zmiany
  ABI ramy SYSCALL nie wolno wymuszac context switcha w srodku TLS.
- Compositor ma globalny dirty, nie dirty rectangles; poprawia liczbe klatek,
  ale pojedyncza klatka nadal sklada cala powierzchnie.
- Projekt pozostaje jednordzeniowy; przed SMP lifetime warstw wymaga
  refcountowanego snapshotu zamiast surowych wskaznikow.
