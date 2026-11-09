#pragma once
#include "bursztyn_gui.h"
#include "bursztyn_ui/czcionka.h"

// App-level policy only. Permissions and atomic operations remain in BWS/PSF.
namespace PlikiUI {
inline const char* komunikat_bledu(WynikPlikowy wynik) {
    switch(wynik) {
    case WynikPlikowy::Ok:return "Operacja zakończona.";
    case WynikPlikowy::Istnieje:return "Element o tej nazwie już istnieje.";
    case WynikPlikowy::BrakUprawnien:return "Brak uprawnień do elementu lub jego folderu.";
    case WynikPlikowy::Niepusty:return "Folder nie jest pusty.";
    case WynikPlikowy::Brak:return "Nie znaleziono elementu.";
    case WynikPlikowy::NieprawidlowaNazwa:return "Nieprawidłowa nazwa lub ścieżka.";
    case WynikPlikowy::BrakMiejsca:return "Brak miejsca w systemie plików.";
    case WynikPlikowy::BladIO:return "Błąd odczytu lub zapisu systemu plików.";
    case WynikPlikowy::Chroniony:return "Element systemowy jest chroniony przed usunięciem.";
    case WynikPlikowy::Czesciowo:return "Usunięto tylko część elementów. Operacja została przerwana.";
    case WynikPlikowy::Uszkodzony:return "Niespójna struktura folderu. Nie wykonano usuwania.";
    case WynikPlikowy::Limit:return "Folder przekracza bezpieczny limit usuwania.";
    case WynikPlikowy::BrakPamieci:return "Brak pamięci do wykonania operacji.";
    default:return "Nie udało się wykonać operacji plikowej.";
    }
}
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
inline WynikPlikowy utworz_element_wynik(const char* directory,const char* name,bool folder) {
    if(!directory || directory[0]!='/' || !nazwa_poprawna(name))return WynikPlikowy::NieprawidlowaNazwa;
    char path[512]{};size_t n=0;
    while(n<sizeof(path)-1 && directory[n]){path[n]=directory[n];++n;}
    if(directory[n])return WynikPlikowy::NieprawidlowaNazwa;
    if(n>1 && path[n-1]=='/')--n;
    if(n>1){if(n+1>=sizeof(path))return WynikPlikowy::NieprawidlowaNazwa;path[n++]='/';}
    for(size_t i=0;name[i];++i){if(n+1>=sizeof(path))return WynikPlikowy::NieprawidlowaNazwa;path[n++]=name[i];}
    path[n]=0;
    return static_cast<WynikPlikowy>(bws_wywolaj(folder?46:2,reinterpret_cast<uint64_t>(path),BWS_STATUS_PLIKOWY));
}
inline bool utworz_element(const char* directory,const char* name,bool folder) {
    return utworz_element_wynik(directory,name,folder)==WynikPlikowy::Ok;
}
// Paths supplied by desktop listings are absolute, normalized paths.
inline bool cel_przeniesienia_poprawny(const char* source,const char* target) {
    if(!source || !target || source[0]!='/' || target[0]!='/')return false;
    size_t n=0;while(source[n] && source[n]==target[n])++n;
    return source[n]!=0 || (target[n]!=0 && target[n]!='/');
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
struct RaportWklejenia {uint32_t wykonane=0,wszystkie=0;char blad[512]{};};
inline WynikWklejenia wklej(const char* directory,RaportWklejenia* report=nullptr) {
    if(report)*report={};
    BwsSchowekPlikow record{};
    if(!schowek_gotowy(&record))return WynikWklejenia::Pusty;
    uint32_t count=record.zarezerwowane?record.zarezerwowane:1;
    if(count>BWS_SCHOWEK_MAX_PLIKOW)return WynikWklejenia::Blad;
    if(report)report->wszystkie=count;
    uint64_t generation=record.generacja;auto operation=record.operacja;uint32_t done=0;
    for(uint32_t n=0;n<count;++n) {
        bool read=n==0 || pobierz_element_schowka_plikow(&record,n,generation);
        bool ok=read && (operation==BWS_SCHOWEK_COPY?kopiuj_twor_uzytkownika(record.sciezka,directory):przenies_twor_uzytkownika(record.sciezka,directory));
        if(!ok) {
            if(report)for(size_t i=0;i<sizeof(report->blad)-1 && record.sciezka[i];++i)report->blad[i]=record.sciezka[i];
            if(operation==BWS_SCHOWEK_CUT && done)usun_prefiks_schowka_plikow(generation,done);
            return WynikWklejenia::Blad;
        }
        ++done;if(report)report->wykonane=done;
    }
    if(operation==BWS_SCHOWEK_CUT)(void)wyczysc_schowek_plikow(generation);
    return WynikWklejenia::OK;
}

} // namespace PlikiUI
