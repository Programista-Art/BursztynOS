constexpr uint32_t SHELL_SPONSOR_LIMIT=16384;
constexpr size_t SHELL_SPONSOR_LINE_LIMIT=4095;
const char* const SHELL_SPONSOR_PATH="/dane/sponsorze/sponsorzy";
char g_sponsor_data[SHELL_SPONSOR_LIMIT+1];
void shell_sponsors() {
    uint32_t size=0;
    if(!pobierz_rozmiar_pliku(SHELL_SPONSOR_PATH,&size)) { wypisz("Brak pliku z listą sponsorów projektu.\n");return; }
    if(size>SHELL_SPONSOR_LIMIT) { wypisz("Plik sponsorów jest zbyt duży.\n");return; }
    for(size_t i=0;i<sizeof(g_sponsor_data);++i) g_sponsor_data[i]=0;
    if(size && !czytaj_plik(SHELL_SPONSOR_PATH,g_sponsor_data,SHELL_SPONSOR_LIMIT)) { wypisz("Nie można odczytać listy sponsorów projektu.\n");return; }
    uint32_t current=0;
    if(!pobierz_rozmiar_pliku(SHELL_SPONSOR_PATH,&current) || current!=size) { wypisz("Lista sponsorów zmieniła się podczas odczytu. Spróbuj ponownie.\n");return; }
    g_sponsor_data[size]=0;
    size_t line=0;
    for(size_t i=0;i<size;) {
        if(g_sponsor_data[i]=='\n') { line=0;++i;continue; }
        if(g_sponsor_data[i]=='\r' && (i+1==size || g_sponsor_data[i+1]=='\n')) { ++i;continue; }
        size_t n=0;
        if(!shell_valid_codepoint(g_sponsor_data,i,size-i,&n)) { wypisz("Nieprawidłowy tekst UTF-8 w pliku sponsorów.\n");return; }
        line+=n;
        if(line>SHELL_SPONSOR_LINE_LIMIT) { wypisz("Wiersz listy sponsorów jest zbyt długi.\n");return; }
        i+=n;
    }
    bool any=false;
    for(size_t begin=0;begin<size;) {
        size_t end=begin;while(end<size && g_sponsor_data[end]!='\n') ++end;
        size_t trimmed=end;if(trimmed>begin && g_sponsor_data[trimmed-1]=='\r') --trimmed;
        g_sponsor_data[trimmed]=0;
        if(trimmed>begin) {
            if(!any) { wypisz("Sponsorzy projektu\n");any=true; }
            wypisz(g_sponsor_data+begin);wypisz("\n");
        }
        begin=end+1;
    }
    if(!any) wypisz("Brak sponsorów do wyświetlenia.\n");
}
