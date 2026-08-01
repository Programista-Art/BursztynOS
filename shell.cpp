/*
 * Aplikacja: Powłoka Bursztynowa (bsh)
 * Poziom: Ring 3 (Przestrzeń Użytkownika)
 */

#include <stdint.h>
#include <stdbool.h>

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

extern "C" void _start();

extern "C" __attribute__((section(".naglowek"), used))
const struct NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    (uint64_t)&_start,
    4096,  8192, 0x401000, 
    12288, 8192, 0x403000  
};

uint64_t bws_wywolaj(uint64_t nr_funkcji, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0) {
    register uint64_t r8 asm("r8") = nr_funkcji;
    register uint64_t r9 asm("r9") = arg1;
    register uint64_t r10 asm("r10") = arg2;
    register uint64_t r12 asm("r12") = arg3;
    register uint64_t r13 asm("r13") = arg4;
    register uint64_t rax asm("rax");

    asm volatile (
        "syscall"
        : "=a" (rax)
        : "r" (r8), "r" (r9), "r" (r10), "r" (r12), "r" (r13)
        : "rcx", "r11", "memory"
    );
    return rax;
}

void print(const char* tekst) { bws_wywolaj(1, (uint64_t)tekst); }
bool utworz(const char* plik) { return bws_wywolaj(2, (uint64_t)plik) != 0; }
bool zapisz_plik(const char* plik, const char* dane, uint32_t dlugosc) { return bws_wywolaj(3, (uint64_t)plik, (uint64_t)dane, dlugosc) != 0; }
char getch() { return (char)bws_wywolaj(4); }
bool czytaj_plik(const char* plik, char* bufor, uint32_t max_dlugosc) { return bws_wywolaj(5, (uint64_t)plik, (uint64_t)bufor, max_dlugosc) != 0; }
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
        char c = getch();
        if (c == 0) continue; 

        if (c == '\n' || c == '\r') {
            bufor[pozycja] = '\0';
            break;
        } 
        else if (c == '\b') {
            if (pozycja > 0) { pozycja--; print("\b \b"); }
        } 
        else if (pozycja < max_dlugosc - 1) {
            bufor[pozycja++] = c;
            char tmp[2] = {c, '\0'}; print(tmp);
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

extern "C" void _start() {
    print("\n");
    print("==================================================\n");
    print(" Powłoka Bursztynowa v1.8 (Ring 3 - DNS Klient)\n");
    print(" Wpisz 'pomoc', aby zobaczyć listę poleceń.\n");
    print("==================================================\n");

    char bufor_komendy[128];

    while (true) {
        print("\npowłoka> ");
        pobierz_linie(bufor_komendy, 128);
        print("\n");

        if (strlen(bufor_komendy) == 0) continue;

        for(int i = 4; i > 0; i--) { for(int j=0; j<128; j++) historia[i][j] = historia[i-1][j]; }
        for(int j=0; j<128; j++) historia[0][j] = bufor_komendy[j];
        if (hist_ilosc < 5) hist_ilosc++;

        if (strcmp(bufor_komendy, "pomoc")) {
            print("--- KATEGORIA: SYSTEM ---\n");
            print("  pomoc         - wyświetla ten ekran pomocy\n");
            print("  system        - wyświetla parametry sprzętowe i systemowe\n");
            print("  wersja        - wyświetla wersję powłoki i systemu\n");
            print("  kto           - wyświetla zalogowanego użytkownika\n");
            print("  pci           - wyświetla urządzenia na płycie głównej (magistrala PCI)\n");
            print("  uruchom [plik]- uruchamia aplikację (np. uruchom /programy/kalk.bur)\n");
            print("  historia      - wyświetla historię 5 ostatnich poleceń\n");
            print("  czysc         - czyści ekran terminala\n");
            print("  czas          - wyświetla aktualną godzinę z zegara RTC\n");
            print("  ping [Cel]    - wysyła sygnał PING (np. ping 10.0.2.2 lub ping google.com)\n");
            print("--- KATEGORIA: PLIKI ---\n");
            print("  utworz        - tworzy nowy, pusty plik lub katalog\n");
            print("  zapisz        - zapisuje wprowadzony tekst do pliku\n");
            print("  czytaj [plik] - wyświetla zawartość wskazanego pliku\n");
            print("  pliki [kat]   - wyświetla listę plików w katalogu (np. pliki /programy)\n");
            print("  usun [sciezka]- trwale usuwa plik lub katalog\n");
            print("  zmien_nazwe   - uruchamia kreator zmiany nazwy pliku\n");
            print("  gdzie         - wyświetla ścieżkę obecnego katalogu\n");
            print("--- KATEGORIA: ROZRYWKA ---\n");
            print("  pisz [txt]    - wypisuje podany tekst na ekran\n");
            print("  cytat         - wczytuje i wyświetla cytaty z pliku\n");
            print("  losuj         - rzuca wirtualną kością (wynik 1-6)\n");
        }
        else if (strcmp(bufor_komendy, "ping")) {
            print("Składnia polecenia: ping [adres IP lub domena] (np. ping google.com)\n");
        }
        else if (zaczyna_sie_od(bufor_komendy, "ping ")) {
            char* cel = &bufor_komendy[5];
            int ip[4] = {0,0,0,0};
            bool to_ip = true;
            
            // Jeśli w haśle występują litery (a nie tylko cyfry/kropki), wzywamy DNS!
            for (int i = 0; cel[i] != '\0'; i++) {
                if ((cel[i] < '0' || cel[i] > '9') && cel[i] != '.') {
                    to_ip = false; break;
                }
            }
            
            if (to_ip) {
                int czesc = 0; int i = 0;
                while(cel[i] != '\0' && czesc < 4) {
                    if (cel[i] == '.') { czesc++; i++; continue; }
                    if (cel[i] >= '0' && cel[i] <= '9') ip[czesc] = ip[czesc] * 10 + (cel[i] - '0');
                    i++;
                }
                print("Wysyłanie sygnału PING na podany adres IP...\n");
                bws_wywolaj(11, ip[0], ip[1], ip[2], ip[3]);
            } else {
                print("Rozwiązywanie domeny DNS: "); print(cel); print("...\n");
                uint8_t resolved[4] = {0, 0, 0, 0};
                
                // Wywołanie autorskiego BWS nr 12 - Zapytanie DNS do Jądra
                if (bws_wywolaj(12, (uint64_t)cel, (uint64_t)resolved)) {
                    print("Sukces! Znaleziono zmapowany adres IP: ");
                    char buf[16]; 
                    int_do_str(resolved[0], buf); print(buf); print(".");
                    int_do_str(resolved[1], buf); print(buf); print(".");
                    int_do_str(resolved[2], buf); print(buf); print(".");
                    int_do_str(resolved[3], buf); print(buf); print("\n");
                    
                    print("Wysyłanie sygnału PING (ICMP)...\n");
                    bws_wywolaj(11, resolved[0], resolved[1], resolved[2], resolved[3]);
                } else {
                    print("Błąd: Nie udało się rozwiązać domeny DNS (Timeout lub brak serwera).\n");
                }
            }
        }
        else if (strcmp(bufor_komendy, "czas")) {
            char bufor_czasu[32]; bws_wywolaj(9, (uint64_t)bufor_czasu);
            print("Aktualny czas z zegara RTC to: "); print(bufor_czasu); print("\n");
        }
        else if (strcmp(bufor_komendy, "system")) {
            print("OS: Bursztyn OS x86_64\nJądro: Monolityczne, VMM Paging 4-lvl\n");
            print("Sieć: Zintegrowany klient DHCP, ARP, ICMP oraz klient DNS (UDP)\n");
        }
        else if (strcmp(bufor_komendy, "wersja")) {
            print("Powłoka Bursztynowa v1.8 (Build: DNS Resolver)\n");
        }
        else if (strcmp(bufor_komendy, "kto")) {
            print("Zalogowano jako: Administrator Systemu (Ring 3)\n");
        }
        else if (strcmp(bufor_komendy, "pci")) {
            char buf_pci[2048]; for(int i = 0; i < 2048; i++) buf_pci[i] = 0; 
            if (czytaj_plik("/logi/pci.txt", buf_pci, 2047)) {
                print("--- Raport PCI (Zapisany przez Ring 0) ---\n"); print(buf_pci); print("\n");
            } else print("Błąd: Brak raportu PCI w systemie plików.\n");
        }
        else if (zaczyna_sie_od(bufor_komendy, "uruchom ")) {
            char sciezka_pliku[64]; int i = 8, j = 0;
            while (bufor_komendy[i] != '\0' && bufor_komendy[i] != ' ' && j < 63) sciezka_pliku[j++] = bufor_komendy[i++];
            sciezka_pliku[j] = '\0';
            print("Uruchamianie procesu: "); print(sciezka_pliku); print("...\n");
            if (bws_wywolaj(10, (uint64_t)sciezka_pliku) == 0) print("Błąd: Nie udało się załadować programu z dysku.\n");
        }
        else if (strcmp(bufor_komendy, "gdzie")) { print("Obecna lokalizacja: / (Korzeń Systemu Plików)\n"); }
        else if (strcmp(bufor_komendy, "historia")) {
            for(int i = 0; i < hist_ilosc; i++) {
                char numer[4]; int_do_str(i + 1, numer); print(numer); print(". "); print(historia[i]); print("\n");
            }
        }
        else if (strcmp(bufor_komendy, "czysc")) { for(int i = 0; i < 40; i++) print("\n"); }
        else if (strcmp(bufor_komendy, "losuj")) {
            uint64_t cykle = pobierz_cykle(); int kosc = (cykle % 6) + 1;
            char wynik_str[8]; int_do_str(kosc, wynik_str);
            print("Rzucasz kośćmi... Wypadło: "); print(wynik_str); print("!\n");
        }
        else if (strcmp(bufor_komendy, "cytat")) {
            char buf[512]; for(int i=0; i<512; i++) buf[i] = 0; 
            if (czytaj_plik("/cytaty.txt", buf, 511)) { print("--- Cytaty z pliku ---\n"); print(buf); print("\n"); } 
            else {
                if (utworz("/cytaty.txt")) {
                    const char* domyslne = "1. U mnie działa.\n2. Brak błędu to też błąd.\n";
                    zapisz_plik("/cytaty.txt", domyslne, strlen(domyslne)); print("Plik utworzony!\n");
                }
            }
        }
        else if (strncmp(bufor_komendy, "pliki", 5)) {
            char sciezka[64];
            if (bufor_komendy[5] == ' ' && bufor_komendy[6] != '\0') formatuj_sciezke(&bufor_komendy[6], sciezka);
            else { sciezka[0] = '/'; sciezka[1] = '\0'; }

            char buf[512]; for(int i=0; i<512; i++) buf[i] = 0;
            if (wylistuj_katalog(sciezka, buf, 511)) { print("Zawartość źródła ("); print(sciezka); print("):\n"); print(buf); } 
            else print("Błąd: Katalog nie istnieje lub jest pusty.\n");
        }
        else if (strncmp(bufor_komendy, "czytaj ", 7)) {
            char sciezka[64]; formatuj_sciezke(&bufor_komendy[7], sciezka);
            char buf[512]; for(int i=0; i<512; i++) buf[i] = 0; 
            if (czytaj_plik(sciezka, buf, 511)) { print("--- "); print(sciezka); print(" ---\n"); print(buf); print("\n"); } 
            else print("Błąd odczytu: Brak pliku.\n");
        }
        else if (strcmp(bufor_komendy, "utworz")) {
            print("Nazwa nowego pliku/folderu: "); char sciezka[64]; pobierz_linie(sciezka, 64); print("\n");
            char bezp[64]; formatuj_sciezke(sciezka, bezp);
            if (utworz(bezp)) { print("Założono: "); print(bezp); print("\n"); } else print("Błąd: Zablokowane lub błędna ścieżka.\n");
        }
        else if (strcmp(bufor_komendy, "zapisz")) {
            print("Plik docelowy: "); char sciezka[64]; pobierz_linie(sciezka, 64); print("\n");
            char bezp[64]; formatuj_sciezke(sciezka, bezp);
            print("Tekst: "); char dane[128]; pobierz_linie(dane, 128); print("\n");
            if (zapisz_plik(bezp, dane, strlen(dane))) print("Zapisano!\n"); else print("Błąd zapisu.\n");
        }
        else if (strncmp(bufor_komendy, "usun ", 5)) {
            char sciezka[64]; formatuj_sciezke(&bufor_komendy[5], sciezka);
            if (usun_twor(sciezka)) { print("Usunięto obiekt: "); print(sciezka); print("\n"); } 
            else print("Błąd: Nie można usunąć.\n");
        }
        else if (strcmp(bufor_komendy, "zmien_nazwe")) {
            print("Ścieżka do zmiany: "); char stara[64]; pobierz_linie(stara, 64); print("\n"); char bezp_stara[64]; formatuj_sciezke(stara, bezp_stara);
            print("Nowa nazwa: "); char nowa[64]; pobierz_linie(nowa, 64); print("\n");
            if (zmien_nazwe_tworu(bezp_stara, nowa)) print("Zmieniono nazwę.\n"); else print("Błąd zmiany nazwy.\n");
        }
        else if (strncmp(bufor_komendy, "pisz ", 5)) { print(&bufor_komendy[5]); print("\n"); }
        else { print("Nieznane polecenie: '"); print(bufor_komendy); print("'. Wpisz 'pomoc'.\n"); }
    }
}