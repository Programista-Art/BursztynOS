/*
 * Aplikacja: Powłoka Bursztynowa (bsh)
 * Poziom: Ring 3 (Przestrzeń Użytkownika)
 * Wersja zoptymalizowana: Korzysta z biblioteki bursztyn_gui.h oraz 
 * zawiera pełną obsługę sieci i plików z polskimi znakami.
 */

#include "bursztyn_gui.h"



struct NaglowekBur {
    uint8_t  magia[4];            
    uint64_t punkt_wejscia;       
    uint64_t tekst_przesuniecie;  
    uint64_t tekst_rozmiar;       
    uint64_t tekst_wirtualny;     
    uint64_t dane_przesuniecie;   
    uint64_t dane_rozmiar;        
    uint64_t dane_wirtualny;      
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        4096, 32768, 0x601000,
        36864, 131072, 0x609000
    };
}
extern "C" void bws_gui_odswiez();
// void bws_dzwiek_test(uint32_t czestotliwosc, uint32_t czas);


// Funkcje obsługi plików z Jądra
bool wylistuj_katalog(const char* sciezka, char* bufor, uint32_t max_dlugosc) { return bws_wywolaj(6, (uint64_t)sciezka, (uint64_t)bufor, max_dlugosc) != 0; }
bool usun_twor(const char* sciezka) { return bws_wywolaj(7, (uint64_t)sciezka) != 0; }
bool zmien_nazwe_tworu(const char* sciezka, const char* nowa_nazwa) { return bws_wywolaj(8, (uint64_t)sciezka, (uint64_t)nowa_nazwa) != 0; }

int strlen(const char* str) {
    int len = 0; while (str[len] != '\0') len++; return len;
}

bool strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 == *(const unsigned char*)s2;
}

bool zaczyna_sie_od(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str != *prefix) return false;
        str++; prefix++;
    }
    return true;
}

bool strncmp(const char* s1, const char* s2, int n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    return n == 0 || *(const unsigned char*)s1 == *(const unsigned char*)s2;
}

static inline uint64_t pobierz_cykle() {
    uint32_t lo, hi;
    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void pobierz_linie(char* bufor, int max_dlugosc) {
    int pozycja = 0;
    while (true) {
        char c = pobierz_znak(); 
        if (c == 0) continue; 

        if (c == '\n' || c == '\r') {
            bufor[pozycja] = '\0';
            break;
        } 
        else if (c == '\b') {
            if (pozycja > 0) { 
                pozycja--; 
                // Nowy sterownik terminala w grafika.cpp radzi sobie perfekcyjnie z samym \b
                wypisz("\b"); 
            }
        } 
        else if (pozycja < max_dlugosc - 1) {
            bufor[pozycja++] = c;
            char tmp[2] = {c, '\0'}; wypisz(tmp);
        }
    }
}

void formatuj_sciezke(const char* wejscie, char* wyjscie) {
    if (wejscie[0] == '/') {
        int i = 0; while (wejscie[i] != '\0') { wyjscie[i] = wejscie[i]; i++; } wyjscie[i] = '\0';
    } else {
        wyjscie[0] = '/';
        int i = 0; while (wejscie[i] != '\0') { wyjscie[i+1] = wejscie[i]; i++; } wyjscie[i+1] = '\0';
    }
}

void int_do_str(int wartosc, char* bufor) {
    if (wartosc == 0) { bufor[0] = '0'; bufor[1] = '\0'; return; }
    int i = 0; char temp[16];
    while (wartosc > 0) { temp[i++] = (wartosc % 10) + '0'; wartosc /= 10; }
    int j = 0; while (i > 0) { bufor[j++] = temp[--i]; }
    bufor[j] = '\0';
}

char historia[5][128];
int hist_ilosc = 0;

extern "C" __attribute__((noreturn)) void _start() {
    // Oddajemy kontrolę nad myszą menedżerowi okien
    gui_ustaw_przejecie_myszy(false);

    wypisz("\n");
    wypisz("==================================================\n");
    wypisz(" Powłoka Bursztynowa v2.1 (Zintegrowana z GUI)\n");
    wypisz(" Wpisz 'pomoc', aby zobaczyć listę poleceń.\n");
    wypisz("==================================================\n");

    char bufor_komendy[128];

    while (true) {
        wypisz("\npowłoka> ");
        pobierz_linie(bufor_komendy, 128);
        wypisz("\n");

        if (strlen(bufor_komendy) == 0) continue;

        for(int i = 4; i > 0; i--) { for(int j=0; j<128; j++) historia[i][j] = historia[i-1][j]; }
        for(int j=0; j<128; j++) historia[0][j] = bufor_komendy[j];
        if (hist_ilosc < 5) hist_ilosc++;

        if (strcmp(bufor_komendy, "pomoc")) {
            wypisz("--- KATEGORIA: APLIKACJE GUI ---\n");
            wypisz("  notatnik      - uruchamia graficzny edytor tekstu\n");
            wypisz("  kalkulator    - uruchamia kalkulator systemowy\n");
            wypisz("  pulpit        - wraca do Menedżera Okien\n");
            wypisz("--- KATEGORIA: SYSTEM ---\n");
            wypisz("  pomoc         - wyświetla ten ekran pomocy\n");
            wypisz("  system        - wyświetla parametry sprzętowe i systemowe\n");
            wypisz("  wersja        - wyświetla wersję powłoki i systemu\n");
            wypisz("  kto           - wyświetla zalogowanego użytkownika\n");
            wypisz("  pci           - wyświetla urządzenia na płycie głównej (magistrala PCI)\n");
            wypisz("  uruchom [plik]- uruchamia aplikację (np. uruchom /programy/kalk.bur)\n");
            wypisz("  historia      - wyświetla historię 5 ostatnich poleceń\n");
            wypisz("  czysc         - czyści ekran terminala\n");
            wypisz("  czas          - wyświetla aktualną godzinę z zegara RTC\n");
            wypisz("  wyjdz         - zamyka terminal i wraca na Pulpit\n");
            wypisz("--- KATEGORIA: SIEĆ (INTERNET) ---\n");
            wypisz("  ping [Cel]    - wysyła sygnał PING (np. ping 10.0.2.2 lub ping google.com)\n");
            wypisz("  pobierz       - pobiera plik z sieci (np. pobierz example.com / /test.html)\n");
            wypisz("--- KATEGORIA: PLIKI ---\n");
            wypisz("  utworz        - tworzy nowy, pusty plik lub katalog\n");
            wypisz("  zapisz        - zapisuje wprowadzony tekst do pliku\n");
            wypisz("  czytaj [plik] - wyświetla zawartość wskazanego pliku\n");
            wypisz("  pliki [kat]   - wyświetla listę plików w katalogu (np. pliki /programy)\n");
            wypisz("  usun [sciezka]- trwale usuwa plik lub katalog\n");
            wypisz("  zmien_nazwe   - uruchamia kreator zmiany nazwy pliku\n");
            wypisz("  gdzie         - wyświetla ścieżkę obecnego katalogu\n");
            wypisz("--- KATEGORIA: ROZRYWKA ---\n");
            wypisz("  pisz [txt]    - wypisuje podany tekst na ekran\n");
            wypisz("  cytat         - wczytuje i wyświetla cytaty z pliku\n");
            wypisz("  losuj         - rzuca wirtualną kością (wynik 1-6)\n");
            wypisz("  dzwiek        - test dźwięku karta hda intel)\n");
        }
        else if (strcmp(bufor_komendy, "notatnik")) {
            wypisz("Uruchamianie Notatnika...\n");
            bws_wywolaj(10, (uint64_t)"/programy/notatnik.cebula/notatnik.bur");
        }
        else if (strcmp(bufor_komendy, "kalkulator")) {
            wypisz("Uruchamianie Kalkulatora...\n");
            bws_wywolaj(10, (uint64_t)"/programy/kalkulator.cebula/kalkulator.bur");
        }
        else if (strcmp(bufor_komendy, "pulpit") || strcmp(bufor_komendy, "wyjdz") || strcmp(bufor_komendy, "exit")) {
            wypisz("Powrót do Menedżera Okien...\n");
            bws_wywolaj(10, (uint64_t)"/menedzer_okien.bur");
        }
        else if (strcmp(bufor_komendy, "ping")) {
            wypisz("Składnia polecenia: ping [adres IP lub domena] (np. ping google.com)\n");
        }
        else if (zaczyna_sie_od(bufor_komendy, "ping ")) {
            char* cel = &bufor_komendy[5];
            int ip[4] = {0,0,0,0};
            bool to_ip = true;
            
            for (int i = 0; cel[i] != '\0'; i++) {
                if ((cel[i] < '0' || cel[i] > '9') && cel[i] != '.') { to_ip = false; break; }
            }
            
            if (to_ip) {
                int czesc = 0; int i = 0;
                while(cel[i] != '\0' && czesc < 4) {
                    if (cel[i] == '.') { czesc++; i++; continue; }
                    if (cel[i] >= '0' && cel[i] <= '9') ip[czesc] = ip[czesc] * 10 + (cel[i] - '0');
                    i++;
                }
                wypisz("Wysyłanie sygnału PING na podany adres IP...\n");
                bws_wywolaj(11, ip[0], ip[1], ip[2], ip[3]);
            } else {
                wypisz("Rozwiązywanie domeny DNS: "); wypisz(cel); wypisz("...\n");
                uint8_t resolved[4] = {0, 0, 0, 0};
                if (bws_wywolaj(12, (uint64_t)cel, (uint64_t)resolved)) {
                    wypisz("Sukces! Znaleziono zmapowany adres IP: ");
                    char buf[16]; int_do_str(resolved[0], buf); wypisz(buf); wypisz(".");
                    int_do_str(resolved[1], buf); wypisz(buf); wypisz(".");
                    int_do_str(resolved[2], buf); wypisz(buf); wypisz(".");
                    int_do_str(resolved[3], buf); wypisz(buf); wypisz("\n");
                    wypisz("Wysyłanie sygnału PING (ICMP)...\n");
                    bws_wywolaj(11, resolved[0], resolved[1], resolved[2], resolved[3]);
                } else { wypisz("Błąd: Nie udało się rozwiązać domeny DNS.\n"); }
            }
        }
        else if (zaczyna_sie_od(bufor_komendy, "pobierz ")) {
            char domena[64] = {0}; char sciezka_http[64] = {0}; char sciezka_dyskowa[64] = {0};
            int i = 8, j = 0;
            
            while (bufor_komendy[i] != ' ' && bufor_komendy[i] != '\0' && j < 63) domena[j++] = bufor_komendy[i++];
            domena[j] = '\0';
            if (bufor_komendy[i] == ' ') { i++; }
            j = 0;
            
            while (bufor_komendy[i] != ' ' && bufor_komendy[i] != '\0' && j < 63) sciezka_http[j++] = bufor_komendy[i++];
            sciezka_http[j] = '\0';
            if (bufor_komendy[i] == ' ') { i++; }
            j = 0;
            
            while (bufor_komendy[i] != ' ' && bufor_komendy[i] != '\0' && j < 63) sciezka_dyskowa[j++] = bufor_komendy[i++];
            sciezka_dyskowa[j] = '\0';

            if (domena[0] == '\0' || sciezka_http[0] == '\0' || sciezka_dyskowa[0] == '\0') {
                wypisz("Składnia: pobierz [domena] [sciezka_na_serwerze] [zapisz_jako]\n");
                wypisz("Przykład: pobierz example.com / /test.html\n");
            } else {
                wypisz("1. Rozwiązywanie domeny DNS: "); wypisz(domena); wypisz("...\n");
                uint8_t resolved_ip[4] = {0, 0, 0, 0};
                
                if (bws_wywolaj(12, (uint64_t)domena, (uint64_t)resolved_ip)) {
                    wypisz("2. Adres IP znaleziony. Nawiązywanie sesji TCP i pobieranie danych (HTTP)...\n");
                    char bezp_sciezka[64]; formatuj_sciezke(sciezka_dyskowa, bezp_sciezka);
                    
                    uint64_t wynik = bws_wywolaj(13, (uint64_t)resolved_ip, (uint64_t)domena, (uint64_t)sciezka_http, (uint64_t)bezp_sciezka);
                    
                    if (wynik == 1) {
                        wypisz("3. SUKCES! Plik został pobrany z Internetu i utrwalony na dysku jako: "); wypisz(bezp_sciezka); wypisz("\n");
                        wypisz("Wpisz: czytaj "); wypisz(bezp_sciezka); wypisz(" aby zobaczyć jego zawartość!\n");
                    } else {
                        wypisz("BŁĄD: Serwer nie odpowiedział, zerwał połączenie, lub brak uprawnień dyskowych.\n");
                    }
                } else { wypisz("BŁĄD: Nie udało się rozwiązać domeny DNS.\n"); }
            }
        }
        else if (strcmp(bufor_komendy, "czas")) {
            char bufor_czasu[32]; bws_wywolaj(9, (uint64_t)bufor_czasu);
            wypisz("Aktualny czas z zegara RTC to: "); wypisz(bufor_czasu); wypisz("\n");
        }
        else if (strcmp(bufor_komendy, "system")) {
            wypisz("OS: Bursztyn OS x86_64\nJądro: Monolityczne, VMM Paging 4-lvl, Własne API GUI\n");
            wypisz("Sieć: Zintegrowany klient DHCP, ARP, ICMP oraz Klient HTTP (TCP/DNS)\n");
        }
        else if (strcmp(bufor_komendy, "wersja")) {
            wypisz("Powłoka Bursztynowa v2.2\n");
        }
        else if (strcmp(bufor_komendy, "kto")) {
            wypisz("Zalogowano jako: Administrator Systemu (Ring 3)\n");
        }
        else if (strcmp(bufor_komendy, "pci")) {
            char buf_pci[2048]; for(int i = 0; i < 2048; i++) buf_pci[i] = 0; 
            if (czytaj_plik("/logi/pci.txt", buf_pci, 2047)) {
                wypisz("--- Raport PCI (Zapisany przez Ring 0) ---\n"); wypisz(buf_pci); wypisz("\n");
            } else wypisz("Błąd: Brak raportu PCI w systemie plików.\n");
        }
        else if (zaczyna_sie_od(bufor_komendy, "uruchom ")) {
            char sciezka_pliku[64]; int i = 8, j = 0;
            while (bufor_komendy[i] != '\0' && bufor_komendy[i] != ' ' && j < 63) sciezka_pliku[j++] = bufor_komendy[i++];
            sciezka_pliku[j] = '\0';
            wypisz("Uruchamianie procesu: "); wypisz(sciezka_pliku); wypisz("...\n");
            if (bws_wywolaj(10, (uint64_t)sciezka_pliku) == 0) wypisz("Błąd: Nie udało się załadować programu z dysku.\n");
        }
        else if (strcmp(bufor_komendy, "gdzie")) { wypisz("Obecna lokalizacja: / (Korzeń Systemu Plików)\n"); }
        else if (strcmp(bufor_komendy, "historia")) {
            for(int i = 0; i < hist_ilosc; i++) {
                char numer[4]; int_do_str(i + 1, numer); wypisz(numer); wypisz(". "); wypisz(historia[i]); wypisz("\n");
            }
        }
        else if (strcmp(bufor_komendy, "czysc")) { for(int i = 0; i < 40; i++) wypisz("\n"); }
        else if (strcmp(bufor_komendy, "losuj")) {
            uint64_t cykle = pobierz_cykle(); int kosc = (cykle % 6) + 1;
            char wynik_str[8]; int_do_str(kosc, wynik_str);
            wypisz("Rzucasz kośćmi... Wypadło: "); wypisz(wynik_str); wypisz("!\n");
        }
        else if (strcmp(bufor_komendy, "cytat")) {
            char buf[512]; for(int i=0; i<512; i++) buf[i] = 0; 
            if (czytaj_plik("/cytaty.txt", buf, 511)) { wypisz("--- Cytaty z pliku ---\n"); wypisz(buf); wypisz("\n"); } 
            else {
                if (utworz("/cytaty.txt")) {
                    const char* domyslne = "1. U mnie działa.\n2. Brak błędu to też błąd.\n";
                    zapisz_plik("/cytaty.txt", domyslne, strlen(domyslne)); wypisz("Plik utworzony!\n");
                }
            }
        }
        else if (strncmp(bufor_komendy, "pliki", 5)) {
            char sciezka[64];
            if (bufor_komendy[5] == ' ' && bufor_komendy[6] != '\0') formatuj_sciezke(&bufor_komendy[6], sciezka);
            else { sciezka[0] = '/'; sciezka[1] = '\0'; }

            char buf[512]; for(int i=0; i<512; i++) buf[i] = 0;
            if (wylistuj_katalog(sciezka, buf, 511)) { wypisz("Zawartość źródła ("); wypisz(sciezka); wypisz("):\n"); wypisz(buf); } 
            else wypisz("Błąd: Katalog nie istnieje lub jest pusty.\n");
        }
        else if (strncmp(bufor_komendy, "czytaj ", 7)) {
            char sciezka[64]; formatuj_sciezke(&bufor_komendy[7], sciezka);
            
            char duzy_bufor[4096]; 
            for(int i=0; i<4096; i++) duzy_bufor[i] = 0; 
            
            if (czytaj_plik(sciezka, duzy_bufor, 4095)) { 
                wypisz("--- "); wypisz(sciezka); wypisz(" ---\n"); 
                wypisz(duzy_bufor); 
                wypisz("\n"); 
            } 
            else {
                wypisz("Błąd odczytu: Brak pliku lub plik pusty.\n");
            }
        }
        else if (strcmp(bufor_komendy, "utworz")) {
            wypisz("Nazwa nowego pliku/folderu: "); char sciezka[64]; pobierz_linie(sciezka, 64); wypisz("\n");
            char bezp[64]; formatuj_sciezke(sciezka, bezp);
            if (utworz(bezp)) { wypisz("Założono: "); wypisz(bezp); wypisz("\n"); } else wypisz("Błąd: Zablokowane lub błędna ścieżka.\n");
        }
        else if (strcmp(bufor_komendy, "zapisz")) {
            wypisz("Plik docelowy: "); char sciezka[64]; pobierz_linie(sciezka, 64); wypisz("\n");
            char bezp[64]; formatuj_sciezke(sciezka, bezp);
            wypisz("Tekst: "); char dane[128]; pobierz_linie(dane, 128); wypisz("\n");
            if (zapisz_plik(bezp, dane, strlen(dane))) wypisz("Zapisano!\n"); else wypisz("Błąd zapisu.\n");
        }
        else if (strncmp(bufor_komendy, "usun ", 5)) {
            char sciezka[64]; formatuj_sciezke(&bufor_komendy[5], sciezka);
            if (usun_twor(sciezka)) { wypisz("Usunięto obiekt: "); wypisz(sciezka); wypisz("\n"); } 
            else wypisz("Błąd: Nie można usunąć.\n");
        }
        else if (strcmp(bufor_komendy, "zmien_nazwe")) {
            wypisz("Ścieżka do zmiany: "); char stara[64]; pobierz_linie(stara, 64); wypisz("\n"); char bezp_stara[64]; formatuj_sciezke(stara, bezp_stara);
            wypisz("Nowa nazwa: "); char nowa[64]; pobierz_linie(nowa, 64); wypisz("\n");
            if (zmien_nazwe_tworu(bezp_stara, nowa)) wypisz("Zmieniono nazwę.\n"); else wypisz("Błąd zmiany nazwy.\n");
        }
         else if (strcmp(bufor_komendy, "dzwiek")) {
            // Używamy Twojej funkcji wypisz!
            wypisz("Odtwarzanie dzwieku testowego HDA (880 Hz)...\n");
            
            // Wywołanie systemowe do Jądra
            bws_dzwiek_test(880, 500);
        }
        else if (strncmp(bufor_komendy, "pisz ", 5)) { wypisz(&bufor_komendy[5]); wypisz("\n"); }
        else { wypisz("Nieznane polecenie: '"); wypisz(bufor_komendy); wypisz("'. Wpisz 'pomoc'.\n"); }
    }
}
