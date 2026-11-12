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
    TextCluster cluster()const{return text_cluster(c.value,c.length,pos,metrics,d.styles[c.node],pos==0);}
    int current_width()const{size_t n;uint32_t cp=codepoint(n);return password?step_width('*'):cp=='\t'?step_width(cp):cluster_width(c.value,cluster(),metrics,d.styles[c.node],pos==0);}
    void prepare() {
        if(pos>=c.length)return;
        size_t used;uint32_t cp=codepoint(used);
        if(wrap && cp!='\n' && x && int64_t(x)+current_width()>width){x=0;y=clamp64(int64_t(y)+line);}
    }
    void next() {
        if(pos>=c.length)return;
        size_t used;uint32_t cp=codepoint(used);int width=current_width();pos=unsigned(cluster().end);
        if(multiline && cp=='\n'){x=0;y=clamp64(int64_t(y)+line);}
        else x=clamp64(int64_t(x)+width);
    }
};
struct ControlTextGeometry {int caret_x{},caret_y{},width{},height{};};
#if HUSARZ_BIDI
// ---- BSF 2.1 form controls ------------------------------------------------
// A text input/search/textarea whose value needs complex layout (or whose
// direction is rtl) is measured, hit-tested, painted and navigated from one
// BSF layout of its logical value: dir=auto resolves from the current value
// (P2/P3), wrapping textareas wrap at the content width, RTL lines are
// right-aligned. The value, selection offsets, input/change events and form
// submission stay logical. Password fields never take this path (only '*'
// masks are measured), so no plaintext reaches a layout cache.
struct ControlLayoutSlot { BSF::TextLayout layout; uint64_t hash{}, used{}; bool valid{}; };
inline ControlLayoutSlot control_layouts[4];
inline uint64_t control_layout_clock{};
inline const BSF::TextLayout *control_layout(const Document &d, const FormControl &c, Metrics metrics) {
    if (!metrics.family_advance || !c.length || input_type(d, c.node, "password") || !BSF::ready())
        return nullptr;
    const auto &s = d.styles[c.node];
    bool automatic = equal_ci(d.attr(c.node, "dir"), length(d.attr(c.node, "dir"), 8), "auto") ||
                     s.unicode_bidi == UnicodeBidi::Plaintext;
    if (!(s.direction & 1) && !BSF::needs_complex_layout(c.value, c.length))
        return nullptr;
    bool multiline = d.tag(c.node, "textarea");
    bool wrap = multiline && s.white != WhiteSpace::Pre && s.white != WhiteSpace::NoWrap;
    int64_t width = wrap ? int64_t(d.boxes[c.node].content_width > 1 ? d.boxes[c.node].content_width : 1) * BSF::Fixed : 0;
    BSF::BaseDirection dir = automatic ? BSF::BaseDirection::Auto : (s.direction & 1) ? BSF::BaseDirection::RTL : BSF::BaseDirection::LTR;
    uint64_t h = UINT64_C(1469598103934665603);
    for (unsigned i = 0; i < c.length; ++i)
        h = (h ^ uint8_t(c.value[i])) * UINT64_C(1099511628211);
    h ^= (uint64_t(s.font) << 1) ^ (uint64_t(s.bold) << 13) ^ (uint64_t(s.italic) << 14) ^ (uint64_t(uint8_t(dir)) << 15) ^
         (uint64_t(s.native_family) << 20) ^ (uint64_t(width) << 7) ^ (uint64_t(BSF::generation()) << 40);
    ControlLayoutSlot *victim = &control_layouts[0];
    for (auto &slot : control_layouts) {
        if (slot.valid && slot.hash == h && slot.layout.bytes() == c.length) {
            bool same = true;
            for (unsigned i = 0; i < c.length && same; ++i)
                same = slot.layout.text()[i] == c.value[i];
            if (same) {
                slot.used = ++control_layout_clock;
                return &slot.layout;
            }
        }
        if (!slot.valid || (victim->valid && slot.used < victim->used))
            victim = &slot;
    }
    victim->valid = false;
    BSF::LayoutRequest r;
    r.px = s.font;
    r.bold = s.bold;
    r.italic = s.italic;
    r.family = BSF::family_name(s.native_family);
    r.max_width = width;
    r.direction = dir;
    if (!BSF::build_layout(victim->layout, c.value, c.length, r))
        return nullptr;
    victim->hash = h;
    victim->used = ++control_layout_clock;
    victim->valid = true;
    return &victim->layout;
}
inline int control_line_height(const Document &d, const FormControl &c, Metrics metrics) {
    const auto &s = d.styles[c.node];
    int h = font_height(metrics, s);
    return s.line > h ? s.line : h;
}
// Right alignment of an RTL line inside the content box.
inline int control_line_shift(const Document &d, const FormControl &c, const BSF::TextLayout &L, size_t line) {
    if (line >= L.line_count() || !(L.lines()[line].paragraph_level & 1))
        return 0;
    int w = int((L.lines()[line].width + BSF::Fixed / 2) / BSF::Fixed), cw = d.boxes[c.node].content_width - 1;
    return cw > w ? cw - w : 0;
}
inline int control_caret_x(const Document &d, const FormControl &c, const BSF::TextLayout &L, size_t byte, size_t *line) {
    size_t li = 0;
    int64_t x = L.caret_x(byte, &li);
    if (line)
        *line = li;
    return control_line_shift(d, c, L, li) + int((x + BSF::Fixed / 2) / BSF::Fixed);
}
#endif
inline ControlTextGeometry control_text_geometry(const Document &d,const FormControl &c,Metrics metrics) {
    ControlTextGeometry g;
#if HUSARZ_BIDI
    if (const auto *L = control_layout(d, c, metrics)) {
        int lh = control_line_height(d, c, metrics);
        size_t li = 0;
        g.caret_x = control_caret_x(d, c, *L, c.cursor, &li);
        g.caret_y = clamp64(int64_t(li) * lh);
        g.width = int((L->width() + BSF::Fixed - 1) / BSF::Fixed);
        if (g.width < d.boxes[c.node].content_width - 1 && (L->base_level() & 1))
            g.width = d.boxes[c.node].content_width - 1;
        g.height = clamp64(int64_t(L->line_count() ? L->line_count() : 1) * lh);
        return g;
    }
#endif
    ControlTextWalker w(d,c,metrics);
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
#if HUSARZ_BIDI
    if (const auto *L = control_layout(d, c, metrics)) {
        int lh = control_line_height(d, c, metrics);
        size_t li = y < 0 ? 0 : size_t(y / (lh > 0 ? lh : 1));
        if (li >= L->line_count())
            li = L->line_count() ? L->line_count() - 1 : 0;
        return unsigned(L->hit_test(int64_t(x - control_line_shift(d, c, *L, li)) * BSF::Fixed, li));
    }
#endif
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
// Left/Right on a bidi/shaped value move visually on the caret's line. At
// the visual edge of a textarea line the caret continues on the adjacent
// line in reading order (as Notepad, never back into a line of opposite
// direction, so a held key always terminates); an input stays at its edge.
inline bool control_horizontal_key(Document &d,unsigned key,bool shift,Metrics metrics) {
#if HUSARZ_BIDI
    if(key!=0x4b && key!=0x4d)return false;
    auto *c=control(d,d.focused_control);if(!c || !selectable(d,*c))return false;
    const auto *L=control_layout(d,*c,metrics);if(!L)return false;
    int dir=key==0x4d?1:-1;
    size_t li=0;L->caret_x(c->cursor,&li);
    bool rtl=(L->lines()[li].paragraph_level&1)!=0;
    unsigned p=c->cursor;
    if(!shift && c->anchor!=c->cursor)p=((dir>0)!=rtl)?selection_end(*c):selection_start(*c);
    else{
        bool moved=false;size_t m=L->move_visual_on(c->cursor,li,dir,&moved);
        if(moved)p=unsigned(m);
        else if(d.tag(c->node,"textarea")){
            bool forward=(dir>0)!=rtl;
            size_t nl=forward?li+1:li-1;
            if(forward?li+1<L->line_count():li>0){
                if(forward||!((L->lines()[nl].paragraph_level^L->lines()[li].paragraph_level)&1))
                    p=unsigned(L->line_edge(nl,-dir));
            }
        }
    }
    form_move(d,*c,p,shift);c->preferred_x=-1;return true;
#else
    (void)d;(void)key;(void)shift;(void)metrics;return false;
#endif
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
