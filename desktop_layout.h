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
struct Position {int col=0,row=0,span=1,offsetX=0,offsetY=0;bool placed=false,selected=false,custom=false,launcher=false;Label label;};
template<int Capacity> class Layout {
public:
    static constexpr int capacity=Capacity;
    // Matches settings/display bounds, also bounds every search and product.
    static constexpr int GeometryMax=8192;
    Position items[Capacity]{};
    int count=0,cols=1,rows=1,x0=0,y0=0,cw=120,ch=100;
    int workWidth=0,workBottom=0;uint32_t dpi=100;
    uint64_t searchIterations=0;
    int newlyPlaced=0;
    bool set_count(int n) {if(n<0 || n>Capacity)return false;count=n;return true;}
    void geometry(int width,int bottom,int originY,uint32_t scale) {
        width=width<0?0:width>GeometryMax?GeometryMax:width;
        bottom=bottom<0?0:bottom>GeometryMax?GeometryMax:bottom;
        scale=scale>=100 && scale<=400?scale:100;
        originY=originY<0?0:originY>bottom?bottom:originY;
        workWidth=width;workBottom=bottom;dpi=scale;
        cw=ui_skaluj(120,scale);ch=ui_skaluj(100,scale);x0=ui_skaluj(20,scale);y0=originY;
        cols=width>2*x0?(width-2*x0)/cw:0;rows=(bottom-y0)/ch;
    }
    uint64_t candidate_limit() const {
        if(cols<0 || rows<0)return 0;
        uint64_t c=uint64_t(cols)+1,r=uint64_t(rows)+1;
        return r && c>UINT64_MAX/r?0:c*r;
    }
    bool index(int i) const {return i>=0 && i<count && i<Capacity;}
    Prostokat cell(int i) const {
        if(!index(i))return {};
        const auto& p=items[i];
        int64_t x=int64_t(x0)+int64_t(p.col)*cw+p.offsetX;
        int64_t y=int64_t(y0)+int64_t(p.row)*ch+p.offsetY;
        int64_t h=int64_t(p.span)*ch;
        if(x < -GeometryMax*8 || x>GeometryMax*8 || y < -GeometryMax*8 || y>GeometryMax*8 || h<0 || h>GeometryMax*8)return {};
        return {int(x),int(y),cw,int(h)};
    }
    bool fits(int i,int col,int row,int dc=0,int dr=0,bool group=false) const {
        if(!index(i) || col<0 || col>=cols || row<0 || int64_t(row)+items[i].span>rows)return false;
        const auto& p=items[i];
        int64_t ax=int64_t(x0)+int64_t(col)*cw+p.offsetX,ay=int64_t(y0)+int64_t(row)*ch+p.offsetY;
        if(ax < -GeometryMax*8 || ax>GeometryMax*8 || ay < -GeometryMax*8 || ay>GeometryMax*8)return false;
        auto a=visual(int(ax),int(ay),p.label,dpi);
        if(a.x<0 || a.y<0 || int64_t(a.x)+a.szerokosc>workWidth || int64_t(a.y)+a.wysokosc>workBottom)return false;
        for(int j=0;j<count;++j)if(j!=i && items[j].placed) {
            auto b=visual_bounds(j);
            if(group && items[j].selected){b.x+=dc*cw;b.y+=dr*ch;}
            if(intersects(a,b))return false;
        }
        return true;
    }
    bool place(int i,int desiredCol,int desiredRow) {
        if(!index(i))return false;
        int best=-1;int64_t distance=INT64_MAX;
        desiredCol=desiredCol<0?0:desiredCol>cols?cols:desiredCol;
        desiredRow=desiredRow<0?0:desiredRow>rows?rows:desiredRow;
        for(int r=0;r<rows;++r)for(int c=0;c<cols;++c)if(fits(i,c,r)) {
            int64_t dx=c-desiredCol,dy=r-desiredRow,d=dx*dx+dy*dy;
            if(d<distance){distance=d;best=r*cols+c;}
        }
        if(best<0){items[i].placed=items[i].selected=false;return false;}
        items[i].col=best%cols;items[i].row=best/cols;items[i].placed=true;return true;
    }
    void clear_selection() {for(int i=0;i<count;++i)items[i].selected=false;}
    int selected_count() const {int n=0;for(int i=0;i<count;++i)n+=items[i].selected && items[i].placed;return n;}
    void click(int i,bool ctrl) {
        if(!ctrl)clear_selection();
        if(index(i) && items[i].placed)items[i].selected=ctrl?!items[i].selected:true;
    }
    // Dragging a selected member preserves the whole group until MouseUp.
    void drag_click(int i,bool ctrl) {if(ctrl || !index(i) || !items[i].selected)click(i,ctrl);}
    bool group_fits(int dc,int dr) const {
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed &&
            !fits(i,items[i].col+dc,items[i].row+dr,dc,dr,true))return false;
        return true;
    }
    bool move_group(int wantedCol,int wantedRow) {
        if(!selected_count())return false;
        int bc=0,br=0;int64_t best=INT64_MAX;
        wantedCol=wantedCol < -cols?-cols:wantedCol>cols?cols:wantedCol;
        wantedRow=wantedRow < -rows?-rows:wantedRow>rows?rows:wantedRow;
        for(int dr=-rows;dr<=rows;++dr)for(int dc=-cols;dc<=cols;++dc)if(group_fits(dc,dr)) {
            int64_t dx=dc-wantedCol,dy=dr-wantedRow,d=dx*dx+dy*dy;
            if(d<best){best=d;bc=dc;br=dr;}
        }
        if(best==INT64_MAX)return false;
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed){items[i].col+=bc;items[i].row+=br;items[i].custom=true;}
        return true;
    }
    Prostokat visual_bounds(int i) const {
        auto r=cell(i);return r.czy_pusty()?Prostokat{}:visual(r.x,r.y,items[i].label,dpi);
    }
    void visual_position(int i,int x,int y) {
        if(!index(i))return;
        auto& p=items[i];
        // Normalize the grid representation; saved visual coordinates are bounded.
        p.col=p.row=0;
        auto origin=visual(x0,y0,p.label,dpi);
        int64_t dx=int64_t(x)-origin.x,dy=int64_t(y)-origin.y;
        if(dx<INT_MIN || dx>INT_MAX || dy<INT_MIN || dy>INT_MAX){p.placed=p.selected=false;return;}
        p.offsetX=int(dx);p.offsetY=int(dy);p.placed=true;p.custom=true;
    }
    bool visible(int i) const {
        auto r=visual_bounds(i);
        return !r.czy_pusty() && r.x>=0 && r.y>=0 && int64_t(r.x)+r.szerokosc<=workWidth && int64_t(r.y)+r.wysokosc<=workBottom;
    }
    // Retain every valid item first. Relocate only invalid/colliding entries,
    // then fill holes for new entries. Invisible items cannot stay selected.
    int reflow() {
        searchIterations=0;newlyPlaced=0;
        if(count<0 || count>Capacity)return -1;
        int corrected=0;
        for(int pass=0;pass<2;++pass)for(int i=0;i<count;++i)if(items[i].placed && items[i].launcher==(pass==0)) {
            bool valid=visible(i);
            for(int j=0;j<count && valid;++j)if(items[j].placed &&
                ((items[j].launcher && !items[i].launcher) || (items[j].launcher==items[i].launcher && j<i)) &&
                intersects(visual_bounds(i),visual_bounds(j)))valid=false;
            if(!valid){items[i].placed=items[i].selected=false;++corrected;}
        }
        // Valid positions always win; priority applies only to waiting items.
        for(int pass=0;pass<2;++pass)for(int i=0;i<count;++i)if(!items[i].placed && items[i].launcher==(pass==0)) {
            items[i].selected=false;
            auto desired=visual_bounds(i);
            int bestX=0,bestY=0;int64_t best=INT64_MAX;
            for(int row=0;row<=rows;++row)for(int col=0;col<=cols;++col) {
                ++searchIterations;
                auto candidate=visual(x0+col*cw,y0+row*ch,items[i].label,dpi);
                // Partial edge cells use their real visual size.
                if(candidate.szerokosc>workWidth || candidate.wysokosc>workBottom)continue;
                if(candidate.x<0)candidate.x=0;
                if(candidate.y<0)candidate.y=0;
                if(candidate.prawy()>workWidth)candidate.x=workWidth-candidate.szerokosc;
                if(candidate.dolny()>workBottom)candidate.y=workBottom-candidate.wysokosc;
                bool free=true;
                for(int j=0;j<count && free;++j)if(j!=i && items[j].placed && intersects(candidate,visual_bounds(j)))free=false;
                int64_t dx=int64_t(candidate.x)-desired.x,dy=int64_t(candidate.y)-desired.y,d=dx*dx+dy*dy;
                if(free && d<best){best=d;bestX=candidate.x;bestY=candidate.y;}
            }
            if(best!=INT64_MAX){visual_position(i,bestX,bestY);++newlyPlaced;}
        }
        return corrected;
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
        if(!selected_count())return false;
        // Pointer deltas can be arbitrary; clamp before rounding/products.
        clamp_delta(dx,dy);
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
            int64_t best=INT64_MAX;int bestX=0,bestY=0;
            for(int row=-rows;row<=rows;++row)for(int col=-cols;col<=cols;++col) {
                int x=col*cw,y=row*ch;clamp_delta(x,y);
                int64_t ex=int64_t(x)-snappedX,ey=int64_t(y)-snappedY,distance=ex*ex+ey*ey;
                if(distance<best && visual_group_fits(x,y)){best=distance;bestX=x;bestY=y;}
            }
            if(best==INT64_MAX)return false;
            snappedX=bestX;snappedY=bestY;
        }
        for(int i=0;i<count;++i)if(items[i].selected && items[i].placed) {
            items[i].offsetX+=snappedX;items[i].offsetY+=snappedY;items[i].custom=true;
        }
        return true;
    }
};
}
