// Native popup and clipboard adapter; included inside the Shell namespace.
MenuKontekstowe* g_context_menu = nullptr;
ZarzadcaPopupow* g_context_popups = nullptr;
size_t g_input_capacity = 0;
bool g_context_input = false, g_context_mouse_up = false;
int g_context_skip_byte = -1;
char g_clipboard_input[2048];

bool shell_valid_codepoint(const char* text, size_t at, size_t remaining, size_t* count) {
    if(!remaining) return false;
    unsigned char first=static_cast<unsigned char>(text[at]);
    size_t expected=first<0x80?1:first>=0xc2 && first<=0xdf?2:first>=0xe0 && first<=0xef?3:first>=0xf0 && first<=0xf4?4:0;
    if(!expected || expected>remaining) return false;
    uint32_t cp; *count=CzcionkaUI::dekoduj_utf8(text,at,&cp);
    char encoded[5];size_t n=CzcionkaUI::zakoduj_utf8(cp,encoded);
    if(!*count || *count>remaining || n!=*count) return false;
    for(size_t i=0;i<n;++i) if(encoded[i]!=text[at+i]) return false;
    return true;
}
void shell_context_paste() {
    if(!g_context_input || !g_obecny_bufor_wejscia || !g_input_capacity) return;
    // BWS requires capacity for the entire MIME record (no partial reads).
    uint64_t size=0,read=0;
    if(!clipboard_size("text/plain",&size) || !size || size>BWS_CLIPBOARD_FORMAT_MAX) return;
    char* source=new char[static_cast<size_t>(size)+1];if(!source) return;
    if(!clipboard_read("text/plain",source,size,&read) || read>size) { delete[] source;return; }
    size_t bounded=BSF::cluster_prefix(source,read, sizeof(g_clipboard_input)-1);
    for(size_t i=0;i<bounded;++i) g_clipboard_input[i]=source[i];
    delete[] source;read=bounded;
    g_clipboard_input[read]=0;
    size_t length=shell_length(g_obecny_bufor_wejscia);
    if(length>=g_input_capacity || g_caret>length || g_input_anchor>length) return;
    size_t a=g_caret<g_input_anchor?g_caret:g_input_anchor;
    size_t b=g_caret>g_input_anchor?g_caret:g_input_anchor;
    size_t room=g_input_capacity-1-(length-(b-a)), n=0;
    for(size_t i=0;i<read && g_clipboard_input[i];) {
        size_t cp=0;
        if(!shell_valid_codepoint(g_clipboard_input,i,read-i,&cp) || n+cp>room) break;
        for(size_t j=0;j<cp;++j) {
            char c=g_clipboard_input[i+j];
            g_clipboard_input[n+j]=(c=='\n'||c=='\r'||c=='\t')?' ':c;
        }
        n+=cp;i+=cp;
    }
    n=BSF::cluster_prefix(g_clipboard_input,read,n);
    if(!n) return;
    shell_delete_range(g_obecny_bufor_wejscia,a,b);
    length=shell_length(g_obecny_bufor_wejscia);
    for(size_t i=length+1;i>a;--i) g_obecny_bufor_wejscia[i+n-1]=g_obecny_bufor_wejscia[i-1];
    for(size_t i=0;i<n;++i) g_obecny_bufor_wejscia[a+i]=g_clipboard_input[i];
    g_caret=g_input_anchor=BSF::cluster_ceil(g_obecny_bufor_wejscia,shell_length(g_obecny_bufor_wejscia),a+n);
}
void shell_context_action(ElementMenu& item,void*) {
    if(!item.czy_wlaczony()) return;
    const bool output=g_output_selection;
    switch(item.identyfikator()) {
    case 1: shell_copy_selection();return;
    case 2: case 3: shell_context_paste();break; // Wstaw = Wklej.
    case 4:
        if(g_context_input && !output && shell_copy_selection()) shell_delete_input_selection(g_obecny_bufor_wejscia);
        break;
    case 5:
        if(g_context_input && !output) shell_delete_input_selection(g_obecny_bufor_wejscia);
        break;
    case 6:
        if(g_context_input && g_obecny_bufor_wejscia) { g_obecny_bufor_wejscia[0]=0;g_caret=g_input_anchor=0;g_input_scroll_px=0; }
        break;
    }
    shell_odswiez_linie_wejsciowa();
}
void shell_context_close() { if(g_context_menu) g_context_menu->ukryj(); }
void shell_context_open(int x,int y) {
    if(!g_context_menu) {
        g_context_menu=new MenuKontekstowe();g_context_popups=new ZarzadcaPopupow();
        if(!g_context_menu || !g_context_popups) { delete g_context_menu;delete g_context_popups;g_context_menu=nullptr;g_context_popups=nullptr;return; }
        const char* labels[]={"Kopiuj","Wstaw","Wklej","Wytnij","Usuń zaznaczone","Usuń wszystko"};
        for(unsigned i=0;i<6;++i) g_context_menu->menu().dodaj_element(labels[i],i+1,shell_context_action);
    }
    g_context_input=shell_input_rect().zawiera(x,y) && g_obecny_bufor_wejscia;
    const bool selected=g_output_selection?(g_selection_anchor.line!=g_selection_end.line || g_selection_anchor.byte!=g_selection_end.byte):g_caret!=g_input_anchor;
    uint64_t clip=0;const bool paste=g_context_input && clipboard_size("text/plain",&clip) && clip;
    const bool edit=g_context_input && !g_output_selection && selected;
    bool enabled[]={selected,paste,paste,edit,edit,g_context_input && shell_length(g_obecny_bufor_wejscia)>0};
    for(unsigned i=0;i<6;++i) g_context_menu->menu().element(i)->ustaw_wlaczony(enabled[i]);
    g_context_popups->ustaw_skale_dpi(ui_skala());
    g_context_popups->ustaw_geometrie_wlasciciela(Prostokat(shell_x,shell_y,shell_w,shell_h),Prostokat(0,0,shell_screen_w,shell_screen_h));
    g_context_menu->pokaz(*g_context_popups,x,y,ui_skala());
    g_context_popups->odrysuj_popupy(true);
}
bool shell_context_event(const bws_zdarzenie& e) {
    if(e.typ==BWS_ZDARZENIE_EKRAN || e.typ==BWS_ZDARZENIE_BLUR || e.typ==BWS_ZDARZENIE_ZAMKNIJ) { shell_context_close();return false; }
    if(e.typ==BWS_ZDARZENIE_KLAWISZ && g_context_skip_byte>=0) {
        bool skip=static_cast<int>(e.kod)==g_context_skip_byte;g_context_skip_byte=-1;if(skip) return true;
    }
    if(g_context_mouse_up && e.typ==BWS_ZDARZENIE_MYSZ_UP) { g_context_mouse_up=false;return true; }
    if(g_context_menu && g_context_menu->czy_otwarte()) {
        ZdarzenieMyszki m{};m.x=e.x;m.y=e.y;m.przycisk=1;
        switch(e.typ) {
        case BWS_ZDARZENIE_KLAWISZ:
            if(e.kod==27) shell_context_close();
            break;
        case BWS_ZDARZENIE_MYSZ_RUCH: g_context_popups->obsluz_ruch(m);break;
        case BWS_ZDARZENIE_MYSZ_DOWN: g_context_popups->obsluz_wcisniecie(m);g_context_mouse_up=!g_context_menu->czy_otwarte();break;
        case BWS_ZDARZENIE_MYSZ_UP: g_context_popups->obsluz_zwolnienie(m);break;
        case BWS_ZDARZENIE_KLAWISZ_FIZYCZNY:
            if(e.x) { ZdarzenieKlawiatury k{};k.kod_klawisza=((e.kod&BWS_KEY_SOURCE_USB) && (e.kod&255)==40)?13:e.kod;k.modyfikatory=e.przyciski;g_context_popups->obsluz_klawisz(k);
                unsigned code=e.kod&255; if(code==0x1c || code==40) g_context_skip_byte='\n'; else if(code==1 || code==41) g_context_skip_byte=27;
            } break;
        default: break;
        }
        if(g_context_menu->czy_otwarte()) g_context_popups->odrysuj_popupy();
        return true;
    }
    if(e.typ==BWS_ZDARZENIE_MYSZ_PRAWY_DOWN && shell_obszar_klienta().zawiera(e.x,e.y)) {
        shell_context_open(e.x,e.y);return true;
    }
    return false;
}
