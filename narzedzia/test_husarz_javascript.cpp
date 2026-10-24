#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "programy/husarz/layout.h"
#include "programy/husarz/js_dom.h"
using namespace Husarz;
using namespace Husarz::Js;
static unsigned checks{},peak_stack{},peak_frames{},peak_strings{};
#if defined(__SANITIZE_ADDRESS__)
extern "C" size_t __sanitizer_get_current_allocated_bytes();
static size_t heap_sample(){return __sanitizer_get_current_allocated_bytes();}
#else
static size_t heap_sample(){return 0;}
#endif
static size_t heap_baseline{},peak_heap{};
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#x);std::abort();}}while(0)
static unsigned drain(Runtime &r){unsigned n=0;for(;;){unsigned before=r.instructions;bool done=r.step();size_t live=heap_sample();if(live>heap_baseline&&live-heap_baseline>peak_heap)peak_heap=live-heap_baseline;if(r.maximum_stack>peak_stack)peak_stack=r.maximum_stack;if(r.maximum_frames>peak_frames)peak_frames=r.maximum_frames;if(r.bytes_used>peak_strings)peak_strings=r.bytes_used;CHECK(r.instructions-before<=SliceInstructions);CHECK(r.driver.units<=MaxSliceWork);if(done)break;CHECK(++n<100000);}CHECK(!r.driver.failed);CHECK(r.driver.maximum<=MaxSliceWork);return n;}
static void number(const char *s,int64_t expected){auto *r=new Runtime;CHECK(r->start(s,std::strlen(s)));drain(*r);if(r->error!=Error::None)std::fprintf(stderr,"script error %u at %u:%u: %s\n",unsigned(r->error),r->error_line,r->error_column,s);CHECK(r->error==Error::None);CHECK(r->result.kind==Kind::Number);CHECK(r->result.number==Numeric(expected));delete r;}
static void error(const char *s,Error expected){auto *r=new Runtime;CHECK(r->start(s,std::strlen(s)));drain(*r);if(r->error!=expected)std::fprintf(stderr,"wrong error %u expected %u: %s\n",unsigned(r->error),unsigned(expected),s);CHECK(r->error==expected);delete r;}
static void script(BrowserRuntime &r,const char *s){r.error=Error::None;r.budget_exceeded=false;r.budget=ScriptBudget(r.limits);r.driver={};r.driver.allow_clock=false;r.task=r.script_job(s,std::strlen(s));drain(r);if(r.error!=Error::None)std::fprintf(stderr,"DOM error %u at %u:%u: %s\n",unsigned(r.error),r.error_line,r.error_column,s);CHECK(r.error==Error::None);}
static void content(Document &d,const char *id,const char *expected){char buffer[4097]{};auto h=ScriptDom(d).get_element_by_id(id);CHECK(ScriptDom(d).text_content(h,buffer,sizeof(buffer)));CHECK(std::strcmp(buffer,expected)==0);}
static Work navigation_boundary(BrowserRuntime &r,Value key,Value &url){co_await r.point(2030);co_await r.property(Value::handle(Kind::Location),key,true,url);}
static void async_number(const char *s,int64_t expected){auto *r=new Runtime;CHECK(r->start(s,std::strlen(s)));drain(*r);if(r->error!=Error::None)std::fprintf(stderr,"async error %u %u:%u %s\n",unsigned(r->error),r->error_line,r->error_column,s);CHECK(r->error==Error::None);CHECK(r->start("n;",2));drain(*r);CHECK(r->error==Error::None);if(r->result.number!=Numeric(expected))std::fprintf(stderr,"async result %.17g expected %lld: %s\n",r->result.number.value,(long long)expected,s);CHECK(r->result.number==Numeric(expected));delete r;}
int main(){
    heap_baseline=heap_sample();
    {
        uint32_t committed[2]{};CHECK(storage_write_slot(committed)==0);committed[0]=2;CHECK(storage_write_slot(committed)==1);committed[1]=4;CHECK(storage_write_slot(committed)==0);committed[0]=6;CHECK(storage_write_slot(committed)==1);
        // A coalesced even revision must preserve the only valid older record.
        auto *s=new StorageNamespace;auto *loaded=new StorageNamespace;s->origin=Origin::parse("http://durability/");s->loaded=true;CHECK(s->set("key",3,"old",3));auto *a=new uint8_t[StorageDiskCapacity];auto *b=new uint8_t[StorageDiskCapacity];unsigned an=storage_encode(*s,a,StorageDiskCapacity);CHECK(s->revision==2);
        CHECK(s->set("key",3,"middle",6));CHECK(s->set("key",3,"new",3));CHECK(s->revision==4);committed[0]=2;committed[1]=0;CHECK(storage_write_slot(committed)==1);unsigned bn=storage_encode(*s,b,StorageDiskCapacity);b[bn-1]^=1;CHECK(!storage_decode(*loaded,b,bn,s->origin));CHECK(storage_decode(*loaded,a,an,s->origin));CHECK(std::strcmp(loaded->entries[0].value,"old")==0);delete[] a;delete[] b;delete s;delete loaded;
    }

    async_number("var n=0;new Promise(async function(resolve){resolve(5);}).then(x=>{n=x;});",5);
    async_number("var n=0;new Promise(async function(resolve){var x=await 9;resolve(x);}).then(x=>{n=x;});",9);

    number("var a={x:1};var b=Object.create(a);b.x=2;a.x+b.x;",3);
    number("var a={x:1};var b=Object.create(a);a.x=7;b.x;",7);
    error("var a={};for(var i=0;i<20;i++){a=Object.create(a);}",Error::Range);
    number("var n=0;try{throw {x:6};}catch(e){n=e.x;}finally{n+=1;}n;",7);
    number("var n=0;try{try{throw 1;}finally{throw 5;}}catch(e){n=e;}n;",5);
    {char json[80]{},source[128]{};unsigned n=0;for(unsigned i=0;i<20;++i)json[n++]='[';json[n++]='0';for(unsigned i=0;i<20;++i)json[n++]=']';std::snprintf(source,sizeof(source),"JSON.parse('%s');",json);error(source,Error::Range);}

    {
        auto *r=new Runtime;CHECK(r->start("var x=3;function f(x){return x;}",std::strlen("var x=3;function f(x){return x;}")));drain(*r);CHECK(r->error==Error::None);r->scope_count=ScopeLimit;CHECK(r->start("f(9);",5));drain(*r);CHECK(r->error==Error::Memory);bool found=false;for(unsigned i=0;i<r->binding_count;++i)if(r->bindings[i].scope==0&&r->is(r->bindings[i].name,"x")){found=true;CHECK(r->bindings[i].value.number==3);}CHECK(found);delete r;
    }
    {
        auto *r=new Runtime;CHECK(r->start("1;",2));drain(*r);CHECK(r->error==Error::None);Value before=r->objects[r->object_prototype.ref].props[0].value;r->object_count=ObjectLimit;CHECK(r->start("new Error('x');",std::strlen("new Error('x');")));drain(*r);CHECK(r->error==Error::Memory);CHECK(r->same(before,r->objects[r->object_prototype.ref].props[0].value));delete r;
    }
    {
        auto *d=new Document;auto *r=new BrowserRuntime;r->bind(*d,22,1,"http://oom/");auto *s=new StorageNamespace;s->origin=Origin::parse("http://oom/");s->loaded=true;CHECK(s->set("key",3,"retained",8));r->local_storage=s;
        script(*r,"1;");Value args[]={Value::handle(Kind::String,r->string("key",3)),Value::num(42)};r->string_count=StringCount;r->task=r->native(method(StorageSet,Value::handle(Kind::Storage)),args,2,r->result);drain(*r);CHECK(r->error==Error::Memory);CHECK(std::strcmp(s->entries[0].value,"retained")==0);delete r;delete d;delete s;
    }

    {
        auto *d=new Document;const char *html="<body><button id='b'>click</button></body>";parse_html(*d,html,std::strlen(html));auto *r=new BrowserRuntime;r->bind(*d,21,2,"http://receiver/");r->load_sent=true;
        script(*r,"var receiver=0;document.getElementById('b').addEventListener('click',function(e){if(this==e.currentTarget){receiver=1;}});");
        unsigned id=ScriptDom(*d).get_element_by_id("b").id;CHECK(r->queue(id,"click"));for(unsigned i=0;i<10;++i)if(r->tick(i*10000000))drain(*r);script(*r,"receiver;");CHECK(r->result.number==1);delete r;delete d;
    }

    number("Number.isNaN(NaN)&&NaN!=NaN&&!(NaN<1)&&!(NaN>=1)?1:0;",1);
    number("Number.MIN_VALUE>0&&Number.MIN_VALUE/2==0?1:0;",1);
    number("1e-8>0&&1e308>1e307&&1e309==Infinity?1:0;",1);
    number("-5.5%2==-1.5?1:0;",1);
    number("isNaN('bad')&&!isNaN('1.5')&&isFinite('2')&&!isFinite(Infinity)?1:0;",1);
    error("var a=[];a[NaN]=1;",Error::Range);
    error("var a=[];a[Infinity]=1;",Error::Range);
    error("await 1;",Error::Syntax);
    error("function f(){return await 1;}",Error::Syntax);
    async_number("var n=0;var p=new Promise(function(resolve){resolve(Promise.resolve(7));});p.then(x=>{n=x;});",7);
    async_number("var n=0;async function f(){var a=await 2;var b=await 3;var c=await 4;return a+b+c;}f().then(x=>{n=x;});",9);
    {
        auto *d=new Document;parse_html(*d,"<body></body>",13);auto *r=new BrowserRuntime;r->bind(*d,18,2,"http://fetch/");r->load_sent=true;
        script(*r,"var fetched=0;async function load(){var r=await fetch('/data');var j=await r.json();fetched=j.value;}load();");CHECK(r->fetch_count==1);CHECK(r->promises[0].state==PromiseState::Pending);
        ResourceEntry entry;char body[]={123,34,118,97,108,117,101,34,58,52,50,125};entry.raw=reinterpret_cast<uint8_t*>(body);entry.size=sizeof(body);entry.status=200;r->supply_fetch(0,&entry);
        for(unsigned i=0;i<20;++i)if(r->tick(i*1000000)){drain(*r);CHECK(r->error==Error::None);}
        script(*r,"fetched;");CHECK(r->result.number==42);CHECK(r->fetches[0].consumed);delete r;CHECK(entry.refs==0);delete d;
    }
    {
        auto *d=new Document;parse_html(*d,"<body></body>",13);auto *r=new BrowserRuntime;r->bind(*d,19,2,"http://fetch/");r->load_sent=true;
        script(*r,"var failed=0;fetch('/failure').catch(e=>{failed=1;});");r->supply_fetch(0,nullptr);
        for(unsigned i=0;i<10;++i)if(r->tick(i*1000000))drain(*r);script(*r,"failed;");CHECK(r->result.number==1);
        script(*r,"async function pending(){await fetch('/old');document.body.textContent='stale';}pending();");CHECK(r->continuations[0]!=nullptr);++d->generation;r->tick(100000000);CHECK(r->cancelled);for(auto *c:r->continuations)CHECK(c==nullptr);delete r;delete d;
    }
    {
        auto *d=new Document;parse_html(*d,"<body></body>",13);auto *r=new BrowserRuntime;r->bind(*d,20,2,"http://microtask/");r->load_sent=true;
        script(*r,"var timerRan=0;setTimeout(()=>{timerRan=1;},10);function loop(){Promise.resolve().then(loop);}loop();");
        CHECK(r->microtask_count>0);CHECK(r->microtasks_run<=MicrotasksPerTurn);
        for(unsigned i=1;i<60;++i)if(r->tick(i*10000000))drain(*r);
        CHECK(r->timer_fires==1);CHECK(r->promise_count<=PromiseLimit);CHECK(!r->cancelled);delete r;delete d;
    }

    async_number("var n=0;Promise.resolve(3).then(x=>x+1).then(x=>{n=x;});",4);
    async_number("var n=0;new Promise(function(resolve,reject){reject(4);resolve(9);}).catch(e=>{n=e;});",4);
    async_number("var n=0;new Promise(function(resolve){throw 6;}).catch(e=>{n=e;});",6);
    async_number("var n=0;Promise.resolve(1).then(x=>{throw 8;}).catch(e=>{n=e;});",8);
    async_number("var n=0;Promise.resolve(1).then(x=>Promise.resolve(9)).then(x=>{n=x;});",9);
    async_number("var n=0;var child=Promise.resolve(1).then(x=>child);child.catch(e=>{n=e.name=='TypeError'?1:0;});",1);
    async_number("var n=0;async function f(){return 5;}f().then(x=>{n=x;});",5);
    async_number("var n=0;async function f(){throw 6;}f().catch(e=>{n=e;});",6);
    async_number("var n=0;function g(){null.x;}async function f(){g();}f().catch(e=>{n=e.name=='TypeError'?1:0;});",1);
    async_number("var n=0;async function f(){try{await Promise.reject(7);}finally{n=2;}}f().catch(e=>{n+=e;});",9);
    number("JSON.parse('{\"x\":[1,2,3],\"s\":\"zażółć\"}').x[1];",2);
    number("JSON.stringify({x:1,s:'abc'})=='{\"x\":1,\"s\":\"abc\"}'?1:0;",1);
    number("var n=0;try{JSON.parse('{bad}');}catch(e){n=e.name=='SyntaxError'?1:0;}n;",1);
    number("var n=0;var a={};a.x=a;try{JSON.stringify(a);}catch(e){n=e.name=='TypeError'?1:0;}n;",1);
    error("var a=[1];var f=a.push;f(2);",Error::Type);
    error("new Number.isNaN(3);",Error::Type);

    number("var n=0;Promise.resolve(3).then(x=>{n=x;});n=1;",1);
    number("var p=new Promise(function(resolve,reject){resolve(4);resolve(9);});var n=0;p.then(x=>{n=x;});n;",0);
    {auto *r=new Runtime;const char *s="var n=0;new Promise(function(resolve){resolve(4);}).then(x=>x+2).then(x=>{n=x;});";CHECK(r->start(s,std::strlen(s)));drain(*r);CHECK(r->error==Error::None);CHECK(r->start("n;",2));drain(*r);CHECK(r->result.number==6);delete r;}
    {auto *r=new Runtime;const char *s="var n=0;Promise.reject(3).catch(e=>e+2).then(x=>{n=x;});";CHECK(r->start(s,std::strlen(s)));drain(*r);CHECK(r->error==Error::None);CHECK(r->start("n;",2));drain(*r);CHECK(r->result.number==5);delete r;}
    {auto *r=new Runtime;const char *s="var n=0;async function f(){var a=await Promise.resolve(3);var b=await 4;return a+b;}f().then(x=>{n=x;});";CHECK(r->start(s,std::strlen(s)));drain(*r);CHECK(r->error==Error::None);CHECK(r->start("n;",2));drain(*r);CHECK(r->result.number==7);delete r;}
    {auto *r=new Runtime;const char *s="var n=0;async function f(){try{await Promise.reject(7);}catch(e){return e+1;}}f().then(x=>{n=x;});";CHECK(r->start(s,std::strlen(s)));drain(*r);CHECK(r->error==Error::None);CHECK(r->start("n;",2));drain(*r);CHECK(r->result.number==8);delete r;}

    number("Number.isNaN(0/0)?1:0;",1);
    number("Number.isFinite(1/0)?0:1;",1);
    number("1/-0 < 0?1:0;",1);
    number("0.1+0.2 == 0.3?0:1;",1);
    number("1e3+1.25e-4*8000;",1001);
    number("0xff;",255);
    number("Number('1.5')*2;",3);
    number("String(0.1+0.2)=='0.30000000000000004'?1:0;",1);
    number("function f(){return this;} f()==undefined?1:0;",1);
    number("var o={n:7,f:function(){return this.n;}};o.f();",7);
    number("var o={f:function(){return this;}};var detached=o.f;detached()==undefined?1:0;",1);
    number("function Person(name){this.name=name;}Person.prototype.say=function(){return this.name;};var p=new Person('Jan');p.say()=='Jan'?1:0;",1);
    number("function F(){this.x=2;return 5;}var p=new F();p.x;",2);
    number("function F(){return {x:9};}var p=new F();p.x;",9);
    number("var a={x:4};var b=Object.create(a);var c=Object.create(b);c.x;",4);
    error("var a={};var b=Object.create(a);Object.setPrototypeOf(a,b);",Error::Type);
    number("var f=x=>x+1;f(5);",6);
    number("var f=(a,b)=>{return a*b;};f(3,4);",12);
    number("var f=()=>5;f();",5);
    number("var o={x:7,f:function(){return ()=>this.x;}};var g=o.f();g();",7);
    error("var f=()=>1;new f();",Error::Type);
    number("var n=0;try{throw 7;}catch(e){n=e;}n;",7);
    number("var n=0;try{null.x;}catch(e){n=e.name=='TypeError'?1:0;}n;",1);
    number("var n=0;function f(){try{return 2;}finally{n=3;}}f()+n;",5);
    number("var n=0;try{try{throw 2;}finally{n=3;}}catch(e){n+=e;}n;",5);
    number("function f(){try{return 2;}finally{return 9;}}f();",9);
    number("var n=0;for(var i=0;i<3;i++){try{if(i==1)continue;n+=1;}finally{n+=10;}}n;",32);
    number("var n=0;try{try{throw 1;}catch(e){throw e+1;}finally{n=5;}}catch(e){n+=e;}n;",7);

    {
        auto origin=Origin::parse("HTTPS://Example.com/path");CHECK(origin.valid);CHECK(origin.same(Origin::parse("https://example.com:443/")));CHECK(!origin.same(Origin::parse("http://example.com/")));CHECK(!Origin::parse("http://a@b/").valid);
        auto *s=new StorageNamespace;auto *decoded=new StorageNamespace;s->origin=origin;s->loaded=true;CHECK(s->set("../key",6,"zażółć",10));auto *bytes=new uint8_t[StorageDiskCapacity];unsigned n=storage_encode(*s,bytes,StorageDiskCapacity);CHECK(n>24);CHECK(!storage_conflicts(bytes,n,origin));CHECK(storage_conflicts(bytes,n,Origin::parse("https://other/")));CHECK(storage_decode(*decoded,bytes,n,origin));CHECK(decoded->find("../key",6)==0);CHECK(!storage_decode(*decoded,bytes,n,Origin::parse("https://other/")));bytes[n-1]^=1;CHECK(!storage_decode(*decoded,bytes,n,origin));bytes[n-1]^=1;
        for(unsigned i=0;i<n;++i)CHECK(!storage_decode(*decoded,bytes,i,origin));
        char path[96];storage_path(origin,0,path);CHECK(std::strstr(path,"../")==nullptr);CHECK(std::strstr(path,"example")==nullptr);
        Document *d=new Document;auto *r=new BrowserRuntime;r->bind(*d,17,1,"https://example.com/");r->local_storage=s;r->session_storage=decoded;
        script(*r,"localStorage.setItem('a','b');localStorage.getItem('a')=='b'?1:0;");CHECK(r->result.number==1);
        script(*r,"localStorage.removeItem('a');localStorage.getItem('a')==null?1:0;");CHECK(r->result.number==1);
        script(*r,"sessionStorage.setItem('tab','one');localStorage.getItem('tab')==null?1:0;");CHECK(r->result.number==1);
        delete r;delete d;delete s;delete decoded;delete[] bytes;
    }
    number("// comment\nvar a=2; /* bounded */ a*3+4;",10);
    number("var a=2; a+=3; a*=4; a-=1; a/=1; a;",19);
    number("var a=1; if(a<2 && a!=0){a=7;}else{a=9;} a;",7);
    number("false || true ? 4:5;",4);
    number("var n=0; while(n<7){n++;} n;",7);
    number("var n=0; for(var i=0;i<1000;i++){if(i==3)continue;if(i==10)break;n+=i;} n;",42);
    number("var n=0;for(var i=0;i<1000;i++){n+=1;}n;",1000);
    number("function add(x,y){return x+y;} add(4,5);",9);
    number("function fact(n){if(n<=1)return 1;return n*fact(n-1);} fact(6);",720);
    number("var x=2; {let x=7; x+=1;} x;",2);
    number("var x=2; {var x=7;} x;",7);
    number("function make(x){return function(y){return x+y;};} var f=make(4); f(8);",12);
    number("var a=[1,2]; a.push(3); a[0]=a.pop(); a[0]+a.length;",5);
    number("var a={x:2,y:3}; a.x=a.x+4; a['x']+a.y;",9);
    number("'zażółć'.length;",6);
    number("'zażółć'.indexOf('ż');",2);
    number("'ąĆęŁńÓśŹż'.toLowerCase().length;",9);
    number("'zażółć'.charAt(2)=='ż'?1:0;",1);
    number("'zażółć'.slice(2,4)=='żó'?1:0;",1);
    number("0.5+0.25==0.75?1:0;",1);
    number("1000000000000;",1000000000000LL);
    number("999999999999;",999999999999LL);
    error("while(true){}",Error::Budget);
    error("for(;;){}",Error::Budget);
    error("function f(){return f();} f();",Error::Range);
    error("const x=1; x=2;",Error::Type);
    error("absent;",Error::Reference);
    error("var x=[];x[999999999]=1;",Error::Range);
    error("var x=[];x[-1]=1;",Error::Range);
    error("var s='x';while(true){s=s+s;}",Error::Range);
    error("'unterminated",Error::Syntax);
    error("/* unterminated",Error::Syntax);
    error("'bad\\q'",Error::Syntax);
    error("'\xc0\xaf'",Error::Syntax);
    error("eval('1');",Error::Syntax);
    error("new Function('1');",Error::Type);
    number("var a=(x)=>x; a(9);",9);
    error("var x={a:{b:{c:1}}};x.a.b.c();",Error::Type);
    number("1/0 > 1e308 ? 1:0;",1);
    number("1000000000000*1000000000000 == 1e24 ? 1:0;",1);
    number("1.2345 > 1.234 ? 1:0;",1);
    number("var o={n:2};o.n+=3;var old=o.n++;old+o.n;",11);
    error("var a=[];for(var i=0;i<129;i++){a.push(i);}",Error::Range);
    error("var a={};for(var i=0;i<40;i++){a['p'+i]=i;}",Error::Range);
    error("var a=[];for(var i=0;i<100;i++){a=[];}",Error::Range);
    error("var n=0;while(n<3000){var s='x'+n;n++;}",Error::Memory);
    error("'\xe2\x82'",Error::Syntax);
    error("'\xed\xa0\x80'",Error::Syntax);
    error("'\\u0041'",Error::Syntax); // Unsupported escapes fail explicitly.
    {
        char source[80]{};for(unsigned i=0;i<65;++i)source[i]='a';source[65]=';';error(source,Error::Range);
    }
    {
        auto *source=new char[TokenLimit+2]{};for(unsigned i=0;i<TokenLimit;++i)source[i]=';';error(source,Error::Range);delete[] source;
    }
    {
        auto *source=new char[StringLimit+4]{};source[0]='\'';for(unsigned i=0;i<StringLimit+1;++i)source[i+1]='x';source[StringLimit+2]='\'';error(source,Error::Range);delete[] source;
    }
    {
        char source[2048]{};unsigned used=0;
        for(unsigned depth=0;depth<3;++depth){source[used++]='[';for(unsigned i=0;i<127;++i){source[used++]='1';source[used++]=',';}}
        source[used++]='0';for(unsigned depth=0;depth<3;++depth)source[used++]=']';source[used++]=';';error(source,Error::Range);
    }
    {char source[256]{};unsigned n=0;for(unsigned i=0;i<50;++i)source[n++]='(';source[n++]='1';for(unsigned i=0;i<50;++i)source[n++]=')';source[n++]=';';error(source,Error::Range);}
    {
        auto *r=new Runtime;char source[SourceLimit+2]{};CHECK(!r->start(source,sizeof(source)));delete r;
    }
    // Fuzz-like bounded compiler mutations: error or completion must be finite,
    // sanitizer-clean and reclaim all coroutine frames after cancellation.
    const char *seed="function f(n){var a={x:[1,2]};for(var i=0;i<n;i++){if(i==2)continue;a.x[0]+=i;}return a.x[0];} f(6);";
    for(unsigned trial=0;trial<256;++trial){
        auto *r=new Runtime;char source[256]{};std::strcpy(source,seed);unsigned n=unsigned(std::strlen(source));unsigned at=(trial*37)%n;
        if(trial&1)source[at]=char((trial*11)%128);else source[at]=0;
        CHECK(r->start(source,std::strlen(source)));drain(*r);delete r;CHECK(work_memory==0);
    }
    {
        auto *d=new Document;const char *html="<body><div id='outer'><p id='x'>old</p><a id='link' href='/fail'>link</a></div><input id='field'><input type='checkbox' id='check'></body>";
        parse_html(*d,html,std::strlen(html));finish_work(prepare_controls_work(*d));
        auto *r=new BrowserRuntime;r->bind(*d,1,2,"http://fixture/");r->load_sent=true;
        script(*r,"var x=document.getElementById('x');x.textContent='zażółć';x.className='a';x.classList.add('b');x.style.color='red';");content(*d,"x","zażółć");
        CHECK(std::strcmp(d->attr(ScriptDom(*d).get_element_by_id("x").id,"class"),"a b")==0);
        script(*r,"var c=document.createElement('span');c.id='created';c.textContent='nowy';document.getElementById('outer').appendChild(c);");content(*d,"created","nowy");
        script(*r,"var all=document.querySelectorAll('#outer span');if(all.length!=1){missing();} var text=document.createTextNode(' tekst');c.appendChild(text);");content(*d,"created","nowy tekst");
        script(*r,"var outer=document.getElementById('outer');outer.insertBefore(c,x);outer.removeChild(c);outer.appendChild(c);var z=document.createElement('b');z.id='z';z.textContent='Z';outer.replaceChild(z,c);");content(*d,"z","Z");
        auto old=ScriptDom(*d).get_element_by_id("z");
        script(*r,"outer.innerHTML='<p id=inner>bezpieczny</p><script>for(;;){}</script>';var inner=document.querySelector('#inner');inner.setAttribute('data-x','ok');inner.removeAttribute('data-x');");content(*d,"inner","bezpieczny");CHECK(!ScriptDom(*d).valid(old));CHECK(d->script_count==0);
        script(*r,"var field=document.getElementById('field');field.value='żółć';document.getElementById('check').checked=true;var count=0;function listener(e){count+=1;field.value=e.type;e.preventDefault();}field.addEventListener('input',listener);");
        unsigned field=ScriptDom(*d).get_element_by_id("field").id;CHECK(r->queue(field,"input",1));CHECK(r->tick(0));drain(*r);CHECK(r->prevented);CHECK(r->default_action==0);CHECK(std::strcmp(control(*d,uint16_t(field))->value,"input")==0);
        script(*r,"field.removeEventListener('input',listener);setTimeout(function(){field.value='timer';},1);");
        CHECK(!r->tick(9000000));CHECK(r->tick(10000000));drain(*r);CHECK(std::strcmp(control(*d,uint16_t(field))->value,"timer")==0);
        script(*r,"var timer=setTimeout(function(){field.value='bad';},10);clearTimeout(timer);");CHECK(!r->tick(30000000));
        script(*r,"var order='';var parent=document.getElementById('outer');parent.addEventListener('click',function(e){order+='c';},true);inner.addEventListener('click',function(e){order+='t';});parent.addEventListener('click',function(e){order+='b';});");
        CHECK(r->queue(ScriptDom(*d).get_element_by_id("inner").id,"click"));CHECK(r->tick(31000000));drain(*r);
        script(*r,"if(order!='ctb'){wrongOrder();}inner.onclick=function(e){e.stopPropagation();};order='';");
        CHECK(r->queue(ScriptDom(*d).get_element_by_id("inner").id,"click"));CHECK(r->tick(32000000));drain(*r);script(*r,"if(order!='ct'){wrongStop();}");
        script(*r,"var html=outer.innerHTML;if(html.indexOf('inner')<0){badSerialization();}var saved=null;inner.addEventListener('input',function(e){saved=e.preventDefault;});");
        CHECK(r->queue(ScriptDom(*d).get_element_by_id("inner").id,"input"));CHECK(r->tick(33000000));drain(*r);
        r->error=Error::None;r->budget=ScriptBudget{};r->driver={};r->task=r->script_job("saved();",8);drain(*r);CHECK(r->error==Error::Type);
        script(*r,"field.onkeydown=function(e){if(e.key=='z'){e.preventDefault();}};");
        field=ScriptDom(*d).get_element_by_id("field").id;CHECK(r->queue(field,"keydown",4,0,'z'));CHECK(r->queue(field,"@text",5,0,'z'));CHECK(r->tick(34000000));drain(*r);CHECK(r->default_action==0);CHECK(r->tick(35000000));drain(*r);CHECK(r->default_action==0);
        script(*r,"inner.onkeydown=function(e){e.preventDefault();while(true){}};");
        CHECK(r->queue(ScriptDom(*d).get_element_by_id("inner").id,"keydown",4,0,'x'));CHECK(r->tick(36000000));drain(*r);CHECK(r->error==Error::Budget);CHECK(!r->event_active);CHECK(r->default_action==0);
        script(*r,"for(var t=0;t<16;t++){setTimeout(function(){},100);} ");
        r->error=Error::None;r->budget=ScriptBudget{};r->driver={};const char *over="setTimeout(function(){},100);";r->task=r->script_job(over,std::strlen(over));drain(*r);CHECK(r->error==Error::Range);
        d->reset();CHECK(!r->tick(40000000));CHECK(r->cancelled);CHECK(r->job_count==0);for(auto &timer:r->timers)CHECK(!timer.active);
        delete r;delete d;
    }
    {
        auto *d=new Document;parse_html(*d,"<body></body>",13);auto *r=new BrowserRuntime;r->bind(*d,2,3,"http://timer/");r->load_sent=true;
        script(*r,"var fired=0;var interval=setInterval(function(){while(true){}},10);setTimeout(function(){fired=1;clearInterval(interval);},10);");
        CHECK(r->tick(10000000));drain(*r);CHECK(r->error==Error::Budget);CHECK(r->tick(20000000));drain(*r);script(*r,"if(fired!=1){unfairTimer();}");CHECK(r->timer_fires==2);CHECK(!r->timers[0].active);delete r;delete d;
    }
    {
        auto *d=new Document;parse_html(*d,"<body></body>",13);auto *r=new BrowserRuntime;r->bind(*d,3,4,"http://recovery/");r->load_sent=true;
        script(*r,"setInterval(function(){let local=7;missing();},10);");
        // A failing callback must not consume a new scope/binding on every tick.
        for(unsigned i=1;i<=300;++i){CHECK(r->tick(uint64_t(i)*10000000));drain(*r);CHECK(r->error==Error::Reference);CHECK(r->scope_count==1);CHECK(r->sp==0&&r->frame_count==0);}
        CHECK(r->timer_fires==300);CHECK(r->errors==300);delete r;delete d;
    }
    {
        auto *d=new Document;parse_html(*d,"<body></body>",13);auto *r=new BrowserRuntime;r->bind(*d,4,5,"http://fairness/");r->load_sent=true;
        script(*r,"setTimeout(function(){},10);");
        for(unsigned i=0;i<8;++i)CHECK(r->queue(1,"mousemove"));
        // A continuously nonempty event queue cannot starve a due timer.
        for(unsigned i=0;i<3&&!r->timer_fires;++i){CHECK(r->queue(1,"input"));CHECK(r->tick(10000000));drain(*r);}
        CHECK(r->timer_fires==1);CHECK(r->job_count>0);
        script(*r,"setTimeout(function(){},10);");
        r->job_count=0;CHECK(r->queue(1,"keydown",4,0,200));
        for(unsigned i=0;i<3&&r->timer_fires<2;++i){CHECK(r->tick(20000000));drain(*r);}
        CHECK(r->timer_fires==2);CHECK(r->job_count==1);delete r;delete d;
    }
    {
        auto *d=new Document;const char *html="<body><div id='parent'><button id='target'>run</button></div><p id='result'></p></body>";
        parse_html(*d,html,std::strlen(html));auto *r=new BrowserRuntime;r->bind(*d,5,6,"http://event-mutation/");r->load_sent=true;
        script(*r,"var p=document.getElementById('parent');var t=document.getElementById('target');var output=document.getElementById('result');var order='';p.addEventListener('click',function(e){order+='c';p.removeChild(t);},true);t.addEventListener('click',function(e){order+='t';});p.addEventListener('click',function(e){order+='b';output.textContent=order;});");
        CHECK(r->queue(ScriptDom(*d).get_element_by_id("target").id,"click"));CHECK(r->tick(10000000));drain(*r);CHECK(r->error==Error::None);content(*d,"result","ctb");
        // The dispatch path stays stable when an ancestor replaces descendants.
        script(*r,"p.appendChild(t);p.addEventListener('input',function(e){p.innerHTML='<i id=fresh>new</i>';},true);t.addEventListener('input',function(e){output.textContent='invalid';});p.addEventListener('input',function(e){output.textContent='safe';});");
        auto old=ScriptDom(*d).get_element_by_id("target");CHECK(r->queue(old.id,"input"));CHECK(r->tick(20000000));drain(*r);CHECK(r->error==Error::None);content(*d,"result","safe");CHECK(!ScriptDom(*d).valid(old));
        script(*r,"clearTimeout(-1);clearTimeout(999999999999);clearTimeout(1.5);");
        delete r;delete d;
    }
    {
        auto *d=new Document;const char *html="<body><input id='field'><input id='secret' type='password' value='secret'><input id='locked' readonly><textarea id='area'></textarea></body>";
        parse_html(*d,html,std::strlen(html));finish_work(prepare_controls_work(*d));bool changed=true;
        auto id=[&](const char *name){return ScriptDom(*d).get_element_by_id(name).id;};
        d->focused_control=id("field");CHECK(form_key(*d,8,false,false,&changed));CHECK(!changed);
        CHECK(form_insert(*d,'a',&changed));CHECK(changed);CHECK(form_key(*d,8,false,false,&changed));CHECK(changed);
        CHECK(form_clipboard(*d,'v',{},&changed));CHECK(!changed);
        auto *c=control(*d,id("field"));char full[MaxFormValue]{};for(auto &ch:full)ch='x';CHECK(form_replace(*d,*c,full,sizeof(full),&changed));CHECK(changed);
        CHECK(form_insert(*d,'a',&changed));CHECK(!changed);CHECK(c->length==MaxFormValue);
        d->focused_control=id("secret");CHECK(form_key(*d,'a',false,true));CHECK(form_clipboard(*d,'x',{},&changed));CHECK(!changed);CHECK(control(*d,id("secret"))->length==6);
        d->focused_control=id("locked");CHECK(form_key(*d,8,false,false,&changed));CHECK(!changed);CHECK(!form_insert(*d,'a',&changed));CHECK(!changed);
        d->focused_control=id("area");CHECK(form_key(*d,13,false,false,&changed));CHECK(changed);CHECK(control(*d,id("area"))->length==1);
        delete d;
    }
    {
        auto *d=new Document;parse_html(*d,"<body></body>",13);auto *r=new BrowserRuntime;r->bind(*d,6,7,"http://navigation/");
        char url[1900]{};std::strcpy(url,"http://fixture/");for(unsigned i=15;i<sizeof(url)-1;++i)url[i]='x';
        Value value=Value::handle(Kind::String,r->string(url,std::strlen(url)));Value key=Value::handle(Kind::String,r->string("href",4));
        r->task=navigation_boundary(*r,key,value);unsigned slices=0;
        while(!r->step()){CHECK(r->navigation_request[0]==0||std::strcmp(r->navigation_request,url)==0);CHECK(r->driver.units<=MaxSliceWork);CHECK(++slices<10);}
        CHECK(slices>0);CHECK(r->error==Error::None);CHECK(std::strcmp(r->navigation_request,url)==0);delete r;delete d;
    }
    std::printf("Husarz JavaScript: PASS (%u checks), runtime=%zu, browser=%zu, compiler=%zu bytes\n",checks,sizeof(Runtime),sizeof(BrowserRuntime),sizeof(Compiler));
    std::printf("Continuation=%zu StorageNamespace=%zu Document=%zu bytes\n",sizeof(Continuation),sizeof(StorageNamespace),sizeof(Document));
    std::printf("Observed operands=%u frames=%u string-bytes=%u work-frame-peak=%zu\n",peak_stack,peak_frames,peak_strings,peak_work_memory);
    std::printf("Sampled ASan live heap peak=%zu bytes above startup (includes DOM/compiler/transaction staging)\n",peak_heap);
}
