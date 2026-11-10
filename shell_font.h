// Terminal-specific grid adapter, included inside the Shell namespace.
// UI chrome keeps its shared font path; text cells use this explicit renderer.
bool g_terminal_ttf = true;
uint32_t g_terminal_face = 0, g_terminal_generation = ~0U;
int g_terminal_px = 16, g_terminal_cell = 10, g_terminal_row = 18;
char g_terminal_family[96]{};
void shell_sync_font() {
#if defined(BSF_NATIVE)
    uint32_t gen=BSF::generation();int px=ui_skaluj(BSF::ui_size(),ui_skala_procent);
    if(gen==g_terminal_generation && px==g_terminal_px)return;
    g_terminal_generation=gen;g_terminal_px=px;
    g_terminal_face=g_terminal_ttf?BSF::terminal_face(g_terminal_family[0]?g_terminal_family:nullptr):0;
    g_terminal_cell=g_terminal_face?BSF::advance_face('M',g_terminal_face,px):ui_skaluj(10,ui_skala_procent);
    int height=g_terminal_face?BSF::height(px,BSF::family_name(g_terminal_face)):ui_skaluj(16,ui_skala_procent);
    if(g_terminal_cell<1){g_terminal_face=0;g_terminal_cell=ui_skaluj(10,ui_skala_procent);}
    g_terminal_row=height+ui_skaluj(2,ui_skala_procent);
#else
    if(g_terminal_px==ui_skaluj(16,ui_skala_procent) && g_terminal_generation==0)return;
    g_terminal_generation=0;
    g_terminal_px=ui_skaluj(16,ui_skala_procent);
    g_terminal_cell=ui_skaluj(10,ui_skala_procent);g_terminal_row=ui_skaluj(18,ui_skala_procent);
#endif
    g_rows_dirty=true;
}
int shell_row_height(){return g_terminal_row;}
int shell_cluster_width(const char* text,size_t begin,size_t end,int x=0,int column=0) {
    if(end==begin+1 && text[begin]=='\t')return x<column?column-x:4*g_terminal_cell;
    return int(BSF::terminal_cells(text+begin,end-begin))*g_terminal_cell;
}
void shell_draw_cluster(KontekstRysowania& ctx,const char* text,size_t begin,size_t end,int x,int y,int width,uint32_t color) {
    if(width<=0)width=g_terminal_cell; // A leading marks-only EGC has no independent cell.
    ctx.dodaj_przyciecie({x,y,width,shell_row_height()});
#if defined(BSF_NATIVE)
    const char* family=g_terminal_face?BSF::family_name(g_terminal_face):nullptr;
    uint32_t face=g_terminal_face?BSF::resolved_cluster(text+begin,end-begin,false,family):0;
    BSF::MarkPen marks;marks.base=x;
#endif
    for(size_t at=begin;at<end;){uint32_t cp;at+=BSF::utf8(text+at,end-at,cp);
        if(BSF::default_ignorable(cp)||cp<32||(cp>=127&&cp<160))continue;
#if defined(BSF_NATIVE)
        auto* glyph=g_terminal_face?(face?BSF::glyph_face(cp,face,g_terminal_px):BSF::glyph(cp,g_terminal_px,false,family)):nullptr;
        if(glyph){
            // Never use fallback advance to position the next cell. Draw
            // immediately: mask loans remain valid only until the next lookup.
            int gx=x+(width-glyph->advance)/2+glyph->x;
            int gy=y+BSF::ascent(g_terminal_px,family)+glyph->y;
            marks.place(cp,x,gx,gy,glyph->width,glyph->height,width);
            auto off=ctx.pobierz_przesuniecie();uint32_t row[BSF::MaxBitmap];
            for(int yy=0;yy<glyph->height;++yy){
                for(int xx=0;xx<glyph->width;++xx)row[xx]=(uint32_t(glyph->alpha[yy*glyph->width+xx])<<24)|(color&0xffffff);
                ctx.rysuj_wiersz_alfa_ekranu(gx+off.x,gy+off.y+yy,glyph->width,row);
            }continue;
        }
#endif
        // Original native bitmap assets remain the recovery renderer. Combining
        // marks overlay within the same cell instead of acquiring another one.
        const uint16_t* bits=CzcionkaUI::bitmapa_znaku(cp);
        int ink=CzcionkaUI::szerokosc_znaku(cp);
        int h=ui_skaluj(16,ui_skala_procent),w=ui_skaluj(ink,ui_skala_procent);
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)
            if(bits[yy*16/h]&(1u<<(15-xx*ink/w)))ctx.rysuj_prostokat({x+xx,y+yy,1,1},color);
    }
    ctx.zdejmij_przyciecie();
}
