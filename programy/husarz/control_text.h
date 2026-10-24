#pragma once
#include "layout.h"
namespace Husarz {
// No document traversal, allocation or per-pixel search. A field scan examines
// at most 2,048 UTF-8 bytes. Rasterization uses the cooperative paint driver.
struct ControlTextWalker {
    const Document &d; const FormControl &c; Metrics metrics;
    unsigned pos{}; int x{},y{},width{},line{}; bool multiline{},wrap{},password{};
    ControlTextWalker(const Document &doc,const FormControl &field,Metrics m):d(doc),c(field),metrics(m) {
        const auto &s=d.styles[c.node];width=d.boxes[c.node].content_width;
        line=s.line>font_height(m,s)?s.line:font_height(m,s);
        multiline=d.tag(c.node,"textarea");password=input_type(d,c.node,"password");
        wrap=multiline && s.white!=WhiteSpace::Pre && s.white!=WhiteSpace::NoWrap;
    }
    uint32_t codepoint(size_t &used)const {
        uint32_t cp=utf8(c.value+pos,c.length-pos,used);
        return password?'*':cp;
    }
    int step_width(uint32_t cp)const {return advance(metrics,cp=='\t'?' ':cp,d.styles[c.node])*(cp=='\t'?4:1);}
    void prepare() {
        if(pos>=c.length)return;
        size_t used;uint32_t cp=codepoint(used);
        if(wrap && cp!='\n' && x && int64_t(x)+step_width(cp)>width){x=0;y=clamp64(int64_t(y)+line);}
    }
    void next() {
        if(pos>=c.length)return;
        size_t used;uint32_t cp=codepoint(used);pos+=unsigned(used);
        if(multiline && cp=='\n'){x=0;y=clamp64(int64_t(y)+line);}
        else x=clamp64(int64_t(x)+step_width(cp));
    }
};
struct ControlTextGeometry {int caret_x{},caret_y{},width{},height{};};
inline ControlTextGeometry control_text_geometry(const Document &d,const FormControl &c,Metrics metrics) {
    ControlTextGeometry g;ControlTextWalker w(d,c,metrics);
    for(;;) {
        w.prepare();if(w.pos==c.cursor){g.caret_x=w.x;g.caret_y=w.y;}
        if(w.x>g.width)g.width=w.x;
        g.height=w.y+w.line;
        if(w.pos>=c.length)break;
        w.next();
    }
    return g;
}
inline unsigned control_text_hit(const Document &d,const FormControl &c,Metrics metrics,int x,int y) {
    ControlTextWalker w(d,c,metrics);unsigned best=0;int64_t best_y=INT64_MAX,best_x=INT64_MAX;
    for(;;) {
        w.prepare();int64_t dy=int64_t(y)-w.y,dx=int64_t(x)-w.x;if(dy<0)dy=-dy;if(dx<0)dx=-dx;
        if(dy<best_y || (dy==best_y && dx<best_x)){best=w.pos;best_y=dy;best_x=dx;}
        if(w.pos>=c.length)break;
        w.next();
    }
    return best;
}
inline bool control_apply_geometry(const Document &d,FormControl &c,Metrics metrics,ControlTextGeometry g) {
    const auto &b=d.boxes[c.node];int x=c.scroll_x,y=c.scroll_y;
    if(g.caret_x<x)x=g.caret_x;
    if(g.caret_x>=int64_t(x)+b.content_width)x=g.caret_x-b.content_width+1;
    int h=font_height(metrics,d.styles[c.node]);
    if(g.caret_y<y)y=g.caret_y;
    if(int64_t(g.caret_y)+h>int64_t(y)+b.content_height)y=g.caret_y+h-b.content_height;
    x=clamp64(x,0,clamp64(int64_t(g.width)+1-b.content_width));
    y=clamp64(y,0,clamp64(int64_t(g.height)-b.content_height));
    bool changed=x!=c.scroll_x || y!=c.scroll_y;c.scroll_x=x;c.scroll_y=y;return changed;
}
inline void control_keep_caret(Document &d,FormControl &c,Metrics metrics) {
    if(control_apply_geometry(d,c,metrics,control_text_geometry(d,c,metrics)))d.invalidate(c.node,1);
    c.reveal_caret=false;
}
inline bool control_scroll(Document &d,FormControl &c,Metrics metrics,int64_t dx,int64_t dy) {
    if(!selectable(d,c))return false;
    auto g=control_text_geometry(d,c,metrics);const auto &b=d.boxes[c.node];
    int x=clamp64(int64_t(c.scroll_x)+dx,0,clamp64(int64_t(g.width)+1-b.content_width));
    int y=d.tag(c.node,"textarea")?clamp64(int64_t(c.scroll_y)+dy,0,clamp64(int64_t(g.height)-b.content_height)):0;
    if(x==c.scroll_x && y==c.scroll_y)return false;
    c.scroll_x=x;c.scroll_y=y;c.reveal_caret=false;d.invalidate(c.node,1);return true;
}
inline bool control_vertical_key(Document &d,unsigned key,bool shift,Metrics metrics) {
    auto *c=control(d,d.focused_control);if(!c || !selectable(d,*c) || !d.tag(c->node,"textarea"))return false;
    if(key!=0x48 && key!=0x50 && key!=0x49 && key!=0x51)return false;
    auto g=control_text_geometry(d,*c,metrics);
    int preferred=c->preferred_x<0?g.caret_x:c->preferred_x;
    int line=ControlTextWalker(d,*c,metrics).line;
    int step=(key==0x49 || key==0x51)?d.boxes[c->node].content_height:line;
    step=step<line?line:step;step=(step/line)*line;
    int y=clamp64(int64_t(g.caret_y)+((key==0x48 || key==0x49)?-step:step),-MaxDimension);
    unsigned p=control_text_hit(d,*c,metrics,preferred,y);
    form_move(d,*c,p,shift);c->preferred_x=preferred;return true;
}
inline void control_pointer(Document &d,FormControl &c,Metrics metrics,int x,int y,bool extend) {
    const auto &b=d.boxes[c.node];
    int line=ControlTextWalker(d,c,metrics).line;
    int old_x=c.scroll_x,old_y=c.scroll_y;
    // One bounded autoscroll step per delivered pointer event.
    if(extend){
        if(x<b.content_x)c.scroll_x=clamp64(int64_t(c.scroll_x)-line);
        if(x>=int64_t(b.content_x)+b.content_width)c.scroll_x=clamp64(int64_t(c.scroll_x)+line);
        if(d.tag(c.node,"textarea")) {
            if(y<b.content_y)c.scroll_y=clamp64(int64_t(c.scroll_y)-line);
            if(y>=int64_t(b.content_y)+b.content_height)c.scroll_y=clamp64(int64_t(c.scroll_y)+line);
        }
    }
    int row=y-b.content_y+c.scroll_y;row=row<0?0:(row/line)*line;
    unsigned p=control_text_hit(d,c,metrics,x-b.content_x+c.scroll_x,row);
    form_move(d,c,p,extend);control_keep_caret(d,c,metrics);
    if(c.scroll_x!=old_x || c.scroll_y!=old_y)d.invalidate(c.node,1);
}
} // namespace Husarz
