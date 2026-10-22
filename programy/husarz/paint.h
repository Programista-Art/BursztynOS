#pragma once
#include "layout.h"
namespace Husarz {
struct Surface {
    uint32_t *pixels{};
    int width{}, height{};
    uint32_t budget{16000000};
    bool truncated{};
    void pixel(int x, int y, uint32_t color, unsigned alpha) {
        if (!budget) {
            truncated = true;
            return;
        }
        --budget;
        if (x < 0 || y < 0 || x >= width || y >= height)
            return;
        uint32_t &dest = pixels[size_t(y) * width + x];
        if (alpha >= 255)
            dest = 0xff000000 | color;
        else {
            unsigned inverse = 255 - alpha;
            uint32_t r = (((color >> 16) & 255) * alpha + ((dest >> 16) & 255) * inverse + 127) / 255,
                     g = (((color >> 8) & 255) * alpha + ((dest >> 8) & 255) * inverse + 127) / 255,
                     b = ((color & 255) * alpha + (dest & 255) * inverse + 127) / 255;
            dest = 0xff000000 | (r << 16) | (g << 8) | b;
        }
    }
    void clear(uint32_t color) {
        for (size_t i = 0; i < size_t(width) * height; ++i)
            pixels[i] = 0xff000000 | color;
    }
    static bool rounded(int x, int y, int w, int h, int radius) {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return false;
        if (radius > w / 2)
            radius = w / 2;
        if (radius > h / 2)
            radius = h / 2;
        if (radius <= 0)
            return true;
        int dx = x < radius        ? radius - 1 - x
                 : x >= w - radius ? x - (w - radius)
                                   : 0,
            dy = y < radius        ? radius - 1 - y
                 : y >= h - radius ? y - (h - radius)
                                   : 0;
        return int64_t(dx) * dx + int64_t(dy) * dy < int64_t(radius) * radius;
    }
    void rect(int x, int y, int w, int h, uint32_t color, unsigned alpha, Clip clip, int radius = 0) {
        Clip c = intersect({x, y, w, h}, intersect(clip, {0, 0, width, height}));
        for (int yy = c.y; yy < c.y + c.height && budget; ++yy)
            for (int xx = c.x; xx < c.x + c.width && budget; ++xx) {
                --budget;
                if (rounded(xx - x, yy - y, w, h, radius) && budget)
                    pixel(xx, yy, color, alpha);
            }
        if (!budget)
            truncated = true;
    }
};
struct Glyphs {
    void *user{};
    const uint16_t *(*bitmap)(void *, uint32_t){};
    int (*width)(void *, uint32_t){};
};
inline Clip ancestor_clip(const Document &d, uint16_t id, int scroll, int vw, int vh) {
    Clip clip{0, 0, vw, vh};
    for (uint16_t p = d.nodes[id].parent; p; p = d.nodes[p].parent) {
        const Style &s = d.styles[p];
        const auto &b = d.boxes[p];
        Clip c = clip;
        if (s.overflow_x != Overflow::Visible) {
            c.x = b.x;
            c.width = b.width;
        }
        if (s.overflow_y != Overflow::Visible) {
            c.y = b.y - scroll;
            c.height = b.height;
        }
        clip = intersect(clip, c);
    }
    return clip;
}
struct CollapsedBorder { uint16_t node{}; uint8_t side{}; int width{}; };
inline CollapsedBorder border_winner(const Document &d, CollapsedBorder a, uint16_t node, unsigned side) {
    if (!node) return a;
    const auto &s=d.styles[node]; int width=s.border_style?s.border[side]:0;
    if (width<=0) return a;
    // Width, then solid/dashed/dotted style, then deterministic earlier DOM order.
    unsigned rank=s.border_style==1?3:s.border_style==2?2:1;
    unsigned old_rank=a.node?(d.styles[a.node].border_style==1?3:d.styles[a.node].border_style==2?2:1):0;
    if (width>a.width || (width==a.width && (rank>old_rank || (rank==old_rank && node<a.node))))
        return {node,uint8_t(side),width};
    return a;
}
inline void paint_collapsed_edge(const Document &d, Surface &surface, CollapsedBorder edge,
                                int x,int y,int extent,bool horizontal,Clip clip) {
    if (!edge.node || extent<=0) return;
    const auto &s=d.styles[edge.node]; unsigned alpha=unsigned(s.paint_opacity)*s.border_alpha/255;
    if (!s.visible || !alpha) return;
    if (s.border_style==1) {
        surface.rect(x-(horizontal?0:edge.width/2),y-(horizontal?edge.width/2:0),
                     horizontal?extent:edge.width,horizontal?edge.width:extent,s.border_color,alpha,clip);
        return;
    }
    int step=s.border_style==2?6:2;
    // Iteration follows visible pixels only, even for enormous CSS coordinates.
    Clip line=intersect(clip,{0,0,surface.width,surface.height});
    int first=horizontal?line.x-x:line.y-y, last=horizontal?line.x+line.width-x:line.y+line.height-y;
    if (first<0) first=0;
    if (last>extent) last=extent;
    for (int i=first;i<last && surface.budget;++i) if (!((i/step)&1))
        surface.rect(x+(horizontal?i:-edge.width/2),y+(horizontal?-edge.width/2:i),
                     horizontal?1:edge.width,horizontal?edge.width:1,s.border_color,alpha,clip);
}
inline void paint_collapsed_tables(const Document &d, Surface &surface, int scroll) {
    for (unsigned t=0;t<d.table_count && surface.budget;++t) {
        const auto &p=d.tables[t]; if (!p.rows || !d.styles[p.node].border_collapse) continue;
        const auto &b=d.boxes[p.node]; Clip clip=ancestor_clip(d,p.node,scroll,surface.width,surface.height);
        for (unsigned r=0;r<=p.rows && surface.budget;++r) {
            int y=r<p.rows?d.boxes[p.row_nodes[r]].y:d.boxes[p.row_nodes[p.rows?p.rows-1:0]].y+(p.rows?p.heights[p.rows-1]:0);
            int x=b.content_x;
            for (unsigned c=0;c<p.columns && surface.budget;++c) {
                uint16_t above=r?p.grid[(r-1)*MaxTableColumns+c]:0, below=r<p.rows?p.grid[r*MaxTableColumns+c]:0;
                CollapsedBorder edge;
                if (above!=below) { edge=border_winner(d,edge,above,2); edge=border_winner(d,edge,below,0); }
                if (!r || r==p.rows) edge=border_winner(d,edge,p.node,r?2:0);
                paint_collapsed_edge(d,surface,edge,x,y-scroll,p.widths[c],true,clip); x+=p.widths[c];
            }
        }
        int x=b.content_x;
        for (unsigned c=0;c<=p.columns && surface.budget;++c) {
            for (unsigned r=0;r<p.rows && surface.budget;++r) {
                uint16_t left=c?p.grid[r*MaxTableColumns+c-1]:0, right=c<p.columns?p.grid[r*MaxTableColumns+c]:0;
                CollapsedBorder edge;
                if (left!=right) { edge=border_winner(d,edge,left,1); edge=border_winner(d,edge,right,3); }
                if (!c || c==p.columns) edge=border_winner(d,edge,p.node,c?1:3);
                paint_collapsed_edge(d,surface,edge,x,d.boxes[p.row_nodes[r]].y-scroll,p.heights[r],false,clip);
            }
            if (c<p.columns) x+=p.widths[c];
        }
    }
}
inline void paint_image(const Document &d, uint16_t id, Surface &target, int scroll, Clip clip, bool background) {
    const auto *image=d.image(id,background); if (!image) return;
    const auto &b=d.boxes[id]; const auto &s=d.styles[id];
    int x=background ? b.x : b.content_x, y=(background ? b.y : b.content_y)-scroll;
    int w=background ? int(image->width) : b.content_width, h=background ? int(image->height) : b.content_height;
    Clip area=background ? Clip{b.x,b.y-scroll,b.width,b.height} : Clip{x,y,w,h};
    if (background && s.background_size) {
        Span size{d.str(s.background_size),length(d.str(s.background_size),128)};
        if (value_is(size,"cover") || value_is(size,"contain")) {
            int64_t sx=int64_t(b.width)*1000/image->width, sy=int64_t(b.height)*1000/image->height;
            int64_t scale=value_is(size,"cover") ? (sx>sy?sx:sy) : (sx<sy?sx:sy);
            w=clamp64(int64_t(image->width)*scale/1000,1); h=clamp64(int64_t(image->height)*scale/1000,1);
        } else {
            Span parts[2]; unsigned count=words(size,parts,2); Length a{},c{};
            if (count && count<=2 && css_length(parts[0],a)) {
                if (count==2) css_length(parts[1],c);
                if (a.unit!=Unit::Auto) w=clamp64(resolve(a,b.width,s.font,d.root_font),1);
                if (c.unit!=Unit::Auto) h=clamp64(resolve(c,b.height,s.font,d.root_font),1);
                else h=clamp64(int64_t(w)*image->height/image->width,1);
                if (a.unit==Unit::Auto && c.unit!=Unit::Auto) w=clamp64(int64_t(h)*image->width/image->height,1);
            }
        }
    }
    if (background && s.background_position) {
        Span parts[2]; unsigned count=words({d.str(s.background_position),length(d.str(s.background_position),128)},parts,2);
        for (unsigned i=0;i<2 && count<=2;++i) {
            Span part=i<count ? parts[i] : Span{"center",6}; Length l; int free=i ? b.height-h : b.width-w, pos=0;
            if (value_is(part,"center")) pos=free/2;
            else if (value_is(part,"right") || value_is(part,"bottom")) pos=free;
            else if (css_length(part,l,true)) pos=resolve(l,free,s.font,d.root_font);
            if (i) y+=pos; else x+=pos;
        }
    }
    if (w<=0 || h<=0) return;
    Clip visible=intersect(area,intersect(clip,{0,0,target.width,target.height}));
    for (int yy=visible.y;yy<visible.y+visible.height && target.budget;++yy)
        for (int xx=visible.x;xx<visible.x+visible.width && target.budget;++xx) {
            --target.budget;
            int dx=xx-x,dy=yy-y;
            bool repeat_x=background && (s.background_repeat==0 || s.background_repeat==1);
            bool repeat_y=background && (s.background_repeat==0 || s.background_repeat==2);
            if ((!repeat_x && (dx<0 || dx>=w)) || (!repeat_y && (dy<0 || dy>=h))) continue;
            if (background && !Surface::rounded(xx-b.x,yy-(b.y-scroll),b.width,b.height,s.radius)) continue;
            dx=(dx%w+w)%w; dy=(dy%h+h)%h;
            unsigned sx=unsigned(int64_t(dx)*image->width/w),sy=unsigned(int64_t(dy)*image->height/h);
            uint32_t pixel=image->pixels[size_t(sy)*image->width+sx];
            target.pixel(xx,yy,pixel,unsigned(pixel>>24)*s.paint_opacity/255);
        }
}
inline void paint_menu_label(Surface &target,const char *text,int x,int y,const Style &style,
                             const Metrics &metrics,const Glyphs &glyphs,Clip clip) {
    size_t size=length(text,MaxAttrLength);
    for (size_t p=0;p<size && target.budget && x<clip.x+clip.width;) {
        size_t used; uint32_t cp=utf8(text+p,size-p,used); p+=used;
        const uint16_t *bits=glyphs.bitmap?glyphs.bitmap(glyphs.user,cp):nullptr;
        int gw=glyphs.width?glyphs.width(glyphs.user,cp):8;
        int dw=clamp64((int64_t(gw)*style.font+8)/16,1,256),dh=font_height(metrics,style);
        if (bits) for (int yy=0;yy<dh && target.budget;++yy) for (int xx=0;xx<dw && target.budget;++xx) {
            --target.budget;
            int sx=xx*gw/dw; if(sx>15)sx=15;
            if ((bits[yy*16/dh]&(1U<<(15-sx))) && x+xx>=clip.x && x+xx<clip.x+clip.width && y+yy>=clip.y && y+yy<clip.y+clip.height)
                target.pixel(x+xx,y+yy,style.color,unsigned(style.color_alpha)*style.paint_opacity/255);
        }
        x=clamp64(int64_t(x)+advance(metrics,cp,style),-MaxDimension);
    }
}
inline void paint(Document &d, Surface &target, int scroll, const Metrics &metrics, const Glyphs &glyphs) {
    target.clear(d.user_agent.canvas);
    uint16_t canvas_node = 0;
    for (uint16_t i = 1; i < d.node_count; ++i)
        if ((d.tag(i, "html") || d.tag(i, "body")) && d.styles[i].has_background && d.styles[i].visible && d.styles[i].display!=Display::None) {
            const Style &s = d.styles[i];
            target.rect(0, 0, target.width, target.height, s.background, unsigned(s.background_alpha)*s.paint_opacity/255,
                        {0, 0, target.width, target.height});
            canvas_node = i;
            break;
        }
    uint32_t fi = 0;
    for (uint16_t id = 0; id < d.node_count; ++id) {
        const Style &s = d.styles[id];
        const Box &b = d.boxes[id];
        if (s.display == Display::None)
            continue;
        Clip clip = ancestor_clip(d, id, scroll, target.width, target.height);
        if (s.visible && s.paint_opacity && b.height && b.width) {
            if (s.has_background && id != canvas_node)
                target.rect(b.x, b.y - scroll, b.width, b.height, s.background, unsigned(s.paint_opacity) * s.background_alpha / 255, clip,
                            s.radius);
            paint_image(d,id,target,scroll,clip,true);
            paint_image(d,id,target,scroll,clip,false);
            if (auto *control_state=control(d,id)) {
                if (control_state->checked && (input_type(d,id,"checkbox") || input_type(d,id,"radio"))) {
                    int cx=b.content_x,cy=b.content_y-scroll;
                    Clip control_clip=intersect(clip,{cx,cy,b.content_width,b.content_height});
                    if (input_type(d,id,"radio")) target.rect(cx+3,cy+3,7,7,s.color,s.color_alpha,control_clip,4);
                    else {
                        for(int k=0;k<4;++k) target.rect(cx+2+k,cy+6+k,2,2,s.color,s.color_alpha,control_clip);
                        for(int k=0;k<6;++k) target.rect(cx+5+k,cy+9-k,2,2,s.color,s.color_alpha,control_clip);
                    }
                }
            }
            const auto *collapsed=table_plan(d,id);
            bool collapsed_part=collapsed && d.styles[collapsed->node].border_collapse &&
                (id==collapsed->node || d.table_cell[id].table==collapsed->node);
            if (s.border_style && !collapsed_part) {
                int y = b.y - scroll;
                Clip c = intersect({b.x, y, b.width, b.height}, clip);
                int innerw = b.width - s.border[1] - s.border[3],
                    innerh = b.height - s.border[0] - s.border[2];
                int r = s.radius - s.border[0];
                if (r < 0)
                    r = 0;
                for (int yy = c.y; yy < c.y + c.height && target.budget; ++yy)
                    for (int xx = c.x; xx < c.x + c.width && target.budget; ++xx) {
                        --target.budget;
                        int dx = xx - b.x, dy = yy - y;
                        if (!Surface::rounded(dx, dy, b.width, b.height, s.radius) ||
                            Surface::rounded(dx - s.border[3], dy - s.border[0], innerw, innerh, r))
                            continue;
                        if (s.border_style > 1) {
                            int step = s.border_style == 2 ? 6 : 2;
                            if (((dy < s.border[0] || dy >= b.height - s.border[2] ? dx : dy) / step) & 1)
                                continue;
                        }
                        target.pixel(xx, yy, s.border_color, unsigned(s.paint_opacity) * s.border_alpha / 255);
                    }
            }
        }
        while (fi < d.fragment_count && d.fragments[fi].node < id)
            ++fi;
        while (fi < d.fragment_count && d.fragments[fi].node == id) {
            const Fragment &f = d.fragments[fi++];
            if (!s.visible || !s.paint_opacity)
                continue;
            Clip fc = intersect({f.clip.x, f.clip.y - scroll, f.clip.width, f.clip.height},
                                {0, 0, target.width, target.height});
            if (f.y - scroll >= target.height || f.y - scroll + f.height <= 0)
                continue;
            int x = f.x, y = f.y - scroll;
            size_t p = 0;
            const char *text = d.str(f.text);
            while (p < f.length && target.budget) {
                size_t used;
                uint32_t cp = utf8(text + p, f.length - p, used);
                cp = transformed(cp,s.transform,p == 0);
                int adv = advance(metrics, cp, s);
                int gw = glyphs.width ? glyphs.width(glyphs.user, cp) : 8;
                const uint16_t *bitmap = glyphs.bitmap ? glyphs.bitmap(glyphs.user, cp) : nullptr;
                int dw = clamp64((int64_t(gw) * s.font + 8) / 16, 1, 256), dh = font_height(metrics, s);
                if (bitmap)
                    for (int py = 0; py < dh && target.budget; ++py) {
                        int sy = py * 16 / dh, skew = s.italic ? (dh - 1 - py) / 5 : 0;
                        for (int px = 0; px < dw && target.budget; ++px) {
                            --target.budget;
                            int sx = px * gw / dw;
                            if (sx > 15)
                                sx = 15;
                            if (!(bitmap[sy] & (1U << (15 - sx))))
                                continue;
                            int tx = x + px + skew, ty = y + py;
                            if (tx >= fc.x && tx < int64_t(fc.x) + fc.width && ty >= fc.y &&
                                ty < int64_t(fc.y) + fc.height) {
                                target.pixel(tx, ty, s.color, unsigned(s.paint_opacity) * s.color_alpha / 255);
                                if (s.bold && tx + 1 < int64_t(fc.x) + fc.width)
                                    target.pixel(tx + 1, ty, s.color, unsigned(s.paint_opacity) * s.color_alpha / 255);
                            }
                        }
                    }
                x = clamp64(int64_t(x) + adv, -MaxDimension);
                p += used;
            }
            if (s.underline)
                target.rect(f.x, y + f.height - 1, f.width, 1, s.color, unsigned(s.paint_opacity) * s.color_alpha / 255, fc);
        }
    }
    paint_collapsed_tables(d,target,scroll);
    if (d.focused_control) {
        auto *c=control(d,d.focused_control);
        if (c) {
            const auto &b=d.boxes[c->node]; const auto &s=d.styles[c->node];
            Clip clip=ancestor_clip(d,c->node,scroll,target.width,target.height);
            clip=intersect(clip,{b.content_x,b.content_y-scroll,b.content_width,b.content_height});
            if (editable(d,*c)) {
                unsigned cursor=c->cursor;
                if (input_type(d,c->node,"password")) {
                    cursor=0; for (size_t p=0;p<c->cursor;) { size_t used; utf8(c->value+p,c->cursor-p,used); p+=used; ++cursor; }
                }
                int x=b.content_x,y=b.content_y-scroll;
                for (uint32_t i=0;i<d.fragment_count;++i) {
                    const auto &f=d.fragments[i]; if (f.node!=c->node) continue;
                    unsigned start=f.text-c->display;
                    if (cursor>=start && cursor<=start+f.length) {
                        x=f.x; y=f.y-scroll;
                        for (size_t p=0;p<cursor-start;) { size_t used; uint32_t cp=utf8(d.str(f.text)+p,cursor-start-p,used); x+=advance(metrics,cp,s); p+=used; }
                        break;
                    }
                }
                target.rect(x,y,1,s.font,s.color,s.color_alpha,clip);
            }
        }
    }
    if (d.open_select) {
        auto *c=control(d,d.open_select);
        if (c) {
            auto menu=select_popup(d,c->node,scroll,target.width,target.height);
            const auto &s=d.styles[c->node]; Clip clip{menu.x,menu.y,menu.width,menu.height};
            target.rect(menu.x,menu.y,menu.width,menu.height,s.border_color,255,clip);
            for (int row=0;row<menu.rows;++row) {
                unsigned index=menu.start+unsigned(row); if(index>=menu.count)break;
                uint16_t option=menu.options[index], text=d.nodes[option].first;
                Style row_style=s;
                if (d.styles[option].has_background) row_style.background=d.styles[option].background;
                else if (!s.has_background) row_style.background=0xffffff;
                if(index==c->option) row_style.background=0xb9d7ff;
                Clip row_clip{menu.x+1,menu.y+row*menu.row+1,menu.width-2,menu.row-2};
                target.rect(row_clip.x,row_clip.y,row_clip.width,row_clip.height,row_style.background,255,row_clip);
                if(text) paint_menu_label(target,d.str(d.nodes[text].text),row_clip.x+4,row_clip.y+3,row_style,metrics,glyphs,row_clip);
            }
        }
    }
    if (!target.budget)
        target.truncated = true;
    if (target.truncated)
        d.truncated = true;
}
} // namespace Husarz
