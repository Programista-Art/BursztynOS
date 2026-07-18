/*
 * Aplikacja: Bursztyn Shell (bsh)
 * Poziom: Ring 3 (Przestrzeń Użytkownika)
 * Opis: Zaawansowana powłoka tekstowa odbierająca komendy użytkownika 
 * i wywołująca API systemu (BWS) w celu interakcji z plikami i ekranem.
 */

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------
// STRUKTURA NAGŁÓWKA PROGRAMU (Wymóg Loadera Bursztyna)
// ---------------------------------------------------------
struct NaglowekBur {
    uint8_t  magia[4];            // Sygnatura identyfikacyjna "BUR\0"
    uint64_t punkt_wejscia;       // Gdzie zaczyna się kod (_start)
    
    uint64_t tekst_przesuniecie;  // Przesunięcie sekcji .text w pliku binarnym
    uint64_t tekst_rozmiar;       // Rozmiar sekcji .text
    uint64_t tekst_wirtualny;     // Docelowy adres wirtualny dla .text
    
    uint64_t dane_przesuniecie;   // Przesunięcie sekcji .data w pliku binarnym
    uint64_t dane_rozmiar;        // Rozmiar sekcji .data
    uint64_t dane_wirtualny;      // Docelowy adres wirtualny dla .data
} __attribute__((packed));

extern "C" void _start();

extern "C" __attribute__((section(".naglowek"), used))
const struct NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    (uint64_t)&_start,
    4096,  8192, 0x401000, // ZWIĘKSZONE: Kod powłoki zajmuje do 8 KB (2 strony)
    12288, 8192, 0x403000  // ZWIĘKSZONE: Dane przesunięte na offset 12 KB, pojemność do 8 KB
};

// ---------------------------------------------------------
// WARSTWA KOMUNIKACJI Z JĄDREM (Syscalls)
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// BIBLIOTEKA STANDARDOWA (Ring 3)
// ---------------------------------------------------------
void print(const char* tekst) { bws_wywolaj(1, (uint64_t)tekst); }
bool utworz(const char* plik) { return bws_wywolaj(2, (uint64_t)plik) != 0; }
bool zapisz_plik(const char* plik, const char* dane, uint32_t dlugosc) { return bws_wywolaj(3, (uint64_t)plik, (uint64_t)dane, dlugosc) != 0; }
char getch() { return (char)bws_wywolaj(4); }
bool czytaj_plik(const char* plik, char* bufor, uint32_t max_dlugosc) { return bws_wywolaj(5, (uint64_t)plik, (uint64_t)bufor, max_dlugosc) != 0; }
bool wylistuj_katalog(const char* sciezka, char* bufor, uint32_t max_dlugosc) { return bws_wywolaj(6, (uint64_t)sciezka, (uint64_t)bufor, max_dlugosc) != 0; }
// NOWE: Zaawansowane manipulacje na plikach (BWS-7 i BWS-8)
bool usun_twor(const char* sciezka) { return bws_wywolaj(7, (uint64_t)sciezka) != 0; }
bool zmien_nazwe_tworu(const char* sciezka, const char* nowa_nazwa) { return bws_wywolaj(8, (uint64_t)sciezka, (uint64_t)nowa_nazwa) != 0; }

int strlen(const char* str) {
    int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

bool strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 == *(const unsigned char*)s2;
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
            if (pozycja > 0) {
                pozycja--;
                print("\b \b");
            }
        } 
        else if (pozycja < max_dlugosc - 1) {
            bufor[pozycja++] = c;
            char tmp[2] = {c, '\0'};
            print(tmp);
        }
    }
}

void formatuj_sciezke(const char* wejscie, char* wyjscie) {
    if (wejscie[0] == '/') {
        int i = 0;
        while (wejscie[i] != '\0') { wyjscie[i] = wejscie[i]; i++; }
        wyjscie[i] = '\0';
    } else {
        wyjscie[0] = '/';
        int i = 0;
        while (wejscie[i] != '\0') { wyjscie[i+1] = wejscie[i]; i++; }
        wyjscie[i+1] = '\0';
    }
}

void int_do_str(int wartosc, char* bufor) {
    if (wartosc == 0) { bufor[0] = '0'; bufor[1] = '\0'; return; }
    int i = 0;
    char temp[16];
    while (wartosc > 0) {
        temp[i++] = (wartosc % 10) + '0';
        wartosc /= 10;
    }
    int j = 0;
    while (i > 0) {
        bufor[j++] = temp[--i];
    }
    bufor[j] = '\0';
}

char historia[5][128];
int hist_ilosc = 0;

// ---------------------------------------------------------
// GŁÓWNA PĘTLA POWŁOKI (ENTRY POINT)
// ---------------------------------------------------------

extern "C" void _start() {
    print("\n");
    print("==================================================\n");
    print(" Bursztyn Shell v1.6 (Bursztyn OS Ring 3)\n");
    print(" Wpisz 'pomoc', aby zobaczyc liste komend.\n");
    print("==================================================\n");

    char bufor_komendy[128];

    while (true) {
        print("\nbsh> ");
        pobierz_linie(bufor_komendy, 128);
        print("\n");

        if (strlen(bufor_komendy) == 0) continue;

        for(int i = 4; i > 0; i--) {
            for(int j=0; j<128; j++) historia[i][j] = historia[i-1][j];
        }
        for(int j=0; j<128; j++) historia[0][j] = bufor_komendy[j];
        if (hist_ilosc < 5) hist_ilosc++;

        // PARSER KOMEND
        if (strcmp(bufor_komendy, "pomoc")) {
            print("--- KATEGORIA: SYSTEM ---\n");
            print("  pomoc         - ten ekran\n");
            print("  system        - parametry sprzetowe\n");
            print("  wersja        - wersja powloki i OS\n");
            print("  kto           - zalogowany uzytkownik\n");
            print("  historia      - ostatnie 5 polecen\n");
            print("  czysc         - czysci ekran terminala\n");
            print("--- KATEGORIA: PLIKI ---\n");
            print("  utworz        - nowy, pusty plik / katalog\n");
            print("  zapisz        - dodaje tekst do pliku\n");
            print("  czytaj [plik] - wyswietla zawartosc pliku\n");
            print("  pliki [kat]   - listuje pliki. Daj sciezke, np: pliki /programy\n");
            print("  usun [sciezka]- usuwa plik lub katalog\n");
            print("  zmien_nazwe   - zmienia nazwe (kreator)\n");
            print("  gdzie         - sciezka obecnego katalogu\n");
            print("--- KATEGORIA: ROZRYWKA ---\n");
            print("  pisz [txt]    - powtarza tekst\n");
            print("  cytat         - wczytuje cytaty z pliku\n");
            print("  losuj         - rzuca koscia (1-6)\n");
        }
        else if (strcmp(bufor_komendy, "system")) {
            print("OS: Bursztyn OS x86_64\nJadro: Monolityczne, VMM Paging 4-lvl\n");
            print("Dysk: Bursztynowy System Plikow (BSP64), Bloki 4KB\n");
            print("Ochrona: Poziomy Zaufania Bursztyna (PZB) Aktywne.\n");
        }
        else if (strcmp(bufor_komendy, "wersja")) {
            print("Bursztyn Shell v1.6 (Build: Ring 3 Freestanding)\n");
        }
        else if (strcmp(bufor_komendy, "kto")) {
            print("Zalogowano jako: Gosc (Prawa: Przestrzen Uzytkownika Ring 3)\n");
        }
        else if (strcmp(bufor_komendy, "gdzie")) {
            print("Obecna lokalizacja: / (Korzen Systemu Plikow)\n");
        }
        else if (strcmp(bufor_komendy, "historia")) {
            for(int i = 0; i < hist_ilosc; i++) {
                char numer[4]; int_do_str(i + 1, numer);
                print(numer); print(". "); print(historia[i]); print("\n");
            }
        }
        else if (strcmp(bufor_komendy, "czysc")) {
            for(int i = 0; i < 40; i++) print("\n");
        }
        else if (strcmp(bufor_komendy, "losuj")) {
            uint64_t cykle = pobierz_cykle();
            int kosc = (cykle % 6) + 1;
            char wynik_str[8]; int_do_str(kosc, wynik_str);
            print("Rzucasz koscmi... Wypadlo: "); print(wynik_str); print("!\n");
        }
        else if (strcmp(bufor_komendy, "cytat")) {
            char buf[512]; for(int i=0; i<512; i++) buf[i] = 0; 
            if (czytaj_plik("/cytaty.txt", buf, 511)) {
                print("--- Cytaty wczytane z /cytaty.txt ---\n"); print(buf); print("\n");
            } else {
                print("Tworze domyslny plik /cytaty.txt...\n");
                if (utworz("/cytaty.txt")) {
                    const char* domyslne = "1. U mnie dziala.\n2. Brak bledu to tez blad.\n";
                    zapisz_plik("/cytaty.txt", domyslne, strlen(domyslne));
                    print("Gotowe! Wpisz 'cytat' ponownie.\n");
                }
            }
        }
        else if (strncmp(bufor_komendy, "pliki", 5)) {
            char sciezka[64];
            // Jeśli wpisano np. "pliki /programy", formatujemy ten fragment. Jeśli samo "pliki", używamy "/"
            if (bufor_komendy[5] == ' ' && bufor_komendy[6] != '\0') {
                formatuj_sciezke(&bufor_komendy[6], sciezka);
            } else {
                sciezka[0] = '/'; sciezka[1] = '\0';
            }

            char buf[512];
            for(int i=0; i<512; i++) buf[i] = 0;

            if (wylistuj_katalog(sciezka, buf, 511)) {
                print("Zawartosc zrodla ("); print(sciezka); print("):\n");
                print(buf);
            } else {
                print("Blad: Katalog nie istnieje lub pusty.\n");
            }
        }
        else if (strncmp(bufor_komendy, "czytaj ", 7)) {
            char sciezka[64]; formatuj_sciezke(&bufor_komendy[7], sciezka);
            char buf[512]; for(int i=0; i<512; i++) buf[i] = 0; 
            
            if (czytaj_plik(sciezka, buf, 511)) {
                print("--- "); print(sciezka); print(" ---\n"); print(buf); print("\n");
            } else {
                print("Blad odczytu: Brak pliku.\n");
            }
        }
        else if (strcmp(bufor_komendy, "utworz")) {
            print("Nazwa nowego pliku/folderu (np. /moj_katalog/test.txt): ");
            char sciezka[64]; pobierz_linie(sciezka, 64); print("\n");
            char bezp[64]; formatuj_sciezke(sciezka, bezp);
            if (utworz(bezp)) { print("Zalozono: "); print(bezp); print("\n"); }
            else print("Blad: Zablokowane przez PZB lub bledna sciezka.\n");
        }
        else if (strcmp(bufor_komendy, "zapisz")) {
            print("Plik docelowy: ");
            char sciezka[64]; pobierz_linie(sciezka, 64); print("\n");
            char bezp[64]; formatuj_sciezke(sciezka, bezp);
            
            print("Tekst: ");
            char dane[128]; pobierz_linie(dane, 128); print("\n");
            
            if (zapisz_plik(bezp, dane, strlen(dane))) print("Zapisano!\n");
            else print("Blad zapisu.\n");
        }
        else if (strncmp(bufor_komendy, "usun ", 5)) {
            char sciezka[64]; formatuj_sciezke(&bufor_komendy[5], sciezka);
            if (usun_twor(sciezka)) {
                print("Usunieto obiekt: "); print(sciezka); print("\n");
            } else {
                print("Blad: Nie mozna usunac (sprawdz PZB lub nazwe).\n");
            }
        }
        else if (strcmp(bufor_komendy, "zmien_nazwe")) {
            print("Sciezka do zmiany (np. /stary.txt): ");
            char stara[64]; pobierz_linie(stara, 64); print("\n");
            char bezp_stara[64]; formatuj_sciezke(stara, bezp_stara);

            print("Nowa nazwa (samo slowo, np. nowy.txt): ");
            char nowa[64]; pobierz_linie(nowa, 64); print("\n");
            
            if (zmien_nazwe_tworu(bezp_stara, nowa)) {
                print("Pyslnie zmieniono nazwe.\n");
            } else {
                print("Blad: Blokada PZB lub brak podanego pliku.\n");
            }
        }
        else if (strncmp(bufor_komendy, "pisz ", 5)) {
            print(&bufor_komendy[5]); print("\n");
        }
        else {
            print("Nieznane polecenie: '"); print(bufor_komendy); print("'. Wpisz 'pomoc'.\n");
        }
    }
}