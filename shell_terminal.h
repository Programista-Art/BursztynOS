// Private Shell viewport; all drawing goes through the existing UI font and BWS57.
size_t g_caret = 0, g_input_anchor = 0;
int g_input_scroll_px = 0;
bool g_select_drag = false, g_select_input = false;
struct ShellPosition { int line; size_t byte; };
ShellPosition g_selection_anchor{0,0}, g_selection_end{0,0};
bool g_output_selection = false;
struct ShellVisualRow { int line; size_t begin, end; int indent; };
constexpr int SHELL_MAX_VISIBLE_ROWS = 256;
ShellVisualRow g_visible_rows[SHELL_MAX_VISIBLE_ROWS];
int g_visible_count = 0, g_total_rows = 1, g_first_line_rows = 1;
bool g_rows_dirty = true;
uint32_t* g_paint_pixels = nullptr;
size_t g_paint_capacity = 0;
// Ring 3 has a 16 KiB stack. Layout and drawing nest, so their scratch buffers
// must not live in their call frames. The Shell event loop is single-threaded.
char g_layout_text[4260], g_render_text[4260], g_hit_text[4260], g_copy_text[4260];
char g_measure_text[2048];

size_t shell_length(const char* s) { size_t n=0; if(s) while(s[n]) ++n; return n; }
size_t shell_next(const char* s, size_t i) {
    uint32_t cp; return i + CzcionkaUI::dekoduj_utf8(s, i, &cp);
}
size_t shell_previous(const char* s, size_t i) {
    if(i) --i;
    while(i && (static_cast<uint8_t>(s[i]) & 0xc0U)==0x80U) --i;
    return i;
}
int shell_prefix_width(const char* s, size_t end) {
    size_t i=0;
    while(s && i<end && i+1<sizeof(g_measure_text)) { g_measure_text[i]=s[i]; ++i; }
    g_measure_text[i]=0; return CzcionkaUI::zmierz_tekst(g_measure_text,ui_skala());
}
Prostokat shell_scrollbar_rect() {
    Prostokat c=shell_obszar_klienta(); const uint32_t p=ui_skala_procent;
    int h=c.wysokosc-ui_skaluj(38,p); if(h<ui_skaluj(16,p)) h=ui_skaluj(16,p);
    return Prostokat(c.prawy()-ui_skaluj(14,p),c.y+ui_skaluj(4,p),ui_skaluj(12,p),h);
}
Prostokat shell_output_rect() {
    Prostokat c=shell_obszar_klienta(); int x=c.x+ui_skaluj(8,ui_skala_procent);
    return Prostokat(x,c.y+ui_skaluj(4,ui_skala_procent),
        shell_scrollbar_rect().x-ui_skaluj(4,ui_skala_procent)-x,
        c.wysokosc-ui_skaluj(38,ui_skala_procent));
}
Prostokat shell_input_rect() {
    Prostokat c=shell_obszar_klienta();
    int y=c.dolny()-ui_skaluj(30,ui_skala_procent);
    return Prostokat(c.x,y+1,shell_scrollbar_rect().x-c.x, c.dolny()-y-1);
}
Prostokat shell_edit_rect() {
    Prostokat r=shell_input_rect(); int x=r.x+ui_skaluj(8,ui_skala_procent);
    int prompt=CzcionkaUI::zmierz_tekst("powłoka> ",ui_skala());
    if(r.prawy()-x-prompt>ui_skaluj(20,ui_skala_procent)) x+=prompt;
    return Prostokat(x,r.y,r.prawy()-x-ui_skaluj(4,ui_skala_procent),r.wysokosc);
}
int shell_page_rows() {
    int n=shell_output_rect().wysokosc/ui_skaluj(16,ui_skala_procent);
    return n>0 ? (n<SHELL_MAX_VISIBLE_ROWS?n:SHELL_MAX_VISIBLE_ROWS) : 1;
}
ShellLine& shell_line(int line) { return g_linie[(g_pierwsza_linia+line)%MAXSCROLLBACKLINES]; }
void shell_line_text(int line, char* out) {
    ShellLine& l=shell_line(line); size_t n=0;
    for(size_t i=0;l.tekst[i];++i) out[n++]=l.tekst[i];
    if(l.opis_x>0 && l.opis[0]) { out[n++]='\t'; for(size_t i=0;l.opis[i];++i) out[n++]=l.opis[i]; }
    out[n]=0;
}
int shell_advance(uint32_t cp,int x,int column) {
    if(cp=='\t') return x<column ? column-x : ui_skaluj(16,ui_skala_procent);
    return CzcionkaUI::postep_znaku(cp,ui_skala());
}
bool shell_before(ShellPosition a,ShellPosition b) {
    return a.line<b.line || (a.line==b.line && a.byte<b.byte);
}
void shell_draw_row(KontekstRysowania& ctx,ShellVisualRow row,int y) {
    char* text=g_render_text; shell_line_text(row.line,text);
    int x=row.indent; Prostokat area=shell_output_rect();
    ShellPosition a=g_selection_anchor,b=g_selection_end;
    if(shell_before(b,a)) { ShellPosition t=a;a=b;b=t; }
    for(size_t i=row.begin;i<row.end;) {
        uint32_t cp; size_t n=CzcionkaUI::dekoduj_utf8(text,i,&cp);
        int w=shell_advance(cp,x,ui_skaluj(shell_line(row.line).opis_x,ui_skala_procent));
        bool selected=g_output_selection && !shell_before({row.line,i},a) && shell_before({row.line,i},b);
        if(selected) ctx.rysuj_prostokat(Prostokat(area.x+x,y,w,ui_skaluj(16,ui_skala_procent)),motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));
        if(cp!='\t') { char glyph[5]; CzcionkaUI::zakoduj_utf8(cp,glyph);
            ctx.rysuj_tekst(Punkt(area.x+x,y),glyph,selected?motyw_kolor_tekstu_zaznaczenia():motyw_kolor(MOTYW_KOLOR_TEKST),ui_skala()); }
        x+=w;i+=n;
    }
}
// Count and paint the same measured rows, including both help columns.
void shell_layout_rows(KontekstRysowania* ctx=nullptr) {
    int total=0; g_visible_count=0; Prostokat area=shell_output_rect();
    for(int l=0;l<g_liczba_linii;++l) {
        char* text=g_layout_text; shell_line_text(l,text); size_t begin=0, i=0;
        int indent=ui_skaluj(shell_line(l).tekst_x,ui_skala_procent),x=indent;
        do {
            uint32_t cp=0; size_t n=CzcionkaUI::dekoduj_utf8(text,i,&cp);
            int w=n?shell_advance(cp,x,ui_skaluj(shell_line(l).opis_x,ui_skala_procent)):0;
            if(!n || (x+w>area.szerokosc && i>begin)) {
                ShellVisualRow row{l,begin,i,indent};
                if(total>=g_scroll_linia && total<g_scroll_linia+shell_page_rows()) {
                    g_visible_rows[g_visible_count++]=row;
                    if(ctx) shell_draw_row(*ctx,row,area.y+(total-g_scroll_linia)*ui_skaluj(16,ui_skala_procent));
                }
                ++total; if(!n) break;
                begin=i;
                indent=shell_line(l).opis_x>0 && i>static_cast<size_t>(shell_line(l).len)
                    ?ui_skaluj(shell_line(l).opis_x,ui_skala_procent):0;
                if(indent>=area.szerokosc) indent=0;
                x=indent;
                w=shell_advance(cp,x,ui_skaluj(shell_line(l).opis_x,ui_skala_procent));
            }
            x+=w;i+=n;
        } while(true);
        if(l==0) g_first_line_rows=total;
    }
    g_total_rows=total;
}
int shell_max_scroll() {
    static int old_width=-1,old_height=-1;static uint32_t old_scale=0;
    Prostokat area=shell_output_rect();
    if(g_rows_dirty || old_width!=area.szerokosc || old_height!=area.wysokosc || old_scale!=ui_skala_procent) {
        shell_layout_rows();g_rows_dirty=false;
        old_width=area.szerokosc;old_height=area.wysokosc;old_scale=ui_skala_procent;
    }
    int n=g_total_rows-shell_page_rows(); return n>0?n:0;
}
void shell_set_scroll(int value) {
    int max=shell_max_scroll(); g_scroll_linia=value<0?0:(value>max?max:value);
    g_auto_scroll=g_scroll_linia==max;
    g_output_selection=false;
    if(g_pasek_przewijania) g_pasek_przewijania->ustaw_wartosc(g_scroll_linia);
}
void dodaj_nowa_linie() {
    g_output_selection=false;
    if(g_liczba_linii<MAXSCROLLBACKLINES) ++g_liczba_linii;
    else {
        shell_layout_rows();
        if(!g_auto_scroll) g_scroll_linia=g_scroll_linia>g_first_line_rows?g_scroll_linia-g_first_line_rows:0;
        g_pierwsza_linia=(g_pierwsza_linia+1)%MAXSCROLLBACKLINES;
    }
    ShellLine& l=shell_line(g_liczba_linii-1); l.len=0;l.tekst_x=0;l.tekst[0]=0;l.opis[0]=0;l.opis_x=0;
    g_rows_dirty=true;
}
void wpisz_do_bufora(uint32_t cp) {
    g_output_selection=false;
    g_rows_dirty=true;
    if(cp=='\n' || cp=='\r') { if(cp=='\n') dodaj_nowa_linie(); return; }
    char bytes[5];size_t n=CzcionkaUI::zakoduj_utf8(cp,bytes);
    if(shell_line(g_liczba_linii-1).len+n>=sizeof(ShellLine::tekst)) dodaj_nowa_linie();
    ShellLine& l=shell_line(g_liczba_linii-1);
    for(size_t i=0;i<n;++i) l.tekst[l.len++]=bytes[i];
    l.tekst[l.len]=0;
}
void shell_wypisz_wiersz_pomocy(const char* command,const char* description) {
    if(!command) return;
    if(shell_line(g_liczba_linii-1).len || shell_line(g_liczba_linii-1).opis_x) dodaj_nowa_linie();
    int indent=0;
    while(*command==' ') { indent+=CzcionkaUI::postep_znaku(' ',SkalaUI(1));++command; }
    shell_line(g_liczba_linii-1).tekst_x=indent;
    for(size_t i=0;command[i];) { uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(command,i,&cp);wpisz_do_bufora(cp);i+=n; }
    if(description && description[0]) {
        int column=270;
        if(ui_skaluj(indent,ui_skala_procent)+CzcionkaUI::zmierz_tekst(command,ui_skala())>ui_skaluj(column-10,ui_skala_procent)) {
            dodaj_nowa_linie(); column=24;
        }
        ShellLine& l=shell_line(g_liczba_linii-1); size_t i=0;
        while(description[i] && i+1<sizeof(l.opis)) { l.opis[i]=description[i];++i; }
        l.opis[i]=0;l.opis_x=column;
    }
    dodaj_nowa_linie(); // Every help row is closed before another output helper runs.
}
bool shell_begin_paint(KontekstRysowania& ctx,Prostokat r) {
    if(r.czy_pusty()) return false;
    size_t count=static_cast<size_t>(r.szerokosc)*r.wysokosc;
    if(count>g_paint_capacity) {
        uint32_t* fresh=new uint32_t[count]; if(!fresh) return false;
        delete[] g_paint_pixels;g_paint_pixels=fresh;g_paint_capacity=count;
    }
    ctx.ustaw_cel(g_paint_pixels,r);ctx.dodaj_przyciecie(r);
    ctx.rysuj_prostokat(r,motyw_kolor(MOTYW_KOLOR_OKNOTLO));return true;
}
void shell_publish(Prostokat r) {
    // BWS57 reserves zero for transparency; opaque black must remain paintable.
    size_t count=static_cast<size_t>(r.szerokosc)*r.wysokosc;
    for(size_t i=0;i<count;++i) if(!g_paint_pixels[i]) g_paint_pixels[i]=0x01000000;
    if(r.szerokosc<=2048 && r.wysokosc<=2048) {
        if(!gui_blit(r.x,r.y,r.szerokosc,r.wysokosc,g_paint_pixels)) return;
    } else {
        // BWS57 limits dimensions to 2048. Finished row tiles retain support
        // for larger desktops without submitting a separate clear operation.
        for(int y=0;y<r.wysokosc;++y) for(int x=0;x<r.szerokosc;x+=2048) {
            int w=r.szerokosc-x;if(w>2048) w=2048;
            if(!gui_blit(r.x+x,r.y+y,w,1,g_paint_pixels+static_cast<size_t>(y)*r.szerokosc+x)) return;
        }
    }
    if(!shell_maximized) {
        int radius=ui_skaluj(motyw_promien(MOTYW_ROG_OKNO),ui_skala_procent);
        for(int dy=0;dy<radius;++dy) {
            int y=shell_y+shell_h-1-dy;
            int inset=motyw_promien_inset(radius,dy);
            Prostokat left=Prostokat(shell_x,y,inset,1).przeciecie(r);
            Prostokat right=Prostokat(shell_x+shell_w-inset,y,inset,1).przeciecie(r);
            if(!left.czy_pusty()) gui_rysuj_prostokat(left.x,left.y,left.szerokosc,1,0);
            if(!right.czy_pusty()) gui_rysuj_prostokat(right.x,right.y,right.szerokosc,1,0);
        }
    }
    gui_odswiez();
}
void shell_draw_input(KontekstRysowania& ctx) {
    Prostokat row=shell_input_rect(),edit=shell_edit_rect();
    const char* text=g_obecny_bufor_wejscia?g_obecny_bufor_wejscia:"";
    int caret=shell_prefix_width(text,g_caret),cw=ui_skaluj(2,ui_skala_procent);
    int maximum=CzcionkaUI::zmierz_tekst(text,ui_skala())-edit.szerokosc+cw;
    if(maximum<0) maximum=0;
    if(g_input_scroll_px>maximum) g_input_scroll_px=maximum;
    if(caret<g_input_scroll_px) g_input_scroll_px=caret;
    if(caret-g_input_scroll_px>edit.szerokosc-cw) g_input_scroll_px=caret-edit.szerokosc+cw;
    if(g_input_scroll_px<0) g_input_scroll_px=0;
    int y=row.y+(row.wysokosc-ui_skaluj(16,ui_skala_procent))/2;
    ctx.dodaj_przyciecie(row);
    if(edit.x>row.x+ui_skaluj(8,ui_skala_procent))
        ctx.rysuj_tekst(Punkt(row.x+ui_skaluj(8,ui_skala_procent),y),"powłoka> ",motyw_kolor(MOTYW_KOLOR_AKCENT),ui_skala());
    ctx.dodaj_przyciecie(edit);
    size_t a=g_caret<g_input_anchor?g_caret:g_input_anchor,b=g_caret>g_input_anchor?g_caret:g_input_anchor;
    int x=edit.x-g_input_scroll_px;
    for(size_t i=0;text[i];) {
        uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(text,i,&cp);int w=CzcionkaUI::postep_znaku(cp,ui_skala());
        bool selected=i>=a && i<b;
        if(x+w>edit.x && x<edit.prawy()) {
            if(selected) ctx.rysuj_prostokat(Prostokat(x,y,w,ui_skaluj(16,ui_skala_procent)),motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));
            char glyph[5];CzcionkaUI::zakoduj_utf8(cp,glyph);
            ctx.rysuj_tekst(Punkt(x,y),glyph,selected?motyw_kolor_tekstu_zaznaczenia():motyw_kolor(MOTYW_KOLOR_TEKST),ui_skala());
        }
        x+=w;i+=n;
    }
    ctx.rysuj_prostokat(Prostokat(edit.x+caret-g_input_scroll_px,y,cw,ui_skaluj(16,ui_skala_procent)),motyw_kolor(MOTYW_KOLOR_TEKST));
    ctx.zdejmij_przyciecie();ctx.zdejmij_przyciecie();
}
void shell_odswiez_linie_wejsciowa() {
    if(shell_minimized) return;
    Prostokat r=shell_input_rect();KontekstRysowania ctx;
    if(!shell_begin_paint(ctx,r)) return;
    shell_draw_input(ctx);
    shell_publish(r);
}
void shell_rysuj_terminal() {
    if(shell_minimized) return;
    static int last_x=-1,last_y=-1,last_w=-1,last_h=-1;static uint32_t last_scale=0;
    if(last_x!=shell_x || last_y!=shell_y || last_w!=shell_w || last_h!=shell_h || last_scale!=ui_skala_procent) {
        gui_rysuj_standardowa_belke(shell_x,shell_y,shell_w,"Powłoka Bursztynowa",shell_maximized);
        last_x=shell_x;last_y=shell_y;last_w=shell_w;last_h=shell_h;last_scale=ui_skala_procent;
    }
    int max=shell_max_scroll();
    if(g_auto_scroll || g_scroll_linia>max) g_scroll_linia=max;
    if(g_scroll_linia<0) g_scroll_linia=0;
    Prostokat c=shell_obszar_klienta();KontekstRysowania ctx;
    if(!shell_begin_paint(ctx,c)) return;
    Prostokat area=shell_output_rect();ctx.dodaj_przyciecie(area);shell_layout_rows(&ctx);ctx.zdejmij_przyciecie();
    if(g_pasek_przewijania) {
        Prostokat sb=shell_scrollbar_rect();sb.x-=c.x;sb.y-=c.y;
        g_pasek_przewijania->ustaw_skale_dpi(ui_skala());g_pasek_przewijania->rozmiesc(sb);
        g_pasek_przewijania->ustaw_rozmiar_strony(shell_page_rows());g_pasek_przewijania->ustaw_zakres(0,max);
        g_pasek_przewijania->ustaw_wartosc(g_scroll_linia);
        ctx.dodaj_przesuniecie(Punkt(c.x,c.y));g_pasek_przewijania->rysuj(ctx);ctx.zdejmij_przesuniecie();
    }
    ctx.rysuj_linie_pozioma(c.x,shell_input_rect().y-1,c.szerokosc,motyw_kolor(MOTYW_KOLOR_RAMKA));
    shell_draw_input(ctx);
    // Publish only complete client pixels. A compositor cannot see a cleared intermediate frame.
    shell_publish(c);
}
void shell_wypisz(const char* text) {
    if(!text) return;
    for(size_t i=0;text[i];) { uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(text,i,&cp);wpisz_do_bufora(cp);i+=n; }
    shell_rysuj_terminal();
}

size_t shell_hit_input(int mouse_x) {
    const char* text=g_obecny_bufor_wejscia?g_obecny_bufor_wejscia:"";
    int target=mouse_x-shell_edit_rect().x+g_input_scroll_px,x=0;
    size_t i=0;
    while(text[i]) {
        uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(text,i,&cp);
        int w=CzcionkaUI::postep_znaku(cp,ui_skala());
        if(target<x+w/2) break;
        x+=w;i+=n;
    }
    return i;
}
ShellPosition shell_hit_output(int mouse_x,int mouse_y) {
    if(!g_visible_count) return {0,0};
    Prostokat area=shell_output_rect();int r=(mouse_y-area.y)/ui_skaluj(16,ui_skala_procent);
    if(r<0) r=0;
    if(r>=g_visible_count) r=g_visible_count-1;
    ShellVisualRow row=g_visible_rows[r];char* text=g_hit_text;shell_line_text(row.line,text);
    int x=row.indent,target=mouse_x-area.x;size_t i=row.begin;
    while(i<row.end) {
        uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(text,i,&cp);
        int w=shell_advance(cp,x,ui_skaluj(shell_line(row.line).opis_x,ui_skala_procent));
        if(target<x+w/2) break;
        x+=w;i+=n;
    }
    return {row.line,i};
}
void shell_copy_selection() {
    // Same MIME and NUL-terminated convention as PoleTekstowe::kopiuj_do_schowka.
    size_t capacity=g_output_selection?static_cast<size_t>(MAXSCROLLBACKLINES)*4261:2048;
    char* text=new char[capacity];if(!text) return;size_t n=0;
    if(g_output_selection) {
        ShellPosition a=g_selection_anchor,b=g_selection_end;
        if(shell_before(b,a)) { ShellPosition t=a;a=b;b=t; }
        for(int l=a.line;l<=b.line && l<g_liczba_linii;++l) {
            char* line=g_copy_text;shell_line_text(l,line);size_t length=shell_length(line);
            size_t begin=l==a.line?a.byte:0,end=l==b.line?b.byte:length;
            for(size_t i=begin;i<end && i<length;++i) text[n++]=line[i];
            if(l<b.line) text[n++]='\n';
        }
    } else if(g_obecny_bufor_wejscia) {
        size_t a=g_caret<g_input_anchor?g_caret:g_input_anchor,b=g_caret>g_input_anchor?g_caret:g_input_anchor;
        for(size_t i=a;i<b;++i) text[n++]=g_obecny_bufor_wejscia[i];
    }
    text[n]=0;if(n) clipboard_set("text/plain",text,n+1);delete[] text;
}
void shell_delete_range(char* text,size_t begin,size_t end) {
    size_t length=shell_length(text);
    for(size_t i=end;i<=length;++i) text[begin+i-end]=text[i];
    g_caret=g_input_anchor=begin;
}
void shell_delete_input_selection(char* text) {
    size_t a=g_caret<g_input_anchor?g_caret:g_input_anchor,b=g_caret>g_input_anchor?g_caret:g_input_anchor;
    shell_delete_range(text,a,b);
}
