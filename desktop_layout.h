#pragma once
#include "pulpit_geometria.h"
#include "bursztyn_ui/czcionka.h"
#include "bursztyn_ui/typy.h"

namespace Desktop {
using BursztynUI::Prostokat;
constexpr int MaxLines=16;
struct Label {
    uint16_t starts[MaxLines]{},ends[MaxLines]{};
    int widths[MaxLines]{},count=0,width=0,height=0;
    bool complete=true;
};
// The caller supplies the current system Unicode font measurement.
inline Label wrap(const char* text,int width,int lineHeight,int gap,int maxLines,
                  int scale,int (*measure)(const char*,int)) {
    Label l;char line[516]{};int start=0,pos=0,n=0;
    if(maxLines>MaxLines)maxLines=MaxLines;
    while(text && text[pos] && l.count<maxLines) {
        uint32_t cp=0;int bytes=int(BursztynUI::CzcionkaUI::dekoduj_utf8(text,pos,&cp));
        if(!bytes)break;
        if(n+bytes>=512)break;
        for(int b=0;b<bytes;++b)line[n+b]=text[pos+b];
        line[n+bytes]=0;
        int measured=measure(line,scale);
        if(n && measured>width) {
            line[n]=0;l.starts[l.count]=start;l.ends[l.count]=pos;
            l.widths[l.count]=measure(line,scale);++l.count;start=pos;n=0;continue;
        }
        // A glyph wider than the region cannot be drawn safely.
        if(measured>width)break;
        n+=bytes;pos+=bytes;
    }
    if(n && l.count<maxLines) {l.starts[l.count]=start;l.ends[l.count]=pos;l.widths[l.count]=measure(line,scale);++l.count;}
    l.complete=!text || !text[pos];
    if(!l.complete && l.count) {
        int last=l.count-1,start=l.starts[last],end=l.ends[last];
        for(;;) {
            int n=end-start;
            for(int i=0;i<n;++i)line[i]=text[start+i];
            line[n]=char(0xe2);line[n+1]=char(0x80);line[n+2]=char(0xa6);line[n+3]=0;
            if(measure(line,scale)<=width || end==start)break;
            --end;while(end>start && (static_cast<unsigned char>(text[end])&192)==128)--end;
        }
        l.ends[last]=end;l.widths[last]=measure(line,scale);
    }
    for(int i=0;i<l.count;++i)if(l.widths[i]>l.width)l.width=l.widths[i];
    l.height=l.count?l.count*(lineHeight+gap)-gap:0;return l;
}
inline Prostokat unite(Prostokat a,Prostokat b) {
    if(a.szerokosc<=0 || a.wysokosc<=0)return b;
    if(b.szerokosc<=0 || b.wysokosc<=0)return a;
    int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y;
    int right=a.x+a.szerokosc>b.x+b.szerokosc?a.x+a.szerokosc:b.x+b.szerokosc;
    int bottom=a.y+a.wysokosc>b.y+b.wysokosc?a.y+a.wysokosc:b.y+b.wysokosc;
    return {x,y,right-x,bottom-y};
}
inline bool intersects(Prostokat a,Prostokat b) {
    return a.szerokosc>0 && a.wysokosc>0 && b.szerokosc>0 && b.wysokosc>0 &&
        a.x<b.x+b.szerokosc && b.x<a.x+a.szerokosc && a.y<b.y+b.wysokosc && b.y<a.y+a.wysokosc;
}
inline Prostokat visual(int x,int y,const Label& l,uint32_t scale) {
    auto g=pulpit_geometria_komorki(x,y,scale);int pad=ui_skaluj(3,scale);
    auto r=unite({g.ikona_x,g.ikona_y,g.ikona_szer,g.ikona_wys},
        {g.etykieta_x,g.etykieta_y,g.etykieta_szer,l.height});
    return {r.x-pad,r.y-pad,r.szerokosc+2*pad,r.wysokosc+2*pad};
}
struct Position {int col=0,row=0,span=1;bool placed=false,selected=false,custom=false;Label label;};
template<int Capacity> class Layout {
public:
    Position items[Capacity]{};
    int count=0,cols=1,rows=1,x0=0,y0=0,cw=120,ch=100;
    void geometry(int width,int bottom,int originY,uint32_t scale) {
        cw=ui_skaluj(120,scale);ch=ui_skaluj(100,scale);x0=ui_skaluj(20,scale);y0=originY;
        cols=(width-2*x0)/cw;if(cols<1)cols=1;rows=(bottom-y0)/ch;if(rows<0)rows=0;
    }
    Prostokat cell(int i) const {auto p=items[i];return {x0+p.col*cw,y0+p.row*ch,cw,p.span*ch};}
    bool fits(int i,int col,int row,int dc=0,int dr=0,bool group=false) const {
        if(col<0 || col>=cols || row<0 || row+items[i].span>rows)return false;
        for(int j=0;j<count;++j)if(j!=i && items[j].placed) {
            int jc=items[j].col,jr=items[j].row;
            if(group && items[j].selected){jc+=dc;jr+=dr;}
            if(col==jc && row<jr+items[j].span && jr<row+items[i].span)return false;
        }
        return true;
    }
    bool place(int i,int desiredCol,int desiredRow) {
        int best=-1,distance=0x7fffffff;
        for(int r=0;r<rows;++r)for(int c=0;c<cols;++c)if(fits(i,c,r)) {
            int dx=c-desiredCol,dy=r-desiredRow,d=dx*dx+dy*dy;
            if(d<distance){distance=d;best=r*cols+c;}
        }
        if(best<0){items[i].placed=false;return false;}
        items[i].col=best%cols;items[i].row=best/cols;items[i].placed=true;return true;
    }
    void clear_selection() {for(int i=0;i<count;++i)items[i].selected=false;}
    int selected_count() const {int n=0;for(int i=0;i<count;++i)n+=items[i].selected;return n;}
    void click(int i,bool ctrl) {
        if(!ctrl)clear_selection();
        if(i>=0 && i<count)items[i].selected=ctrl?!items[i].selected:true;
    }
    // Dragging a selected member preserves the whole group until MouseUp.
    void drag_click(int i,bool ctrl) {if(ctrl || i<0 || !items[i].selected)click(i,ctrl);}
    bool group_fits(int dc,int dr) const {
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed &&
            !fits(i,items[i].col+dc,items[i].row+dr,dc,dr,true))return false;
        return true;
    }
    bool move_group(int wantedCol,int wantedRow) {
        int bc=0,br=0,best=0x7fffffff;
        for(int dr=-rows;dr<=rows;++dr)for(int dc=-cols;dc<=cols;++dc)if(group_fits(dc,dr)) {
            int dx=dc-wantedCol,dy=dr-wantedRow,d=dx*dx+dy*dy;
            if(d<best){best=d;bc=dc;br=dr;}
        }
        if(best==0x7fffffff)return false;
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed){items[i].col+=bc;items[i].row+=br;items[i].custom=true;}
        return true;
    }
    void clamp_delta(int& dx,int& dy) const {
        int minx=0,miny=0,maxx=cols*cw,maxy=rows*ch;bool first=true;
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed) {
            auto p=items[i];int x=p.col*cw,y=p.row*ch;
            if(first){minx=x;miny=y;maxx=x+cw;maxy=y+p.span*ch;first=false;}
            else {if(x<minx)minx=x;if(y<miny)miny=y;if(x+cw>maxx)maxx=x+cw;if(y+p.span*ch>maxy)maxy=y+p.span*ch;}
        }
        if(first){dx=dy=0;return;}
        if(dx< -minx)dx=-minx;
        if(dy< -miny)dy=-miny;
        if(dx>cols*cw-maxx)dx=cols*cw-maxx;
        if(dy>rows*ch-maxy)dy=rows*ch-maxy;
    }
};
}
