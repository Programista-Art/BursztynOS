#pragma once
#include "document.h"
#include "work.h"
#if defined(BSF_NATIVE)
#include "../../bsf/edit.h"
#endif
namespace Husarz {
inline bool editable(const Document &,const FormControl &);
inline bool selectable(const Document &,const FormControl &);
constexpr unsigned MaxFormControls=64, MaxFormValue=2048;
inline bool is_control(const Document &d,uint16_t id) {
    return d.tag(id,"input") || d.tag(id,"textarea") || d.tag(id,"button") || d.tag(id,"select");
}
inline FormControl *control(Document &d,uint16_t id) {
    for (unsigned i=0;i<d.control_count;++i) if (d.controls[i].node==id) return &d.controls[i];
    return nullptr;
}
inline bool input_type(const Document &d,uint16_t id,const char *type) {
    return equal_ci(d.attr(id,"type"),length(d.attr(id,"type"),32),type);
}
inline void control_display(Document &d,FormControl &c) {
    c.reveal_caret=true;
    char *out=d.arena+c.display;
    unsigned n=0;
    if (input_type(d,c.node,"checkbox") || input_type(d,c.node,"radio")) {
        out[n++]=input_type(d,c.node,"radio")?'(':'[';
        out[n++]=c.checked?'x':' ';
        out[n++]=input_type(d,c.node,"radio")?')':']';
    } else if (input_type(d,c.node,"password")) {
        for (size_t i=0;i<c.length;) {
#if defined(BSF_NATIVE)
            i=BSF::next_cluster(c.value,i,c.length);
#else
            size_t used;utf8(c.value+i,c.length-i,used);i+=used;
#endif
            out[n++]='*'; }
    } else { for (;n<c.length;++n) out[n]=c.value[n]; }
    if (!n) out[n++]=' ';
    out[n]=0; d.nodes[c.node].text=c.display; d.nodes[c.node].text_length=n;
    d.invalidate(c.node,selectable(d,c)?1:4);
}
inline Work initialize_controls_work(Document &d) {
    d.control_count=0; d.focused_control=0;
    for (uint16_t id=1;id<d.node_count;++id) { co_await WorkPoint{1}; if (is_control(d,id)) {
        if (d.control_count==MaxFormControls) { d.truncated=true; break; }
        auto &c=d.controls[d.control_count]; c={}; c.node=id; c.checked=d.has_attr(id,"checked");
        // Reserve a stable display buffer once; typing never grows the arena.
        if (ArenaSize-d.arena_used<MaxFormValue+1) { d.truncated=true; break; }
        c.display=d.arena_used; d.arena_used+=MaxFormValue+1;
        const char *value=d.attr(id,"value");
        if (d.tag(id,"textarea") || d.tag(id,"button")) {
            if (d.nodes[id].first && d.nodes[d.nodes[id].first].is_text) value=d.str(d.nodes[d.nodes[id].first].text);
        }
        if (d.tag(id,"select")) {
            unsigned option=0;
            for (uint16_t child=d.nodes[id].first;child && option<64;child=d.nodes[child].next) { co_await WorkPoint{1}; if (d.tag(child,"option")) {
                if (!option || d.has_attr(child,"selected")) {
                    c.option=uint16_t(option);
                    uint16_t text=d.nodes[child].first; value=text?d.str(d.nodes[text].text):"";
                }
                ++option;
            } }
        }
        if (!*value && (input_type(d,id,"submit") || d.tag(id,"button"))) value="Submit";
        size_t source_size=length(value,MaxFormValue);
        if(selectable(d,c)) {
            for(size_t i=0;i<source_size;) {
                co_await WorkPoint{1};size_t used;uint32_t cp=utf8(value+i,source_size-i,used);i+=used;
                if(cp=='\r')continue;
                if((cp=='\n' || cp=='\t') && !d.tag(id,"textarea"))cp=' ';
                char bytes[4];unsigned count=encode(cp,bytes);if(c.length+count>MaxFormValue)break;
                for(unsigned j=0;j<count;++j)c.value[c.length++]=bytes[j];
            }
        } else {
            c.length=uint16_t(source_size);
            for(unsigned i=0;i<c.length;++i){co_await WorkPoint{1};c.value[i]=value[i];}
        }
        c.value[c.length]=0; c.cursor=c.anchor=c.length; ++d.control_count; control_display(d,c);
    } }
}
inline void initialize_controls(Document &d) { finish_work(initialize_controls_work(d)); }

inline bool selectable(const Document &d,const FormControl &c) {
    return !d.has_attr(c.node,"disabled") &&
        (d.tag(c.node,"textarea") || (d.tag(c.node,"input") &&
        (!*d.attr(c.node,"type") || input_type(d,c.node,"text") || input_type(d,c.node,"password"))));
}
inline bool editable(const Document &d,const FormControl &c) {
    return selectable(d,c) && !d.has_attr(c.node,"readonly");
}
// All positions are byte offsets on validated UTF-8 boundaries. Editing loops
// are bounded by MaxFormValue, independent of document size.
inline unsigned form_previous(const FormControl &c,unsigned p) {
    if (p>c.length) p=c.length;
#if defined(BSF_NATIVE)
    return BSF::previous_cluster(c.value,BSF::cluster_floor(c.value,c.length,p));
#else
    if (p) { --p; while (p && (uint8_t(c.value[p])&0xc0)==0x80) --p; }
    return p;
#endif
}
inline unsigned form_next(const FormControl &c,unsigned p) {
    if (p>=c.length) return c.length;
#if defined(BSF_NATIVE)
    return BSF::next_cluster(c.value,p,c.length);
#else
    size_t used; utf8(c.value+p,c.length-p,used); return p+unsigned(used);
#endif
}
inline unsigned selection_start(const FormControl &c) {return c.anchor<c.cursor?c.anchor:c.cursor;}
inline unsigned selection_end(const FormControl &c) {return c.anchor>c.cursor?c.anchor:c.cursor;}
inline void form_move(Document &d,FormControl &c,unsigned p,bool extend) {
    p=p>c.length?c.length:p;
#if defined(BSF_NATIVE)
    p=BSF::cluster_floor(c.value,c.length,p);
#else
    while (p && p<c.length && (uint8_t(c.value[p])&0xc0)==0x80) --p;
#endif
    if(c.cursor==p && (extend || c.anchor==p))return;
    c.cursor=uint16_t(p); if(!extend)c.anchor=c.cursor;
    c.preferred_x=-1;c.reveal_caret=true;d.invalidate(c.node,1);
}
inline bool form_replace(Document &d,FormControl &c,const char *text,size_t size,bool *changed=nullptr) {
    if(changed)*changed=false;
    if(!editable(d,c) || (!text && size))return false;
    unsigned start=selection_start(c),end=selection_end(c);
    unsigned room=MaxFormValue-(c.length-(end-start)),accepted=0;
    // Invalid/truncated UTF-8 terminates the safe prefix. No partial codepoint
    // enters a control, even when clipboard_read cut a longer representation.
    while(accepted<size && accepted<room) {
        size_t used;uint32_t cp=utf8(text+accepted,size-accepted,used);
        if(!text[accepted] || (cp==0xfffd && used==1 && uint8_t(text[accepted])>=128) || used>room-accepted)break;
        if(cp<32 && cp!='\n' && cp!='\t')break;
        if(cp==127 || ((cp=='\n' || cp=='\t') && !d.tag(c.node,"textarea")))break;
        accepted+=unsigned(used);
    }
#if defined(BSF_NATIVE)
    accepted=unsigned(BSF::cluster_prefix(text,size,accepted));
#endif
    if(!accepted && size)return true; // A full field must keep its selection.
    if(!accepted && start==end)return true;
    for(unsigned i=end;i<=c.length;++i) c.value[start+i-end]=c.value[i];
    c.length-=uint16_t(end-start);
    for(unsigned i=c.length+1;i>start;--i)c.value[i+accepted-1]=c.value[i-1];
    for(unsigned i=0;i<accepted;++i)c.value[start+i]=text[i];
    c.length+=uint16_t(accepted);c.cursor=c.anchor=uint16_t(start+accepted);c.preferred_x=-1;
#if defined(BSF_NATIVE)
    c.cursor=c.anchor=uint16_t(BSF::cluster_ceil(c.value,c.length,c.cursor));
#endif
    control_display(d,c);if(changed)*changed=true;return true;
}
inline bool form_insert(Document &d,uint32_t cp,bool *changed=nullptr) {
    if(changed)*changed=false;
    auto *c=control(d,d.focused_control);
    if(!c || !editable(d,*c) || cp<32 || cp==127 || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
    char encoded[4];unsigned n=encode(cp,encoded);return form_replace(d,*c,encoded,n,changed);
}
inline bool form_key(Document &d,unsigned key,bool shift=false,bool ctrl=false,bool *changed=nullptr) {
    if(changed)*changed=false;
    auto *c=control(d,d.focused_control); if(!c || !selectable(d,*c))return false;
    if(ctrl && (key=='a' || key=='A' || key==1)){c->anchor=0;c->cursor=c->length;c->preferred_x=-1;c->reveal_caret=true;d.invalidate(c->node,1);return true;}
    unsigned p=c->cursor;
    if(key==8 || key==0x53) {
        if(!editable(d,*c))return true;
        if(c->anchor==c->cursor) {
            if(key==8)c->anchor=uint16_t(form_previous(*c,p));
            else c->anchor=uint16_t(form_next(*c,p));
        }
        return form_replace(d,*c,nullptr,0,changed);
    }
    if(key==0x4b)p=!shift && c->anchor!=p?selection_start(*c):form_previous(*c,p);
    else if(key==0x4d)p=!shift && c->anchor!=p?selection_end(*c):form_next(*c,p);
    else if(key==0x47) {
        if(!ctrl && d.tag(c->node,"textarea")) {while(p && c->value[p-1]!='\n')--p;}
        else p=0;
    } else if(key==0x4f) {
        if(!ctrl && d.tag(c->node,"textarea")) {while(p<c->length && c->value[p]!='\n')++p;}
        else p=c->length;
    } else if(key==13 && d.tag(c->node,"textarea"))return !editable(d,*c) || form_replace(d,*c,"\n",1,changed);
    else return false;
    form_move(d,*c,p,shift);return true;
}
// Caller supplies the existing OS clipboard; password checks live here as well
// as in GUI routing so alternate callers cannot accidentally disclose a secret.
struct FormClipboard {
    bool (*set)(const char*,const void*,uint64_t){};
    bool (*read)(const char*,void*,uint64_t,uint64_t*){};
};
inline bool form_clipboard(Document &d,unsigned key,FormClipboard clipboard,bool *changed=nullptr) {
    if(changed)*changed=false;
    auto *c=control(d,d.focused_control);if(!c || !selectable(d,*c))return false;
    key=unsigned(lower(char(key)));
    if(key!='c' && key!='x' && key!='v')return false;
    if(key!='c' && !editable(d,*c))return true;
    if(key!='v' && input_type(d,c->node,"password"))return true;
    struct Buffer {char bytes[MaxFormValue+1]{};};
    auto *buffer=new Buffer;if(!buffer)return true;
    if(key=='v') {
        uint64_t n=0;
        if(clipboard.read && clipboard.read("text/plain",buffer->bytes,MaxFormValue,&n) && n<=MaxFormValue) {
            unsigned out=0;
            for(unsigned i=0;i<n && buffer->bytes[i];++i) {
                char ch=buffer->bytes[i];if(ch=='\r') {if(i+1<n && buffer->bytes[i+1]=='\n')continue;ch='\n';}
                if((ch=='\n' || ch=='\t') && !d.tag(c->node,"textarea"))ch=' ';
                buffer->bytes[out++]=ch;
            }
            if(out)form_replace(d,*c,buffer->bytes,out,changed);
        }
    } else {
        unsigned start=selection_start(*c),n=selection_end(*c)-start;
        if(n) {
            for(unsigned i=0;i<n;++i)buffer->bytes[i]=c->value[start+i];
            if(clipboard.set && clipboard.set("text/plain",buffer->bytes,n+1) && key=='x')form_replace(d,*c,nullptr,0,changed);
        }
    }
    // Do not retain pasted passwords in temporary heap storage.
    volatile char *wipe=buffer->bytes;for(unsigned i=0;i<=MaxFormValue;++i)wipe[i]=0;
    delete buffer;return true;
}
inline uint16_t form_owner(const Document &d,uint16_t id) {
    while (id && !d.tag(id,"form")) id=d.nodes[id].parent;
    return id;
}
inline void form_toggle(Document &d,FormControl &c) {
    if (d.has_attr(c.node,"disabled")) return;
    if (input_type(d,c.node,"checkbox")) c.checked=!c.checked;
    else if (input_type(d,c.node,"radio")) {
        for (unsigned i=0;i<d.control_count;++i) {
            auto &other=d.controls[i];
            if (input_type(d,other.node,"radio") && form_owner(d,other.node)==form_owner(d,c.node) && equal(d.attr(other.node,"name"),d.attr(c.node,"name"))) {
                other.checked=other.node==c.node; d.invalidate(other.node,1);
            }
        }
    }
    d.invalidate(c.node,1);
}
struct SelectPopup { int x{}, y{}, width{}, height{}, row{}, rows{}; unsigned start{}, count{}; uint16_t options[64]{}; };
inline SelectPopup select_popup(const Document &d,uint16_t id,int scroll,int width,int height) {
    SelectPopup p; const auto &b=d.boxes[id]; const auto &s=d.styles[id];
    for (uint16_t n=d.nodes[id].first;n && p.count<64;n=d.nodes[n].next) if (d.tag(n,"option")) p.options[p.count++]=n;
    p.row=clamp64(int64_t(s.line)+8,16,1024);
    p.rows=int(p.count)>8?8:int(p.count); if (p.rows>height/p.row) p.rows=height/p.row;
    p.width=clamp64(b.width,1,width); if (p.width<120 && width>=120) p.width=120;
    p.x=clamp64(b.x,0,width-p.width); p.height=p.rows*p.row;
    p.y=b.y+b.height-scroll;
    if (p.y+p.height>height) p.y=b.y-scroll-p.height;
    p.y=clamp64(p.y,0,height-p.height);
    for (unsigned i=0;i<d.control_count;++i) if (d.controls[i].node==id && d.controls[i].option>=unsigned(p.rows) && p.rows)
        p.start=d.controls[i].option-unsigned(p.rows)+1;
    return p;
}
inline void invalidate_select_popup(Document &d,int scroll,int width,int height){
    if(!d.open_select || d.open_select>=d.node_count)return;
    auto popup=select_popup(d,d.open_select,scroll,width,height);
    d.damage.add({popup.x,popup.y+scroll,popup.width,popup.height});
}
inline void choose_option(Document &d,FormControl &c,unsigned index) {
    unsigned count=0; uint16_t selected=0;
    for (uint16_t n=d.nodes[c.node].first;n && count<64;n=d.nodes[n].next) if (d.tag(n,"option")) {
        if (count==index) selected=n;
        ++count;
    }
    if (!selected) return;
    c.option=uint16_t(index); uint16_t text=d.nodes[selected].first;
    const char *value=text?d.str(d.nodes[text].text):"";
    c.length=uint16_t(length(value,MaxFormValue));
    for (unsigned i=0;i<c.length;++i) c.value[i]=value[i];
    c.value[c.length]=0; c.cursor=c.anchor=c.length; control_display(d,c);
}
inline void form_select(Document &d,FormControl &c,int delta) {
    unsigned count=0; uint16_t choices[64]{};
    for (uint16_t n=d.nodes[c.node].first;n && count<64;n=d.nodes[n].next) if (d.tag(n,"option")) choices[count++]=n;
    if (!count) return;
    // Arrow navigation stops at the ends, including repeated native key-downs.
    uint16_t next=uint16_t(clamp64(int64_t(c.option)+delta,0,int(count)-1));
    if (next==c.option) return;
    c.option=next;
    uint16_t text=d.nodes[choices[c.option]].first; const char *value=text?d.str(d.nodes[text].text):"";
    c.length=uint16_t(length(value,MaxFormValue));
    for (unsigned i=0;i<c.length;++i) c.value[i]=value[i];
    c.value[c.length]=0;
    c.cursor=c.anchor=c.length; control_display(d,c);
}
inline bool encode_form_part(char *out,size_t cap,size_t &used,const char *s) {
    static constexpr char digits[]="0123456789ABCDEF";
    for (size_t i=0;s[i];++i) {
        uint8_t ch=uint8_t(s[i]); bool literal=(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='*'||ch=='-'||ch=='.'||ch=='_';
        size_t bytes=literal || ch==' ' ? 1 : 3;
        if (used+bytes>=cap) return false;
        if (literal) out[used++]=char(ch);
        else if (ch==' ') out[used++]='+';
        else { out[used++]='%'; out[used++]=digits[ch>>4]; out[used++]=digits[ch&15]; }
    }
    out[used]=0; return true;
}
inline bool form_query(Document &d,uint16_t form,uint16_t submit,char *out,size_t cap) {
    size_t used=0; if (!cap) return false; out[0]=0;
    for (unsigned i=0;i<d.control_count;++i) {
        const auto &c=d.controls[i]; uint16_t id=c.node;
        const char *name=d.attr(id,"name");
        if(form_owner(d,id)==form && input_type(d,id,"file") && !d.has_attr(id,"disabled"))return false;
        if (form_owner(d,id)!=form || !*name || d.has_attr(id,"disabled") || input_type(d,id,"button") || input_type(d,id,"reset") ||
            ((input_type(d,id,"checkbox") || input_type(d,id,"radio")) && !c.checked) ||
            ((input_type(d,id,"submit") || d.tag(id,"button")) && id!=submit)) continue;
        if (used) { if (used+1>=cap) return false; out[used++]='&'; }
        if (!encode_form_part(out,cap,used,name) || used+1>=cap) return false;
        out[used++]='='; const char *value=c.value;
        if ((input_type(d,id,"checkbox") || input_type(d,id,"radio"))) value=*d.attr(id,"value")?d.attr(id,"value"):"on";
        if (d.tag(id,"select")) {
            unsigned index=0;
            for (uint16_t n=d.nodes[id].first;n;n=d.nodes[n].next) if (d.tag(n,"option")) {
                if (index++==c.option) { if (d.has_attr(n,"value")) value=d.attr(n,"value"); break; }
            }
        }
        if (!encode_form_part(out,cap,used,value)) return false;
    }
    out[used]=0; return true;
}
} // namespace Husarz
