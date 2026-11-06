#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "../programy/husarz/pipeline.h"
using namespace Husarz;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static std::string copied;static bool fail_set;
static bool copy_set(const char *mime,const void *p,uint64_t n){CHECK(!std::strcmp(mime,"text/plain"));if(fail_set)return false;copied.assign(static_cast<const char*>(p),size_t(n));return true;}
static bool copy_read(const char *mime,void *p,uint64_t cap,uint64_t *n){CHECK(!std::strcmp(mime,"text/plain"));*n=copied.size()<cap?copied.size():cap;std::memcpy(p,copied.data(),size_t(*n));return true;}
static FormClipboard clipboard{copy_set,copy_read};
static const uint16_t *bitmap(void*,uint32_t){static const uint16_t bits[16]={0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff};return bits;}
static void drain(Work task){WorkDriver driver;driver.allow_clock=false;unsigned steps=0;while(!task.step(driver)){CHECK(++steps<100000);}CHECK(!driver.failed);CHECK(driver.maximum<=MaxSliceWork);}
static void load(Document &d,const char *html){parse_html(d,html,std::strlen(html));collect_styles(d);prepare_controls(d);reflow(d,400,{});d.dirty=0;d.damage.clear();}
static void valid(const FormControl &c){CHECK(c.length<=MaxFormValue);CHECK(!c.value[c.length]);CHECK(c.cursor<=c.length && c.anchor<=c.length);bool cursor=false,anchor=false;for(unsigned p=0;;){cursor|=p==c.cursor;anchor|=p==c.anchor;if(p==c.length)break;size_t used;auto cp=utf8(c.value+p,c.length-p,used);CHECK(cp!=0xfffd);p+=unsigned(used);CHECK(p<=c.length);}CHECK(cursor && anchor);}
int main(){
 auto doc=std::make_unique<Document>();auto &d=*doc;
 load(d,"<input><textarea></textarea><input type=password>");auto &c=d.controls[0];d.focused_control=c.node;
 const char *polish="aąbćęłńóśźżZ";auto reflows=d.reflow_count,revision=d.revision,arena=d.arena_used;
 CHECK(form_replace(d,c,polish,std::strlen(polish)));valid(c);CHECK(c.cursor==std::strlen(polish));
 CHECK(form_key(d,0x47));CHECK(c.cursor==0 && c.anchor==0);
 CHECK(form_key(d,0x4d,true));CHECK(c.cursor==1 && c.anchor==0);
 CHECK(form_key(d,0x4d,true));CHECK(c.cursor==3 && c.anchor==0);
 CHECK(form_clipboard(d,'c',clipboard));CHECK(copied==std::string("aą\0",4));
 fail_set=true;CHECK(form_clipboard(d,'x',clipboard));CHECK(std::string(c.value)==polish);fail_set=false;
 CHECK(form_clipboard(d,'x',clipboard));CHECK(std::string(c.value)=="bćęłńóśźżZ");CHECK(c.cursor==0 && c.anchor==0);
 CHECK(form_clipboard(d,'v',clipboard));CHECK(std::string(c.value)==polish);valid(c);
 CHECK(form_key(d,0x4f));CHECK(form_key(d,0x4b,true));CHECK(selection_end(c)-selection_start(c)==1);
 CHECK(form_insert(d,'!'));CHECK(std::string(c.value)=="aąbćęłńóśźż!");
 CHECK(form_key(d,'a',false,true));CHECK(form_insert(d,0x142));CHECK(std::string(c.value)=="ł");
 CHECK(form_key(d,0x47));CHECK(form_key(d,0x4d,true));CHECK(form_key(d,8));CHECK(!c.length);
 CHECK(form_replace(d,c,polish,std::strlen(polish)));CHECK(form_key(d,'a',false,true));CHECK(form_key(d,0x53));CHECK(!c.length);
 CHECK(!form_insert(d,0xd800));CHECK(!form_insert(d,0x110000));
 copied=std::string(2047,'a')+"ż";CHECK(form_clipboard(d,'v',clipboard));CHECK(c.length==2047);valid(c);
 copied="b";CHECK(form_clipboard(d,'v',clipboard));CHECK(c.length==2048);valid(c);
 CHECK(form_key(d,'a',false,true));copied="ąć\r\nxyz\tZ";CHECK(form_clipboard(d,'v',clipboard));CHECK(std::string(c.value)=="ąć xyz Z");
 CHECK(form_key(d,'a',false,true));CHECK(form_replace(d,c,nullptr,0));copied="abc\xf0\x9f";CHECK(form_clipboard(d,'v',clipboard));CHECK(std::string(c.value)=="abc");valid(c);
 CHECK(d.revision==revision && d.reflow_count==reflows && d.arena_used==arena && !(d.dirty&4));
 auto &password=d.controls[2];d.focused_control=password.node;CHECK(form_replace(d,password,polish,std::strlen(polish)));CHECK(form_key(d,'a',false,true));
 copied="public";CHECK(form_clipboard(d,'c',clipboard));CHECK(copied=="public");CHECK(form_clipboard(d,'x',clipboard));CHECK(copied=="public" && std::string(password.value)==polish);
 CHECK(form_clipboard(d,'v',clipboard));CHECK(std::string(password.value)=="public");CHECK(std::string(d.str(password.display))=="******");
 auto &ta=d.controls[1];d.focused_control=ta.node;CHECK(form_replace(d,ta,"ąb\nćd\nęf",11));valid(ta);
 CHECK(form_key(d,0x47));CHECK(ta.cursor==8);CHECK(form_key(d,0x47,false,true));CHECK(ta.cursor==0);
 CHECK(control_vertical_key(d,0x50,true,{}));CHECK(ta.anchor==0 && ta.cursor==4);
 CHECK(control_vertical_key(d,0x50,true,{}));CHECK(ta.cursor==8);
 CHECK(form_key(d,0x4f,true));CHECK(ta.cursor==ta.length);CHECK(form_clipboard(d,'c',clipboard));CHECK(copied==std::string("ąb\nćd\nęf\0",12));
 CHECK(form_key(d,0x53));CHECK(!ta.length);
 // Internal viewports keep outer dimensions stable and every point maps to a
 // legal byte offset, including soft wraps and reverse pointer selection.
 load(d,"<input style='width:30px;height:20px'><textarea style='width:30px;height:20px'></textarea>");
 auto &input=d.controls[0];d.focused_control=input.node;CHECK(form_replace(d,input,polish,std::strlen(polish)));auto box=d.boxes[input.node];
 control_keep_caret(d,input,{});CHECK(input.scroll_x>0 && input.scroll_y==0);
 form_key(d,0x47);control_keep_caret(d,input,{});CHECK(input.scroll_x==0);
 control_pointer(d,input,{},box.content_x+9,box.content_y,false);CHECK(input.cursor==1);
 control_pointer(d,input,{},box.content_x,box.content_y,true);CHECK(input.cursor==0 && input.anchor==1);
 CHECK(d.boxes[input.node].width==box.width && d.boxes[input.node].height==box.height);
 auto &multi=d.controls[1];d.focused_control=multi.node;CHECK(form_replace(d,multi,polish,std::strlen(polish)));control_keep_caret(d,multi,{});CHECK(multi.scroll_y>0);
 form_key(d,'a',false,true);
 std::vector<uint32_t> pixels(400*240),reference(400*240);Surface initial{pixels.data(),400,240};drain(paint_work(d,initial,0,{}, {nullptr,bitmap,nullptr},{0,0,400,240}));
 d.damage.clear();form_key(d,0x4b,true);control_keep_caret(d,multi,{});CHECK(!d.damage.full && d.damage.count);
 for(unsigned i=0;i<d.damage.count;++i){Surface partial{pixels.data(),400,240};drain(paint_work(d,partial,0,{}, {nullptr,bitmap,nullptr},d.damage.rectangles[i]));}
 Surface full{reference.data(),400,240};drain(paint_work(d,full,0,{}, {nullptr,bitmap,nullptr},{0,0,400,240}));CHECK(pixels==reference);
 // Deterministic editing fuzz with Polish UTF-8 and selection extension.
 uint32_t random=123;
 for(unsigned i=0;i<4000;++i){random=random*1664525+1013904223;switch(random%9){case 0:form_insert(d,0x105);break;case 1:form_insert(d,'a');break;case 2:form_key(d,0x4b,true);break;case 3:form_key(d,0x4d,true);break;case 4:form_key(d,8);break;case 5:form_key(d,0x53);break;case 6:form_key(d,0x47,(random&16)!=0);break;case 7:form_key(d,0x4f,(random&16)!=0);break;default:form_key(d,13);}valid(multi);}
 load(d,"<input readonly value='ąć'><input disabled value='x'><textarea style='width:30px;height:20px'></textarea>");
 auto &ro=d.controls[0];d.focused_control=ro.node;CHECK(form_key(d,'a',false,true));CHECK(form_clipboard(d,'c',clipboard));CHECK(copied==std::string("ąć\0",5));
 CHECK(form_clipboard(d,'x',clipboard));CHECK(std::string(ro.value)=="ąć");copied="changed";CHECK(form_clipboard(d,'v',clipboard));CHECK(std::string(ro.value)=="ąć");
 CHECK(form_key(d,8));CHECK(std::string(ro.value)=="ąć");CHECK(!form_insert(d,'x'));
 d.focused_control=d.controls[1].node;CHECK(!form_key(d,'a',false,true));
 auto &scroller=d.controls[2];d.focused_control=scroller.node;std::string long_text(2048,'a');CHECK(form_replace(d,scroller,long_text.data(),long_text.size()));control_keep_caret(d,scroller,{});CHECK(scroller.scroll_y>1000);
 CHECK(control_scroll(d,scroller,{},0,-MaxDimension));CHECK(!scroller.scroll_y);
 Surface scrolled{pixels.data(),400,240};drain(paint_work(d,scrolled,0,{}, {nullptr,bitmap,nullptr},{0,0,400,240}));CHECK(!scroller.scroll_y);
 CHECK(!control_scroll(d,scroller,{},0,-100));CHECK(control_scroll(d,scroller,{},0,30));CHECK(scroller.scroll_y==30);
 CHECK(control_vertical_key(d,0x49,true,{}));control_keep_caret(d,scroller,{});CHECK(scroller.scroll_y>30);valid(scroller);
 auto old=d.generation;load(d,"<input>");CHECK(d.generation!=old && !d.focused_control && !d.controls[0].anchor && !d.controls[0].cursor);
 CHECK(work_memory==0);std::printf("Husarz selection/clipboard: %u checks PASS\n",checks);
}
