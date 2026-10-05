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
        {g.etykieta_x+(g.etykieta_szer-l.width)/2,g.etykieta_y,l.width,l.height});
    return {r.x-pad,r.y-pad,r.szerokosc+2*pad,r.wysokosc+2*pad};
}
struct Position {int col=0,row=0,span=1,offsetX=0,offsetY=0;bool placed=false,selected=false,custom=false;Label label;};
template<int Capacity> class Layout {
public:
    Position items[Capacity]{};
    int count=0,cols=1,rows=1,x0=0,y0=0,cw=120,ch=100;
    int workWidth=0,workBottom=0;uint32_t dpi=100;
    void geometry(int width,int bottom,int originY,uint32_t scale) {
        workWidth=width;workBottom=bottom;dpi=scale;
        cw=ui_skaluj(120,scale);ch=ui_skaluj(100,scale);x0=ui_skaluj(20,scale);y0=originY;
        cols=(width-2*x0)/cw;if(cols<1)cols=1;rows=(bottom-y0)/ch;if(rows<0)rows=0;
    }
    Prostokat cell(int i) const {auto p=items[i];return {x0+p.col*cw+p.offsetX,y0+p.row*ch+p.offsetY,cw,p.span*ch};}
    bool fits(int i,int col,int row,int dc=0,int dr=0,bool group=false) const {
        if(col<0 || col>=cols || row<0 || row+items[i].span>rows)return false;
        for(int j=0;j<count;++j)if(j!=i && items[j].placed) {
            int jc=items[j].col,jr=items[j].row;
            if(group && items[j].selected){jc+=dc;jr+=dr;}
            if(items[i].custom || items[j].custom) {
                auto a=visual(x0+col*cw+items[i].offsetX,y0+row*ch+items[i].offsetY,items[i].label,dpi);
                auto b=visual(x0+jc*cw+items[j].offsetX,y0+jr*ch+items[j].offsetY,items[j].label,dpi);
                if(intersects(a,b))return false;
            } else if(col==jc && row<jr+items[j].span && jr<row+items[i].span)return false;
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
    Prostokat visual_bounds(int i) const {
        auto r=cell(i);return visual(r.x,r.y,items[i].label,dpi);
    }
    void clamp_delta(int& dx,int& dy) const {
        Prostokat bounds{};
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed)
            bounds=unite(bounds,visual_bounds(i));
        if(bounds.czy_pusty()){dx=dy=0;return;}
        if(dx < -bounds.x)dx=-bounds.x;
        if(dy < -bounds.y)dy=-bounds.y;
        if(dx > workWidth-bounds.prawy())dx=workWidth-bounds.prawy();
        if(dy > workBottom-bounds.dolny())dy=workBottom-bounds.dolny();
    }
    bool visual_group_fits(int dx,int dy) const {
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed) {
            auto moved=visual_bounds(i);moved.x+=dx;moved.y+=dy;
            if(moved.x<0 || moved.y<0 || moved.prawy()>workWidth || moved.dolny()>workBottom)return false;
            for(int j=0;j<count;++j)if(!items[j].selected && items[j].placed &&
                intersects(moved,visual_bounds(j)))return false;
        }
        return true;
    }
    // Grid snapping remains the normal placement rule. At an edge the shared
    // delta is limited by the actual icon/label/padding union, not a grid cell.
    bool move_visual_group(int dx,int dy) {
        int snappedX=(dx+(dx>=0?cw/2:-cw/2))/cw*cw;
        int snappedY=(dy+(dy>=0?ch/2:-ch/2))/ch*ch;
        int edgeX=dx,edgeY=dy;clamp_delta(edgeX,edgeY);
        clamp_delta(snappedX,snappedY);
        // A pointer already clamped against an edge must settle on that edge.
        int probeX=dx+(dx>=0?1:-1),probeY=dy+(dy>=0?1:-1);
        clamp_delta(probeX,probeY);
        if(dx && probeX==edgeX)snappedX=edgeX;
        if(dy && probeY==edgeY)snappedY=edgeY;
        if(!visual_group_fits(snappedX,snappedY)) {
            // Retain the existing nearest-free-placement behavior, translating
            // the entire group together. Zero is a legal fallback in a packed desktop.
            int best=0x7fffffff,bestX=0,bestY=0;
            for(int row=-rows;row<=rows;++row)for(int col=-cols;col<=cols;++col) {
                int x=col*cw,y=row*ch;clamp_delta(x,y);
                int ex=x-snappedX,ey=y-snappedY,distance=ex*ex+ey*ey;
                if(distance<best && visual_group_fits(x,y)){best=distance;bestX=x;bestY=y;}
            }
            if(best==0x7fffffff)return false;
            snappedX=bestX;snappedY=bestY;
        }
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed) {
            items[i].offsetX+=snappedX;items[i].offsetY+=snappedY;items[i].custom=true;
        }
        return true;
    }
};
}
