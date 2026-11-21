# Współtworzenie Bursztyn OS

Bursztyn OS to niezależny, polski system operacyjny rozwijany od podstaw w C, C++ i asemblerze. Zapraszamy do zgłaszania błędów, testowania, poprawiania dokumentacji oraz rozwijania kodu.

Projekt jest eksperymentalny. Zmiany powinny zachowywać jego architekturę i być możliwe do sprawdzenia. Główne mechanizmy obejmują jądro Bursztyna, BWS, PZB, programy `.bur`, pakiety `.cebula` i BursztynUI.

## 1. Jak możesz pomóc

- Zgłaszaj błędy z krokami odtworzenia i logami.
- Testuj działanie w QEMU i na opisanych konfiguracjach sprzętowych.
- Poprawiaj dokumentację, instrukcje budowania i przykłady.
- Zgłaszaj poprawki kodu oraz testy regresyjne.
- Proponuj usprawnienia wydajności poparte pomiarami.

Większe zmiany architektury, nowe zależności, formaty plików i niezgodne zmiany ABI najpierw opisz w Issue. Drobne poprawki można zgłaszać bezpośrednio jako PR. Jeśli zaczynasz większą pracę nad istniejącym zgłoszeniem, napisz o tym w komentarzu, aby ograniczyć dublowanie prac.

## 2. Zgłaszanie błędów

Sprawdź najpierw, czy podobne zgłoszenie już istnieje. W nowym Issue podaj:

- wersję systemu, gałąź i identyfikator commita;
- kroki odtworzenia oraz zachowanie oczekiwane i rzeczywiste;
- konfigurację QEMU i firmware BIOS/UEFI albo model fizycznego komputera;
- przy problemie sprzętowym: identyfikatory urządzenia, jeśli są dostępne;
- odpowiedni fragment logu portu szeregowego, komunikat błędu lub zrzut ekranu;
- informację, czy błąd występuje również bez Twoich lokalnych zmian.

Przy zgłoszeniu wydajności opisz metodę pomiaru, konfigurację i porównywane rewizje. Nie publikuj danych osobowych, haseł ani prywatnych obrazów dysków.

## 3. Fork, gałąź, commit i pull request

Standardowa ścieżka współpracy to **fork → osobna gałąź → commity → pull request → przegląd → scalenie przez opiekuna projektu**.

1. Utwórz fork repozytorium `Programista-Art/BursztynOS` na swoim koncie GitHub.
2. Sklonuj swój fork. Adres znajdziesz pod przyciskiem **Code** w swoim repozytorium.
3. Zaktualizuj gałąź bazową forka względem projektu nadrzędnego. Jako podstawę wybierz aktualną domyślną gałąź repozytorium, chyba że w Issue uzgodniono inną.
4. Utwórz osobną gałąź dla jednej poprawki lub funkcji.
5. Wprowadź zmiany i wykonaj odpowiednie sprawdzenia.
6. Zapisz commity i wyślij swoją gałąź do forka.
7. Na GitHubie utwórz PR do uzgodnionej gałęzi projektu nadrzędnego.

Po przygotowaniu i zaktualizowaniu gałęzi bazowej przykładowe polecenia są następujące. Zastąp przykładową ścieżkę rzeczywistym plikiem, który zmieniasz:

```bash
git switch -c poprawka/opis-zmiany

# Wprowadź zmiany i wykonaj odpowiednie sprawdzenia.
git diff --check
git diff
git add sciezka/do/zmienionego_pliku.cpp
git diff --cached --check
git diff --cached
git commit -m "Napraw walidację rozmiaru bufora"
git push -u origin poprawka/opis-zmiany
```

W tym przykładzie `origin` powinien wskazywać na Twój fork. Nie potrzebujesz uprawnień zapisu do głównego repozytorium, aby przygotować taki PR. Kolejne commity wysłane na tę samą gałąź aktualizują otwarty pull request.

## 4. Jak przygotować czytelne commity i PR

Jeden PR powinien rozwiązywać jeden problem lub wprowadzać jedną spójną funkcję. Oddzielaj zmiany zachowania od masowego formatowania i niezwiązanych porządków.

Nazwij commit konkretnie, np. `Napraw utratę fokusu po zamknięciu menu`. Opis może być po polsku lub po angielsku. Przy nietrywialnej zmianie wyjaśnij również jej przyczynę.

W opisie PR podaj:

- problem i sposób jego rozwiązania;
- powiązane Issue, jeśli istnieje;
- zmienione podsystemy oraz wpływ na ABI, formaty i zgodność;
- wykonane polecenia testowe, środowisko i rzeczywiste wyniki;
- znane ograniczenia i przypadki, których nie sprawdzono;
- zrzuty ekranu przy zmianach interfejsu lub pomiary przy zmianach wydajności.

Jeśli praca nie jest gotowa do scalenia, otwórz **Draft PR**. Dyskusja i prośba o poprawki są normalną częścią przeglądu. O przyjęciu i scaleniu zmiany decyduje opiekun projektu; samo zgłoszenie PR nie gwarantuje jego przyjęcia.

## 5. Zasady pisania kodu

- Zachowuj styl najbliższego istniejącego kodu. Używaj czytelnych nazw i małych, spójnych funkcji.
- Dane aplikacji Ring 3, zawartość plików i dane urządzeń traktuj jako niezaufane. Korzystaj z istniejących mechanizmów walidacji i kontrolowanego kopiowania.
- Sprawdzaj granice buforów oraz przepełnienia przed dodawaniem offsetów, mnożeniem rozmiarów i alokacją pamięci.
- Jawnie określaj własność pamięci i zasobów. Obsługuj zwalnianie po błędzie, przerwaniu operacji i zakończeniu procesu. W C++ stosuj RAII tam, gdzie pasuje do istniejącego kodu.
- Przy zmianach BWS zachowuj opublikowane znaczenie numerów. Aktualizuj odpowiednie nagłówki, struktury ABI, wrappery, dokumentację i testy. Zmiany layoutu sprawdzaj przez `static_assert`, jeśli są częścią kontraktu binarnego.
- W asemblerze dokumentuj założenia ABI, wyrównanie stosu oraz rejestry i flagi zmieniane przez kod.
- Przy współbieżności określaj własność danych i kolejność blokad. `volatile` nie zastępuje operacji atomowych ani synchronizacji. Ograniczaj pracę w obsłudze przerwań i sekcjach krytycznych.
- Nie obchodź walidacji ani nie wyłączaj testów, aby ukryć błąd. Każdy wyjątek od istniejących zabezpieczeń wymaga uzasadnienia w PR.

Nie dodawaj do zmian wynikowych ISO, obrazów dysków, obiektów kompilacji, prywatnych logów ani innych wygenerowanych plików, chyba że konkretny scenariusz projektu wyraźnie ich wymaga.

## 6. Budowanie i sprawdzanie zmian

Korzystaj z aktualnych instrukcji w [README.md](README.md) oraz celów istniejących w bieżącym `Makefile`. Zapisz użyte wersje kompilatora i narzędzi. Nazwy testów i konfiguracje urządzeń mogą zmieniać się między rewizjami.

Zakres sprawdzeń dopasuj do zmiany:

- **Dokumentacja:** sprawdź treść, linki i podgląd Markdown.
- **Parsery, bufory i logika dostępna na hoście:** uruchom właściwe testy; użyj ASan/UBSan tam, gdzie istniejący sposób budowania na to pozwala.
- **Jądro, BWS, procesy i sterowniki:** zbuduj odpowiednie komponenty, wykonaj dostępne testy podsystemu i sprawdź odpowiednią ścieżkę działania w QEMU.
- **GUI:** sprawdź zmieniony scenariusz oraz powiązane zachowania, np. fokus, obsługę klawiatury lub zamknięcie okna.
- **Zapis na dysk i instalator:** pracuj na jednorazowych obrazach testowych, oddzielonych od prywatnych danych.

Przy poprawianiu błędu dodaj test regresyjny, jeśli można go sensownie przygotować. Dla wrażliwych interfejsów sprawdź również odpowiednie przypadki odmowy: błędny wskaźnik, rozmiar, uprawnienia, właściciel lub nieaktualny uchwyt.

Raportuj tylko wykonane sprawdzenia. Jeśli nie możesz uruchomić testu, napisz to w PR. Test hostowy, uruchomienie QEMU i próba na sprzęcie fizycznym potwierdzają różne rzeczy.

## 7. Licencja i pochodzenie wkładu

Autorskie elementy kodu projektu są udostępniane na [PolyForm Noncommercial License 1.0.0](LICENSE.md), o ile dla konkretnego elementu nie wskazano inaczej. Jest to licencja source-available z ograniczeniem do dozwolonych celów niekomercyjnych.

Przesyłając PR, potwierdzasz, że masz prawo udostępnić proponowany wkład. Własne poprawki do kodu objętego PolyForm przekazujesz na tych samych warunkach, chyba że wcześniej uzgodniono odrębną umowę. Zachowujesz autorstwo i przysługujące Ci prawa; samo zgłoszenie PR nie stanowi przeniesienia praw autorskich.

Kod, biblioteki, grafiki i inne materiały osób trzecich wymagają podania źródła, autora i warunków licencji oraz sprawdzenia zgodności z planowanym sposobem ich wykorzystania. Zachowuj wymagane informacje licencyjne. Dodanie pliku do repozytorium nie zmienia automatycznie jego dotychczasowej licencji.

Samo przyjęcie wkładu na PolyForm Noncommercial nie daje opiekunowi automatycznej zgody na komercyjne wykorzystanie cudzych poprawek ani zmianę ich licencji. Jeżeli taki zakres praw będzie potrzebny, wymaga odrębnego uzgodnienia z odpowiednim właścicielem praw, np. umowy CLA określającej go wprost.

## 8. Nazwa i logo podczas współpracy

Stosuj [zasady używania nazwy i logo](TRADEMARKS.md). Techniczne forki służące wyłącznie przygotowaniu i przeglądowi PR mogą zachować nazwę repozytorium i niezmienione oficjalne logo na potrzeby tej współpracy. Nie oznacza to zatwierdzenia forka ani jego wydań przez autora Bursztyn OS.

Przy publikowaniu własnych zmodyfikowanych wydań systemu stosuj własną nazwę i logo, chyba że uzyskano odrębną zgodę. Zachowuj informacje o pochodzeniu kodu i jego autorach.

## 9. Narzędzia AI

Możesz korzystać z narzędzi AI, ale odpowiadasz za zgłoszony kod, jego pochodzenie i weryfikację. Przy istotnym udziale AI opisz jego zakres w PR. Przed zgłoszeniem przeczytaj i sprawdź proponowane zmiany; wyniki testów muszą pochodzić z rzeczywistych uruchomień.

## 10. Bezpieczeństwo i komunikacja

Podatności umożliwiające naruszenie bezpieczeństwa zgłaszaj prywatnie na [programista.art@gmail.com](mailto:programista.art@gmail.com). Jeśli repozytorium udostępnia instrukcję `SECURITY.md` lub prywatne zgłaszanie podatności w GitHubie, kieruj się wskazaną tam procedurą. Daj opiekunowi możliwość analizy problemu przed publikacją szczegółów umożliwiających wykorzystanie podatności.

W dyskusjach oceniaj rozwiązania techniczne, podawaj argumenty i szanuj innych uczestników. Zgłoszenia błędów i uwagi do kodu są mile widziane także bez gotowej poprawki.

## Lista przed wysłaniem PR

- [ ] Zmiana ma określony cel i ograniczony zakres.
- [ ] Mam prawo udostępnić kod i wskazałem źródła materiałów osób trzecich.
- [ ] Sprawdziłem zmiany i opisałem wykonane testy oraz ich ograniczenia.
- [ ] Zaktualizowałem dokumentację lub ABI, jeśli zmiana tego wymaga.
- [ ] Nie dołączyłem prywatnych danych ani przypadkowych plików wynikowych.
- [ ] Podałem powiązane Issue i wpływ zmiany na użytkownika, jeśli dotyczy.
