# HDA: transport nieblokujący, CORB/RIRB i pierwszy raport Acera — 2026-10-12

## Zakres i granica akceptacji

Kontynuacja worktree `244d8984e13839ff0d460b5664de0cea75ea728c`. Punktem odniesienia jest działające wykrywanie QEMU: kodek **1AF4:0012**, AFG **01**, Audio Output **02**, Pin Complex **03**, **Line Out**, połączenie **02 → 03**. Pozostaje dotychczasowa konfiguracja i generowanie tonu QEMU. Testy hostowe sprawdzają ten układ, lecz nie zastępują ręcznej akceptacji QEMU.

Znane dane fizyczne przekazane przez użytkownika:

| PCI BDF | Vendor:Device | Znaczenie |
|---|---|---|
| 00:1F.3 | 8086:A348 | Intel HDA, oczekiwany kontroler wbudowanego kodeka |
| 01:00.1 | 10DE:0FB9 | NVIDIA HDA, osobna topologia HDMI/DisplayPort |

Identyfikatory te są dowodem dotyczącym konkretnego laptopa, nie listą obsługiwanych urządzeń. Wykrywanie opiera się wyłącznie na klasie PCI **04**, podklasie **03**. Nazwa Windows „Realtek Audio” nie ustala modelu kodeka.

Nie znaleziono fizycznego `hda.txt` Acera w repozytorium. **Exact Realtek codec Vendor and Device ID is not yet known. Exact physical speaker NIDs are not yet known. Exact amplifier and EAPD requirements are not yet known. Vendor specific Realtek quirks must not be implemented until real hardware evidence exists.**

Obraz przeznaczony jest do ręcznego testu Live USB i pozyskania raportu. Nie stwierdzono, że dźwięk na Acerze działa. Fizyczny test tonu jest celowo niedostępny przed analizą rzeczywistej topologii. Nie uruchamiano QEMU, VirtualBox, instalatora ani zapisu na dysk Acera.

## Dlaczego poprzedni BWS 89 zatrzymywał mysz

Budżet czterech verbów ograniczał ich liczbę, ale każdy verb wykonywał synchroniczne oczekiwanie na ICB/IRV. Jeden syscall nadal mógł długo zajmować CPU. Nowy skaner nie wywołuje starej synchronicznej funkcji; w teście produkcyjnego skanera jej osiągnięcie kończy test błędem.

Wspólny kernelowy transport ma operacje zgłoszenia, pojedynczego kroku, odebrania wyniku oraz anulowania/odzyskiwania. IC przechodzi przez przygotowanie, sprawdzenie bezczynności, usunięcie starej IRV, wysłanie, sprawdzenie zakończenia ICB, sprawdzenie IRV, odczyt i stan końcowy. Każda faza wraca do Ring 3. Nie ma pętli oczekującej na rejestr ani na deadline w ścieżce BWS 89. CORB/RIRB używa tego samego interfejsu; skaner nie zna wybranego transportu.

Jeden kontroler ma najwyżej jedną transakcję i właściciela. Krok skanera obsługuje jednego kontrolera oraz jedną fazę; nigdy pętli kompletnych verbów. W przygotowaniu migawki obrabia najwyżej **16 KiB RAM** na wywołanie. Budowanie ścieżek ma limit **8192 kandydatów**, głębokość **16** i jawne oznaczenie obcięcia. Inicjalizacja ma odrębne ograniczone fazy; sprawdzenie zatrzymania DMA czyta najwyżej 30 deskryptorów, bez ponawiania w pętli.

Deadline jest absolutnym czasem HPET. Każdy krok najpierw sprawdza aktualną gotowość sprzętu, a dopiero przy braku postępu upływ terminu: interwał GUI nie może odrzucać odpowiedzi, która już przyszła. Stary kod miał limity 2 000 000 / 5 000 000 **iteracji CPU**, nie milisekundy. Nie istnieje ich przenośna równoważność czasowa. Jawna polityka wynosi **2 ms** oczekiwania na verb oraz **5 ms** zmiany stanu resetu/ringów; nie zwiększano jej w celu ukrycia problemów sprzętu. CRST=0 utrzymywany jest co najmniej 100 µs, po CRST=1 stabilizacja kodeków trwa co najmniej 542 µs (poprzedni próg 13000 taktów WALCLK). Synchroniczne ograniczone adaptery istnieją wyłącznie dla starego startowego odtwarzania QEMU, poza BWS 89.

`BWS_HDA_DIAG_METRYKI` zwraca największy rzeczywisty czas HPET kroku skanera. Kontrolery raportują też maksimum pojedynczej fazy transportu. Produkcyjne logowanie tych pomiarów jest wyłączone. Model testowy nalicza deterministyczny koszt operacji rejestrów; dodatkowo test mierzy czas ścienny i czas CPU hosta. Czas hosta pod ASan oraz planowanie procesów nie są gwarancją opóźnień fizycznego systemu.

## PCI, BAR, reset i niezależność kontrolerów

Rejestr mieści cztery kontrolery. Odczytywane są wszystkie funkcje PCI, również HDA poza funkcją 0. Przekroczenie limitu jest jawne w raporcie. Błąd inicjalizacji pozostawia kontroler w rejestrze wraz z BDF i dokładnym etapem.

Każdy kontekst ma własny BAR, mapowanie, GCAP, wersję, GCTL, początkowe/bieżące STATESTS, maski obecności i odpowiedzi kodeków, stream capabilities, DMA ringów, wskaźniki, transakcję, deadline, właściciela i generację. Kodek CAD=0 na jednym kontrolerze nie jest kodekiem CAD=0 na drugim.

BAR0 jest sprawdzany jako MMIO, 32- lub 64-bitowy; składane są oba DWORD-y BAR64. Odrzucane są typy I/O/zarezerwowane, prefetchable, niewyrównane lub przepełniające adresy. Standardowe okno HDA ma 16 KiB. PCI Command otrzymuje Memory Space i Bus Master; pozostałe bity pozostają zachowane. Przy zapisie DWORD-a górna połowa Status wynosi zero, aby nie skasować W1C.

Mapowanie używa istniejącego VMM z PCD|PWT, supervisor-only, w dedykowanym zakresie `0xffff920000000000 + index*0x10000`. Sprawdzany jest wynik PTE i adres ramki. Zakres nie pokrywa buforów grafiki (`ffff9000...`) ani stosów jądra (`ffff9400...`). Nie używa się zwykłego mapowania RAM/HHDM do rejestrów. Wartość BAR fizycznego jest metadaną raportu; adresy wirtualne jądra i adresy DMA nie trafiają do Ring 3.

Po sprawdzeniu wersji 1.0 i GCAP zatrzymywane są DMA i przerwania HDA. CRST jest zerowany i ustawiany w osobnych fazach z deadline. STATESTS jest zapamiętany przed W1C, ponownie odczytany po stabilizacji, a wykryta maska jest zachowana niezależnie od późniejszego wyzerowania rejestru. Skan aktywnie sprawdza legalne CAD **0..14**, nigdy adres broadcast 15.

Raport rozróżnia m.in. BAR, MMIO, PCI Memory/Bus Master, GCAP, wersję, HPET, stop DMA, oba etapy CRST, rozmiar/alokację/reset CORB i RIRB, start ringów oraz utratę CRST. Brak odpowiedzi kodeka po poprawnej inicjalizacji nie jest „Brak kontrolera HDA”. Recovery kończy się także po ostatnim nieodpowiadającym CAD, przed publikacją: sprawny kontroler bez kodeków nadal raportuje initialized + no codec response, a nie fałszywe niepowodzenie inicjalizacji.

## CORB/RIRB

Bufory należą do jądra i korzystają z istniejącego `dma_allocate`. Każdy ma stronę 4096 bajtów i wyrównanie co najmniej 128 bajtów. Kontroler bez 64OK musi dostać całą stronę poniżej 4 GiB; adres jest sprawdzany, nigdy obcinany. Kontroler 64-bitowy programuje również górne DWORD-y adresów. Testy obejmują adresy powyżej 4 GiB. Obecny systemowy allocator przydziela najniższe dostępne ciągłe RAM; odrzucenie nieadresowalnego bufora ma osobny etap błędu.

CORBSIZE/RIRBSIZE wybierają największy **rzeczywiście obsługiwany** rozmiar spośród 2, 16 i 256; brak capability jest błędem. Po zatrzymaniu ringów zapisywane są bazy i rozmiary oraz sprawdzany odczyt wybranego rozmiaru. CORBRP przechodzi handshake reset=1, reset=0; RIRBWP resetuje wskaźnik sprzętowy. Start wymaga potwierdzenia RUN. W trybie IC ringi są przygotowane, lecz zatrzymane.

Normalny transport fizyczny to CORB/RIRB. Zgodność istniejącego QEMU 8086:2668 zachowuje IC; jest to polityka transportu, nie kryterium wykrywania. Kodek QEMU jest dodatkowo sprawdzany jako znany identyfikator 1AF4:0012/0022 przed dotychczasową konfiguracją NID 2/3. Te NID-y nie są używane do konfiguracji fizycznego kodeka.

Publikacja wpisu CORB używa bariery pamięci przed WP; odczyt RIRB jest volatile z barierą. Wskaźniki zawijają się według wybranego rozmiaru. Pełny CORB zwraca pending do deadline, potem dokładny status. RIRB odbiera najwyżej jeden wpis na fazę; CAD musi pasować, unsolicited jest liczony oddzielnie, obce/stare odpowiedzi są odrzucane. Zarezerwowane bity i przepełnienie RIRB są błędem. Nie włączono unsolicited events w kodekach.

Przy opóźnionym kroku GUI poprawna odpowiedź może już leżeć za unsolicited. Opróżnianie takich gotowych wpisów ma dodatkowy limit jednego pełnego ringa; nie odrzuca oczekującej poprawnej odpowiedzi tylko dlatego, że poll GUI nastąpił po deadline. Gdy kolejka nie zawiera dalszej odpowiedzi, timeout pozostaje oparty na tym samym terminie HPET.

Po błędzie/anulowaniu transport przechodzi ograniczone odzyskiwanie: stop ringów, okres ciszy równy timeoutowi verba, ponowne zaprogramowanie/reset wskaźników i start. Nie resetuje całego kontrolera przy każdym braku kodeka. Odpowiedź poprzedniej generacji nie może być celowo przyjęta jako nowa. IC czeka na zwolnienie sprzętowego ICB; tylko potwierdzony kodek QEMU ma zgodność programowego wyzerowania ICB po sondowaniu nieistniejącego CAD. Fizyczny transport nie używa tego obejścia.

## Migawka, GUI i raport

ABI migawki jest podniesione do **2** ze względu na rozszerzony layout; stare wersje i niezgodne rozmiary są odrzucane. BWS pozostaje **89/90**. Opcjonalne 16-bajtowe żądanie START/STEP/CANCEL wybiera kontroler; null zachowuje skan wszystkich. Walidacja praw PZB, copy-in/copy-out i brak zatrzymanych wskaźników Ring 3 pozostają. Nie udostępniono raw verbów, PCI writes, MMIO ani DMA.

Dwa bufory publikacji oraz osobny bufor roboczy są obracane wskaźnikami przy zakończeniu, bez kopiowania całej migawki w kroku publikacji. Czytelnik ma krótką blokadę próbną na czas spójnego kopiowania; nigdy nie oczekuje w pętli. Odświeżenie zachowuje poprzednią pełną generację. Przy anulowaniu części skanu pozostałe kontrolery mogą dokończyć transakcje, ale nie publikuje się niepełnej generacji. Zapis `hda.txt` wymaga pełnej opublikowanej migawki i nie zapisuje uciętego bufora tekstowego.

Limity: 4 kontrolery × 4 kodeki × 4 Function Groups × 96 widgetów, 32 rozwinięte połączenia/widget, wszystkie 127 kodowanych wpisów listy w 64 surowych DWORD-ach, 64 trasy po 16 NID-ów, raport 1 MiB. Każdy limit i błędny zakres jest sprawdzany, przekroczenie oznaczone. Surowe listy pozwalają wyjaśnić obcięcie rozwiniętej listy. Nie dobierano limitów do dwuwidgetowego QEMU.

Odczyt obejmuje Vendor/Device, Revision, root subordinate range, wszystkie FG do limitu, parametry AFG, widget capabilities, dziedziczone/override PCM i amp, power capabilities/current state, pin capabilities/default config, selector i EAPD capabilities/state, surowe i rozwinięte short/long/range connection lists. Zakresy odnoszą się do poprzedniego wpisu, NID-y muszą być legalne. Dekodowanie poprawia m.in. gross location Internal, pola PCM i EAPD, widget delay w bitach 19:16 oraz actual PowerState w bitach 7:4.

GUI zachowuje BursztynUI, Unicode, zakładki i WskaznikZajetosci. Przy wielu kontrolerach pokazuje standardową ListaRozwijana z nazwą i numeric PCI IDs/BDF. Zakładki dotyczą wyboru, raport obejmuje wszystkie kontrolery. Widoczne są etapy inicjalizacji, transport, maski, dokładne codec IDs, topology i niedostępność fizycznego tonu. NVIDIA nie zostaje domyślnym głośnikiem.

## Trasa głośników i kolejne etapy

Standardowy finder przechodzi rzeczywiste Connection Lists, wykrywa cykle i błędne NID-y, ogranicza pracę, buduje uporządkowane ścieżki DAC → pośrednicy → pin z indeksami selectorów. Zachowuje kandydatów, klasyfikuje je i punktuje: internal Speaker 100, Headphone 60, Line Out 40, cyfrowe/pozostałe 10. Nie aktywuje pierwszego znalezionego DAC-a. Disconnected piny nie są trasą odtwarzania.

`hda_quirks.h` zawiera pustą strukturę dla dokładnych Vendor/Device oraz opcjonalnego subsystem ID i helper konserwatywnego gain <= -18 dB ze standardowych amp capabilities. Nie wykonuje żadnego zapisu. Dopiero prawdziwy raport i potwierdzony path pozwolą dobrać D0, stream/channel/format, wymagane selectory, wyłącznie obsługiwane pin-control, amp offset/steps/step size/mute i warunkowe EAPD. Nie zgaduje się 0 dB ani maksymalnego gain. Żaden Realtek coefficient/vendor verb nie jest obecnie wysyłany.

Do quirku potrzebne będą: pełny raport z dokładnym codec ID, PCI subsystem identity, realna ścieżka NID-ów i capabilities, wynik standardowego bezpiecznego tonu oraz udokumentowana sekwencja specyficzna dla tego sprzętu (np. źródło sterownika dla dokładnej tożsamości lub kontrolowany ślad). Sam napis „Realtek Audio” nie wystarcza.

Audio Input i Mic In są enumerowane, połączenia capture pozostają w grafie raportu. Nagrywanie, AGC i DMA capture nie są włączone. NVIDIA jest skanowana osobno, bez konfiguracji HDMI jako domyślnego output. To kolejne etapy po ręcznie potwierdzonych głośnikach.

## Zmienione pliki

- `sterowniki/dzwiek/hda_transport.h/.cpp`: rejestr, platform adapter, PCI/MMIO/DMA, inicjalizacja, IC/CORB/RIRB i recovery.
- `sterowniki/dzwiek/hda.cpp`, `hda_wewnetrzny.h`: istniejące odtwarzanie QEMU przez wspólny transport, bez duplikacji starego transportu.
- `sterowniki/dzwiek/hda_diag.h/.cpp`, `hda_diag_format.h`: skaner, publikacja, limity, dekodowanie, route finder i raport.
- `sterowniki/dzwiek/hda_quirks.h`: wyłączony exact-identity framework i bezpieczne obliczenie gain.
- `bws_hda_diag.h`, `bws_nowoczesne.cpp`: ABI 2, wybór kontrolera, metryki, walidacja rozmiarów.
- `programy/test_dzwieku/test_dzwieku.cpp`, `test_dzwieku_linker.ld`: selektor, stan, raport i jawny budżet BSS aplikacji 8 MiB; loader pozostaje bez zmian.
- `narzedzia/hda_host_model.h`, `test_hda_transport.cpp`, `test_hda_physical.py`: deterministyczny model wykonujący produkcyjny kod.
- `narzedzia/test_hda_diag.cpp`, `test_hda_bws_isolation.cpp`, `test_dzwieku_hda_bws.cpp`, `test_dzwieku_gui.cpp`, `test_dzwieku_repair.py`: aktualizacja testów schematu i rzeczywistych BWS/GUI.
- `Makefile`: obiekt transportu i test hostowy. Parametry QEMU/pointer pozostają bez zmian.
- Niniejszy dokument.

## Walidacja i obraz

Polecenie produkcyjne: `make -B -j2 all iso` — zakończone pomyślnie, bez warning/error kompilatora. Obrazu nie uruchomiono.

| Test | Wynik |
|---|---|
| Produkcyjny transport, PCI/init, ringi, skaner/topology | 44 scenariusze, 10 740 sprawdzeń PASS, ASan + UBSan |
| Dotychczasowy HDA diagnostic format/routes | 233 sprawdzenia, 0 błędów, ASan + UBSan |
| Dotychczasowy BWS isolation | 148 sprawdzeń, 0 błędów, ASan + UBSan |
| Rzeczywiste BWS 89/90 + produkcyjny scanner/transport | 8 scenariuszy, 2008 sprawdzeń PASS, ASan + UBSan |
| BUR loader, samodzielna regresja | 17/17 PASS, bez zmian loadera |
| Końcowy loader/package/GUI/report | wynik końcowego zestawu poniżej |

Model dwóch kontrolerów wykonał 2510 tur skanera; na wywołanie najwyżej 7 odczytów i 7 zapisów MMIO, najwyżej jeden wysłany verb. Deterministyczne maksimum modelowanego kosztu rejestrów/HPET wyniosło 700 ns. **Pełny rzeczywisty czas hosta** pod ASan: maksimum kroku 2 082 954 ns, maksimum czasu CPU 1 937 065 ns; rzeczywisty handler BWS 89 maksimum 4 417 788 ns. To pomiary hostowe, zależne od sprzętu i planowania procesów, nie deklaracja czasu wykonania na Acerze lub Windows QEMU. Produkcyjne instrumentowanie HPET obejmuje pełną pracę kroku, również RAM i topology.

Rozmiary ABI: snapshot 2 392 400 bajtów, widget 388, kontroler 152, stan skanu 72, trasa 56. Trzy zachowane bufory to 7 177 200 bajtów; szczyt z buforem BWS copy-out 9 569 600 bajtów. Alokacje są sprawdzane, bez wielomegabajtowych obiektów na stosie jądra. Budżet BSS aplikacji jest jawnie zwiększony w jej linkerze; loader i jego walidacja pozostają bez zmian.

Po buildzie porównano SHA256 31 chronionych plików z początkiem pracy: wszystkie bez zmian. Dotyczy m.in. VirtIO Input, input_device, loadera, syscalls, DMA allocator, USB/HID/xHCI, sterowników sieci/storage i runwindows. Diff Makefile obejmuje tylko HDA object/dependencies oraz target testowy; działająca konfiguracja GTK pointer nie została zmieniona.

Logi z wykonania: `.hda-test/physical/physical.log`, `legacy.log`, `bws.log`, `bur.log`, `production-build.log`, `final-repair.log`.

Manualna akceptacja QEMU pozostaje po stronie użytkownika: natychmiastowe okno, animacja spinnera, mysz, przeciąganie, zamknięcie, odświeżanie podczas skanu, kodek 1AF4:0012 i topology 01/02/03 Line Out, działający ton.

Manualna pierwsza akceptacja Acera: oba PCI HDA, precyzyjny stan init/ringów, exact codec ID/CAD/AFG/widgety, pin classification i zapis `/dane/diagnostyka/hda.txt`. Następnie analiza tego pliku przed implementacją lub finalizacją fizycznego tonu 440 Hz z konserwatywną głośnością. Nie ustala się dziś fizycznych NID-ów ani wymaganych vendor quirks.

Podstawa semantyki rejestrów i verbów: [Intel High Definition Audio Specification 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf). Weryfikacja zachowania ringów także względem [Linux HDA core controller](https://raw.githubusercontent.com/torvalds/linux/master/sound/hda/core/controller.c) oraz [Linux HDA verb definitions](https://raw.githubusercontent.com/torvalds/linux/master/include/sound/hda_verbs.h) i istniejących lokalnych źródeł modelu QEMU.
