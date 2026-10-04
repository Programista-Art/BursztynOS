#pragma once
#include "bursztyn_gui.h"
#include "bursztyn_ui/czcionka.h"

// App-level policy only. Permissions and atomic operations remain in BWS/PSF.
namespace PlikiUI {
inline bool nazwa_poprawna(const char* name, size_t capacity = 56) {
    if (!name || !name[0]) return false;
    size_t n = 0;
    while (n < capacity && name[n]) ++n;
    if (n == capacity || (n == 1 && name[0] == '.') ||
        (n == 2 && name[0] == '.' && name[1] == '.')) return false;
    for (size_t i = 0; i < n;) {
        uint32_t cp = 0;
        size_t used = BursztynUI::CzcionkaUI::dekoduj_utf8(name, i, &cp);
        char encoded[5];
        size_t size = BursztynUI::CzcionkaUI::zakoduj_utf8(cp, encoded);
        if (!used || used != size || used > n - i || cp < 32 || cp == 127 ||
            cp == '/' || cp == '\\') return false;
        for (size_t j = 0; j < used; ++j) if (name[i+j] != encoded[j]) return false;
        i += used;
    }
    return true;
}
inline bool utworz_element(const char* directory,const char* name,bool folder) {
    if(!directory || directory[0]!='/' || !nazwa_poprawna(name))return false;
    char path[512]{};size_t n=0;
    while(n<sizeof(path)-1 && directory[n]){path[n]=directory[n];++n;}
    if(directory[n])return false;
    if(n>1 && path[n-1]=='/')--n;
    if(n>1){if(n+1>=sizeof(path))return false;path[n++]='/';}
    for(size_t i=0;name[i];++i){if(n+1>=sizeof(path))return false;path[n++]=name[i];}
    path[n]=0;
    return folder?utworz_katalog_uzytkownika(path):utworz(path);
}
inline bool tekstowy(const char* path, bool directory) {
    if (!path || directory) return false;
    size_t n = 0; while (n < 512 && path[n]) ++n;
    if (n < 4 || n == 512 || path[n-4] != '.') return false;
    const char* ext = "txt";
    for (int i = 0; i < 3; ++i) {
        char c = path[n-3+i]; if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if (c != ext[i]) return false;
    }
    return true;
}
inline bool notatnik(const char* path, bool directory) {
    return tekstowy(path, directory) && uruchom_program_z_argumentem_uzytkownika(
        "/programy/notatnik.cebula/notatnik.bur", path);
}
inline bool schowek_gotowy(BwsSchowekPlikow* record) {
    return record && pobierz_schowek_plikow(record) &&
        record->wersja == BWS_SCHOWEK_WERSJA && record->sciezka[0] == '/' &&
        (record->operacja == BWS_SCHOWEK_COPY || record->operacja == BWS_SCHOWEK_CUT);
}
enum class WynikWklejenia { OK, Pusty, Blad };
inline WynikWklejenia wklej(const char* directory) {
    BwsSchowekPlikow record{};
    if (!schowek_gotowy(&record)) return WynikWklejenia::Pusty;
    bool ok = record.operacja == BWS_SCHOWEK_COPY
        ? kopiuj_twor_uzytkownika(record.sciezka, directory)
        : przenies_twor_uzytkownika(record.sciezka, directory);
    // BWS48 moves atomically without overwrite. Never delete after failed paste.
    if (!ok) return WynikWklejenia::Blad;
    if (record.operacja == BWS_SCHOWEK_CUT) (void)wyczysc_schowek_plikow(record.generacja);
    return WynikWklejenia::OK;
}
} // namespace PlikiUI
