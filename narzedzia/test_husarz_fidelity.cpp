#include "../programy/husarz/paint.h"
#include "../programy/husarz/resource.h"
#include "../programy/husarz/async.h"
#include "../programy/husarz/script.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <string>
using namespace Husarz;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
static Metrics metrics;
static void load(Document &d,const char *s,int width=320) {
    parse_html(d,s,std::strlen(s)); collect_styles(d); prepare_controls(d); reflow(d,width,metrics);
}
static uint16_t find(Document &d,const char *id) { return ScriptDom(d).get_element_by_id(id).id; }
static bool match(Document &d,const char *id,const char *sel) {
    uint32_t budget=400000; return matches(d,find(d,id),selector({sel,std::strlen(sel)}),budget);
}
static std::vector<uint32_t> pixels(Document &d) {
    std::vector<uint32_t> out(320*240); Surface surface{out.data(),320,240}; paint(d,surface,0,metrics,{}); CHECK(!surface.truncated); return out;
}
int main() {
    auto owned=std::make_unique<Document>(); auto &d=*owned;
    struct ColorCase {const char* text; uint32_t value; unsigned alpha;};
    for (auto c : {ColorCase{"#abc",0xaabbcc,255},{"#abcd",0xaabbcc,221},{"#10203080",0x102030,128},
         {"rgba(10,20,30,.5)",0x0a141e,127},{"rgb(100%,0%,0%)",0xff0000,255},
         {"hsl(120,100%,50%)",0x00ff00,255},{"hsla(240,100%,50%,.25)",0x0000ff,63},
         {"rebeccapurple",0x663399,255},{"transparent",0,0}}) {
        uint32_t rgb=0; uint8_t alpha=0; bool tr=false;
        CHECK(color({c.text,std::strlen(c.text)},rgb,tr,&alpha)); CHECK(rgb==c.value); CHECK(alpha==c.alpha); CHECK(tr==(!alpha));
    }
    for (auto text : {"#12","#abcdx","rgb(1,2)","rgba(1,2,3,0.5,2)","hsl(120,3,4)","rgb(1e9,2,3)"}) {
        uint32_t rgb; bool tr; CHECK(!color({text,std::strlen(text)},rgb,tr));
    }
    load(d,"<div><span id=a data-x='alpha beta' title='Hello world'></span> text <b id=b></b><span id=c></span></div><p><i id=only></i></p>");
    for (auto s : {"[data-x]","[data-x='alpha beta']","[data-x~=beta]","[data-x^=alpha]","[data-x$=beta]","[title*=world]","span:first-child","span:first-of-type","span:nth-child(1)","span:nth-child(2n+1)","span:not(.disabled)"}) CHECK(match(d,"a",s));
    for (auto s : {"span + b","span ~ b","b:nth-child(2)","b:nth-last-child(2)","b:not(span)"}) CHECK(match(d,"b",s));
    for (auto s : {"span:last-child","span:last-of-type","span:nth-child(-n+3)"}) CHECK(match(d,"c",s));
    CHECK(match(d,"only","i:only-child"));
    for (auto s : {"[missing]","[data-x~=alp]","span:last-child","span:not(span)","span:nth-child(2)"}) CHECK(!match(d,"a",s));
    for (auto s : {"span:not(:not(span))","div >","[x='unterminated]","span:nth-child(2n+1) +"}) CHECK(!selector({s,std::strlen(s)}).valid);
    load(d,"<style>body{background:#123456;color:#fedcba} p{color:red!important} #a{color:green} div{letter-spacing:2px;word-spacing:3px} span{color:unset} a{color:#551a8b}</style><div><span id=s>Text</span><p id=a style='color:blue'>text</p><a id=l href='/'>Link</a></div>");
    CHECK(d.styles[find(d,"a")].color==0xff0000); CHECK(d.styles[find(d,"s")].color==0xfedcba);
    CHECK(d.styles[find(d,"s")].letter_spacing==2); CHECK(d.styles[find(d,"s")].word_spacing==3); CHECK(d.styles[find(d,"l")].color==0x551a8b);
    CHECK(pixels(d)[0]==0xff123456);
    load(d,"<style>*{font-size:20px} p{margin:0}</style><p id=a>UA origin must lose to author universal rules</p>");
    CHECK(d.styles[1].font==20); CHECK(d.styles[find(d,"a")].margin[0].value==0);
    load(d,"<style>body{margin:0;background:white} div{width:100px;height:40px;background:#00000080}</style><div></div>");
    CHECK(pixels(d)[0]==0xff7f7f7f);
    load(d,"<style>body{margin:0} p{margin:0} .s{font-size:20px}</style><p><span id=a>One</span><strong id=b>Two</strong><em id=c>Three</em></p>");
    CHECK(d.boxes[find(d,"a")].y==d.boxes[find(d,"b")].y); CHECK(d.boxes[find(d,"b")].y==d.boxes[find(d,"c")].y);
    load(d,"<style>body{margin:0} .ib{display:inline-block;width:60px;height:30px} .r{position:relative;left:10px;top:5px;width:100px;height:100px} .a{position:absolute;left:20px;top:10px;width:30px;height:20px}</style><div class=ib id=a>A</div><div class=ib id=b>B</div><div class=r id=r><div class=a id=c>X</div></div>");
    CHECK(d.boxes[find(d,"b")].x==60); CHECK(d.boxes[find(d,"a")].y==d.boxes[find(d,"b")].y);
    CHECK(d.boxes[find(d,"c")].x==d.boxes[find(d,"r")].content_x+20);
    CHECK(d.boxes[find(d,"c")].y==d.boxes[find(d,"r")].content_y+10);
    load(d,"<style>body{margin:0} table{width:300px;border-collapse:collapse}</style><table><tr><td id=a>Long long heading</td><td id=b>X</td></tr><tr><td colspan=2 id=c>Wide</td></tr></table>");
    CHECK(d.boxes[find(d,"a")].width>d.boxes[find(d,"b")].width);
    CHECK(d.boxes[find(d,"c")].width==300);
    reflow(d,200,metrics); CHECK(d.boxes[find(d,"c")].width==300); // explicit width
    load(d,"<form id=f><input id=t name=q><input id=p type=password name=secret><textarea id=ta name=note>Hi</textarea><input id=cb type=checkbox name=check><input id=r1 type=radio name=r value=a><input id=r2 type=radio name=r value=b><select id=sel name=s><option value=one>One</option><option value=two>Two</option></select><button id=submit name=go value=yes>Send</button></form>");
    auto t=find(d,"t"); d.focused_control=t; auto *text=control(d,t); CHECK(text);
    unsigned arena=d.arena_used;
    CHECK(form_insert(d,'Z')); CHECK(form_insert(d,0x17c)); CHECK(text->length==3); CHECK(text->cursor==3);
    CHECK(form_key(d,0x47)); CHECK(text->cursor==0); CHECK(form_key(d,0x4f)); CHECK(text->cursor==3);
    CHECK(form_key(d,0x4b)); CHECK(text->cursor==1); CHECK(form_key(d,0x53)); CHECK(equal(text->value,"Z"));
    CHECK(form_key(d,8)); CHECK(!text->length); CHECK(d.arena_used==arena);
    d.focused_control=find(d,"p"); form_insert(d,0x105); CHECK(equal(d.str(d.nodes[d.focused_control].text),"*"));
    d.focused_control=find(d,"ta"); CHECK(form_key(d,13)); CHECK(form_insert(d,0x142));
    auto *cb=control(d,find(d,"cb")); form_toggle(d,*cb); CHECK(cb->checked);
    auto *r1=control(d,find(d,"r1")),*r2=control(d,find(d,"r2")); form_toggle(d,*r1); form_toggle(d,*r2); CHECK(!r1->checked && r2->checked);
    auto *select=control(d,find(d,"sel"));
    form_select(d,*select,1); CHECK(select->option==1);
    auto select_revision=d.revision;
    form_select(d,*select,1); CHECK(select->option==1); CHECK(d.revision==select_revision);
    form_select(d,*select,-1); form_select(d,*select,-1); CHECK(select->option==0);
    form_select(d,*select,2147483647); CHECK(select->option==1);
    form_select(d,*select,(-2147483647-1)); CHECK(select->option==0);
    form_select(d,*select,1);
    char query[4096]; CHECK(form_query(d,find(d,"f"),find(d,"submit"),query,sizeof(query)));
    CHECK(std::strstr(query,"secret=%C4%85")); CHECK(std::strstr(query,"check=on")); CHECK(std::strstr(query,"r=b")); CHECK(std::strstr(query,"s=two"));
    char tiny[2]; CHECK(!form_query(d,find(d,"f"),0,tiny,sizeof(tiny)));
    // Cache pinning and pressure while two documents share a decoded image.
    ResourceCache cache(std::malloc,std::free); uint8_t raw[16]{};
    auto *entry=cache.insert("http://example.test/a.png",ResourceType::Image,raw,sizeof(raw)); CHECK(entry);
    entry->pixels=static_cast<uint32_t*>(std::malloc(16)); entry->decoded=16; entry->width=2; entry->height=2; cache.decoded_bytes=16;
    for (unsigned i=0;i<4;++i) entry->pixels[i]=0xffff0000;
    auto second=std::make_unique<Document>();
    load(d,"<img id=im width=20>"); CHECK(cache.bind(d,find(d,"im"),*entry)); CHECK(cache.bind(*second,1,*entry)); CHECK(entry->refs==2);
    reflow(d,320,metrics); CHECK(d.boxes[find(d,"im")].content_width==20); CHECK(d.boxes[find(d,"im")].content_height==20);
    CHECK(pixels(d)[8*320+8]==0xffff0000);
    for (unsigned i=0;i<96;++i) { std::string url="http://example.test/"+std::to_string(i); CHECK(cache.insert(url.c_str(),ResourceType::Css,raw,sizeof(raw))); }
    CHECK(entry->refs==2 && entry->pixels[0]==0xffff0000); CHECK(cache.evictions>0);
    d.reset(); CHECK(entry->refs==1); second.reset(); CHECK(entry->refs==0);
    CHECK(cache.find("http://example.test/a.png",ResourceType::Image)==entry); CHECK(cache.hits==1);
    CHECK(!cache.insert("http://example.test/big",ResourceType::Image,raw,MaxResourceBytes+1));
    CHECK(!cache.insert(nullptr,ResourceType::Css,raw,sizeof(raw))); CHECK(!cache.insert("",ResourceType::Css,raw,sizeof(raw)));
    CHECK(cache.insert("http://example.test/expire",ResourceType::Css,raw,sizeof(raw),100));
    CHECK(!cache.find("http://example.test/expire",ResourceType::Css,UINT64_C(60000000101)));
    char normalized[2048];
    CHECK(normalize_resource_url("HTTP://Example.TEST/a/../b.css#anchor",normalized,sizeof(normalized)));
    CHECK(equal(normalized,"http://example.test/b.css"));
    CHECK(!normalize_resource_url("file:///etc/passwd",normalized,sizeof(normalized)));
    auto pending=std::make_unique<ResourceQueue>(); ResourceTask task{};
    task.tab=7; task.navigation=10; task.document=20; task.kind=RequestKind::Image;
    for (unsigned i=0;i<MaxPendingRequests;++i) CHECK(pending->push(task));
    CHECK(!pending->push(task));
    CHECK(ResourceQueue::valid(task,7,10,20));
    CHECK(!ResourceQueue::valid(task,7,11,20)); CHECK(!ResourceQueue::valid(task,8,10,20)); CHECK(!ResourceQueue::valid(task,7,10,21));
    pending->cancel(7); CHECK(!pending->count); CHECK(pending->discarded==MaxPendingRequests);
    for (unsigned sample=0;sample<400;++sample) {
        task.kind=sample%2 ? RequestKind::Css : RequestKind::Image;
        task.tab=sample%6+1; task.navigation=sample/6+1; task.document=sample+1;
        CHECK(pending->push(task)); ResourceTask completion{}; CHECK(pending->pop(completion));
        CHECK(!ResourceQueue::valid(completion,task.tab,task.navigation+1,task.document));
        CHECK(!ResourceQueue::valid(completion,task.tab,task.navigation,task.document+1));
    }
    // Bounded malformed selectors, declarations, form input and cache metadata.
    uint32_t random=0x12345678;
    for (unsigned sample=0;sample<600;++sample) {
        char bytes[97]; for (unsigned j=0;j<96;++j) { random=random*1664525+1013904223; bytes[j]=char(32+(random>>24)%95); } bytes[96]=0;
        auto sel=selector({bytes,96}); uint32_t budget=200;
        (void)matches(d,0,sel,budget); parse_css(d,bytes,96);
        if (sample%20==0) load(d,"<input id=t>");
        d.focused_control=find(d,"t"); form_insert(d,random%0x110000); form_key(d,sample%2?8:0x53);
        CHECK(d.arena_used<=ArenaSize); CHECK(cache.raw_bytes<=MaxCacheRaw); CHECK(cache.decoded_bytes<=MaxCacheDecoded);
    }
    std::printf("Husarz fidelity: %u checks PASS; cache hits=%llu misses=%llu evictions=%llu\n",checks,(unsigned long long)cache.hits,(unsigned long long)cache.misses,(unsigned long long)cache.evictions);
}
