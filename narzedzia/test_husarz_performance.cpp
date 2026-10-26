#define HUSARZ_PERF 1
#include "../programy/husarz/pipeline.h"
#include "../programy/husarz/work_clock.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
using namespace Husarz;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static unsigned drain(Work work){WorkDriver driver;unsigned steps=0;while(!work.step(driver)){CHECK(driver.units<=MaxSliceWork);CHECK(++steps<100000);}CHECK(!driver.failed);CHECK(driver.maximum<=MaxSliceWork);return steps;}
static Work load(Document &d,const std::string &s){co_await parse_html_work(d,s.data(),s.size());co_await collect_styles_work(d);co_await prepare_controls_work(d);co_await reflow_work(d,780,{});}
static uint64_t hash(const std::vector<uint32_t> &p){uint64_t h=1469598103934665603ULL;for(auto v:p)h=(h^v)*1099511628211ULL;return h;}
static const uint16_t *solid_glyph(void*,uint32_t){static const uint16_t bits[16]={0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff};return bits;}
static int solid_width(void*,uint32_t){return 16;}
static const Glyphs solid{nullptr,solid_glyph,solid_width};
static uint64_t diagnostic_clock_value{},budget_clock_value{};
static uint64_t budget_clock(){return budget_clock_value+=3000000;}
static uint64_t diagnostic_clock(){return diagnostic_clock_value+=100;}
static Work diagnostic_timing(Performance &stats){
    PerfScope scope(stats,Stage::Html);
    for(unsigned i=0;i<MaxSliceWork*2;++i)co_await WorkPoint{1};
}
int main(){
    // Stage timing must remain available when the optional budget clock is invalid.
    Performance diagnostic_stats;WorkDriver diagnostic_driver;diagnostic_driver.allow_clock=false;
    perf_clock=diagnostic_clock;
    {auto task=diagnostic_timing(diagnostic_stats);while(!task.step(diagnostic_driver)){};}
    CHECK(!diagnostic_driver.clock && diagnostic_driver.slices>1);
    CHECK(diagnostic_driver.cpu>0 && diagnostic_stats.duration[unsigned(Stage::Html)]>0);
    WorkDriver budget_driver;budget_driver.allow_clock=false;budget_driver.clock=budget_clock;
    {auto task=diagnostic_timing(diagnostic_stats);
        CHECK(!task.step(budget_driver) && budget_driver.units==ClockPollWork);
        while(!task.step(budget_driver)){};
    }
    CHECK(budget_driver.maximum==ClockPollWork);
    perf_clock=nullptr;
    WorkClock timer;timer.sample(1000000,3000000);CHECK(!timer.valid);timer.sample(11000000,33000000);CHECK(timer.valid);CHECK(timer.read(36000000)==12000000);
    timer.sample(21000000,1000);CHECK(!timer.valid);
    ViewportRequest viewport;for(int i=0;i<100;++i)CHECK(viewport.request(800+i,600+i));
    CHECK(viewport.received==100 && viewport.coalesced==99);CHECK(viewport.width==899 && viewport.height==699);
    CHECK(!viewport.request(899,699));CHECK(!viewport.request(0,0));CHECK(viewport.width==899);
    LayoutIdentity identity{1,2,3,4,5,6};auto newer=identity;++newer.viewport;CHECK(!(identity==newer));++newer.navigation;CHECK(!(identity==newer));
    CHECK(escalate_dirty(DirtyPaint)==DirtyPaint);CHECK(escalate_dirty(DirtyStyle)==15);
    DirtyRectangles dirty;dirty.clear();dirty.add({1,2,10,10});dirty.add({8,9,10,10});CHECK(dirty.count==1 && dirty.rectangles[0].width==17);
    dirty.clear();for(int i=0;i<9;++i)dirty.add({i*20,0,5,5});CHECK(dirty.full && !dirty.count);
    auto d=std::make_unique<Document>();CHECK(d->tables.bytes()==0);
    std::string source="<style>.a{color:red}DIV.a{padding:1px}div.a span{color:blue}#id{padding:3px}:not(.b){border-width:0}div+div{margin:1px}[data-k=x]{color:green}</style><div id=id class='a b' data-k=x><span>First</span></div><div class=a>Second</div>";
    drain(load(*d,source));auto indexed=std::make_unique<Style[]>(d->node_count);
    for(unsigned i=0;i<d->node_count;++i)indexed[i]=d->styles[i];
    unsigned resize_reflows=d->reflow_count;auto selectors=d->perf.selector_checks;
    drain(reflow_work(*d,viewport.width,{}));
    ++viewport.performed;viewport.pending=false;
    CHECK(d->reflow_count==resize_reflows+1 && viewport.performed==1);
    CHECK(d->layout_width==899 && d->perf.selector_checks==selectors);
    compute_styles(*d,false);for(unsigned i=0;i<d->node_count;++i)CHECK(indexed[i].color==d->styles[i].color && indexed[i].font==d->styles[i].font && indexed[i].display==d->styles[i].display && indexed[i].padding[0].value==d->styles[i].padding[0].value && indexed[i].margin[0].value==d->styles[i].margin[0].value);
    SelectorIndex index;for(unsigned i=0;i<d->rule_count;++i)index.add(*d,i);
    for(uint16_t n=0;n<d->node_count;++n){uint64_t bits[SelectorIndex::Words];index.candidates(*d,n,bits);
        for(unsigned r=0;r<d->rule_count;++r){uint32_t budget=4000000;bool match=matches(*d,n,index.parsed[r],budget);if(match)CHECK(bits[r/64]&(UINT64_C(1)<<(r%64)));}}
    for(const char *fixture:{"heavy-dom.html","heavy-css.html","heavy-table.html","heavy-long.html","heavy-images.html","heavy-mixed.html","bsf-text-heavy.html"}){
        std::ifstream f(std::string("narzedzia/fixtures/husarz/")+fixture);std::string html((std::istreambuf_iterator<char>(f)),{});CHECK(!html.empty());
        unsigned yields=drain(load(*d,html));CHECK(yields>0);CHECK(!d->truncated);CHECK(d->node_count<=MaxNodes);
        if(equal(fixture,"heavy-dom.html"))CHECK(d->node_count>=2000);
        if(equal(fixture,"bsf-text-heavy.html"))CHECK(d->node_count==246);
        if(equal(fixture,"heavy-long.html"))CHECK(d->height>=40000);
        if(equal(fixture,"heavy-table.html"))CHECK(d->table_count==12 && d->tables.bytes()<=MaxTables*sizeof(TablePlan));
        if(equal(fixture,"heavy-dom.html"))CHECK(d->tables.bytes()==0);
        auto stylechecks=d->perf.selector_checks;auto parses=d->parse_count;
        reflow(*d,900,{});CHECK(d->perf.selector_checks==stylechecks && d->parse_count==parses);
        std::vector<uint32_t> pixels(900*500),sliced(900*500);Surface a{pixels.data(),900,500},b{sliced.data(),900,500};
        paint(*d,a,200,{},{});drain(paint_work(*d,b,200,{},{},{0,0,900,500}));CHECK(hash(pixels)==hash(sliced));
        if(equal(fixture,"heavy-long.html"))CHECK(d->perf.paint_culled>d->perf.paint_visible*5);
        auto before=d->perf.selector_checks;Surface c{pixels.data(),900,500};paint(*d,c,500,{},{});CHECK(d->perf.selector_checks==before && d->parse_count==parses);
        WorkDriver cancel_driver;{Work work=reflow_work(*d,600,{});bool complete=work.step(cancel_driver);if(d->node_count>1000)CHECK(!complete);work.cancel();}CHECK(!work_memory);
        std::printf("Husarz performance fixture %s PASS yields=%u nodes=%u\n",fixture,yields,d->node_count);
    }
    drain(load(*d,"<style>input{width:80px;height:24px}</style><div>Before</div><input value='a'><div>After</div>"));
    uint16_t control_node=d->controls[0].node;d->focused_control=control_node;auto full=d->perf.full_reflows;
    auto edit_revision=d->revision;CHECK(form_insert(*d,'b'));CHECK(d->revision==edit_revision && !(d->dirty&4));
    // Retain coverage of the conservative partial geometry fallback as well.
    d->invalidate(control_node,4);CHECK(local_control_update(*d,780)==control_node);
    auto local=std::make_unique<Document>();drain(clone_layout_document(*local,*d));drain(update_control_layout(*local,*d,control_node,{}));
    CHECK(local->height==d->height && local->perf.full_reflows==full && local->perf.partial_reflows==1);
    auto ref=std::make_unique<Document>();drain(clone_layout_document(*ref,*d));reflow(*ref,780,{});
    CHECK(ref->fragment_count==local->fragment_count);for(unsigned i=0;i<ref->fragment_count;++i){auto &a=ref->fragments[i],&b=local->fragments[i];if(a.node!=b.node || a.text!=b.text || a.length!=b.length || a.x!=b.x || a.y!=b.y || a.width!=b.width || a.height!=b.height)std::fprintf(stderr,"fragment %u ref=%u %u %u %d %d %d %d local=%u %u %u %d %d %d %d\n",i,a.node,a.text,a.length,a.x,a.y,a.width,a.height,b.node,b.text,b.length,b.x,b.y,b.width,b.height);CHECK(a.node==b.node && a.text==b.text && a.length==b.length && a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height);}
    drain(load(*d,"<input type=checkbox>"));auto reflows=d->reflow_count;form_toggle(*d,d->controls[0]);CHECK(d->reflow_count==reflows && !(d->dirty&4));
    std::vector<uint32_t> dirty_pixels(780*240),full_pixels(780*240);
    Surface initial{dirty_pixels.data(),780,240};paint(*d,initial,0,{},{});d->damage.clear();
    form_toggle(*d,d->controls[0]);
    for(unsigned i=0;i<d->damage.count;++i){Surface partial{dirty_pixels.data(),780,240};paint(*d,partial,0,{},{},d->damage.rectangles[i]);}
    Surface complete{full_pixels.data(),780,240};paint(*d,complete,0,{},{});CHECK(hash(dirty_pixels)==hash(full_pixels));
    drain(load(*d,"<style>select{width:80px;height:24px}</style><select><option>First</option><option>Second</option></select>"));
    d->focused_control=d->open_select=d->controls[0].node;
    Surface popup_initial{dirty_pixels.data(),780,240};paint(*d,popup_initial,0,{},{});d->damage.clear();
    form_select(*d,d->controls[0],1);
    auto popup_result=std::make_unique<Document>();drain(clone_layout_document(*popup_result,*d));drain(update_control_layout(*popup_result,*d,d->open_select,{}));
    invalidate_select_popup(*popup_result,0,780,240);
    for(unsigned i=0;i<popup_result->damage.count;++i){Surface partial{dirty_pixels.data(),780,240};paint(*popup_result,partial,0,{},{},popup_result->damage.rectangles[i]);}
    Surface popup_full{full_pixels.data(),780,240};paint(*popup_result,popup_full,0,{},{});CHECK(hash(dirty_pixels)==hash(full_pixels));
    drain(load(*d,"<div style='position:relative;left:-70px;font-size:128px;line-height:128px;letter-spacing:-128px'>X</div>"));
    std::vector<uint32_t> ink(160*160);Surface ink_surface{ink.data(),160,160};paint(*d,ink_surface,0,{},solid);CHECK((ink[20*160+8]&0xffffff)==0);
    std::string image_source="<img alt='alt' style='display:block;vertical-align:top;width:80px;height:80px'>";
    drain(load(*d,image_source));uint16_t image_node=0;for(uint16_t i=1;i<d->node_count;++i)if(d->tag(i,"img"))image_node=i;CHECK(image_node);
    Surface old_image{dirty_pixels.data(),780,240};paint(*d,old_image,0,{},solid);d->damage.clear();
    static const uint32_t green[4]={0xff227733,0xff227733,0xff227733,0xff227733};
    d->images[0]={image_node,2,2,green,nullptr,nullptr,false};d->image_count=1;
    auto image_reflows=d->reflow_count;CHECK(publish_fixed_image_paint(*d,image_node,{},solid));d->invalidate(image_node,1);
    for(unsigned i=0;i<d->damage.count;++i){Surface partial{dirty_pixels.data(),780,240};paint(*d,partial,0,{},solid,d->damage.rectangles[i]);}
    auto image_reference=std::make_unique<Document>();parse_html(*image_reference,image_source.data(),image_source.size());collect_styles(*image_reference);prepare_controls(*image_reference);
    image_reference->images[0]={image_node,2,2,green,nullptr,nullptr,false};image_reference->image_count=1;reflow(*image_reference,780,{});
    Surface complete_image{full_pixels.data(),780,240};paint(*image_reference,complete_image,0,{},solid);
    CHECK(hash(dirty_pixels)==hash(full_pixels));CHECK(d->height==image_reference->height && d->reflow_count==image_reflows);
    std::string percentages="<table style='width:100%'><col style='width:70%'><tr><td>A</td><td style='width:30%'>B</td></tr></table>";
    drain(load(*d,percentages));reflow(*d,200,{});
    auto reference=std::make_unique<Document>();parse_html(*reference,percentages.data(),percentages.size());collect_styles(*reference);prepare_controls(*reference);reflow(*reference,200,{});
    CHECK(d->table_count==1 && reference->table_count==1);
    for(unsigned c=0;c<d->tables[0].columns;++c)CHECK(d->tables[0].widths[c]==reference->tables[0].widths[c]);
    drain(load(*d,"<input value='abc' style='width:80px;height:24px'>"));
    d->focused_control=d->controls[0].node;
    Surface caret_initial{dirty_pixels.data(),780,240};paint(*d,caret_initial,0,{},{});d->damage.clear();
    auto caret_revision=d->revision,caret_paint=d->paint_revision,caret_reflows=d->reflow_count;
    CHECK(form_key(*d,0x4b));
    CHECK(d->revision==caret_revision && d->reflow_count==caret_reflows && !(d->dirty&4));
    CHECK(d->paint_revision>caret_paint && d->damage.count==1);
    for(unsigned i=0;i<d->damage.count;++i){Surface partial{dirty_pixels.data(),780,240};paint(*d,partial,0,{},{},d->damage.rectangles[i]);}
    Surface caret_full{full_pixels.data(),780,240};paint(*d,caret_full,0,{},{});CHECK(hash(dirty_pixels)==hash(full_pixels));
    CHECK(form_key(*d,0x47) && form_key(*d,0x4f));
    CHECK(d->revision==caret_revision && d->reflow_count==caret_reflows);
    CHECK(work_memory==0);CHECK(peak_work_memory<=MaxWorkMemory);
    std::printf("Husarz performance: %u checks PASS; deterministic slice cap=%u peakframes=%zu sizeof(Document)=%zu\n",checks,MaxSliceWork,peak_work_memory,sizeof(Document));
}
