#pragma once
#include "js_core.h"
#include "forms.h"
#include "resource.h"
#include "origin.h"
#include "storage.h"
namespace Husarz { namespace Js {
constexpr unsigned ListenerLimit=64, TimerLimit=16, JobLimit=32, QueryLimit=64;
// Transaction staging is heap owned; published indices change only at commit.
struct TreeMutation {
    Node nodes[MaxNodes]{};Node ordered[MaxNodes]{};
    uint16_t order[MaxNodes]{},map[MaxNodes]{};bool seen[MaxNodes]{};
    unsigned count{},next{};
};
struct Listener {uint16_t node{},type{},serial{};Value callback{};bool capture{},active{},property{};};
struct Timer {uint32_t id{};uint64_t due{},delay{};Value callback{};bool repeat{},active{};};
struct EventJob {uint16_t node{},type{},action{},control{};uint32_t sequence{};unsigned key{};bool ctrl{},shift{},alt{};int x{},y{};uint32_t related_key{},native_key{};uint64_t queued_at{};bool printable{},await_text{};};
class BrowserRuntime : public Runtime {
  public:
    StorageNamespace *local_storage{},*session_storage{};
    static constexpr unsigned FetchLimit=4, FetchBodyLimit=4096;
    struct FetchRecord {char url[2048]{};Value promise{};ResourceEntry *entry{};bool active{},queued{},ready{},failed{},consumed{};};
    FetchRecord fetches[FetchLimit]{};unsigned fetch_count{};
    void supply_fetch(unsigned slot,ResourceEntry *entry){if(!alive()||slot>=fetch_count||!fetches[slot].active||fetches[slot].ready)return;auto &f=fetches[slot];f.failed=!entry||entry->size>FetchBodyLimit;f.entry=f.failed?nullptr:entry;if(f.entry)++f.entry->refs;f.ready=true;}
    Work complete_fetch(unsigned slot){auto &f=fetches[slot];f.active=false;Value result{};if(f.failed)result=error_object(Error::Type);else result=Value::handle(Kind::Response,slot,generation);co_await settle(f.promise,result,f.failed);}
    Listener listeners[ListenerLimit]{};Timer timers[TimerLimit]{};
    EventJob jobs[JobLimit]{};uint16_t event_types[32]{},changed_nodes[64]{};unsigned event_type_count{};
    unsigned job_count{},listener_serial{},timer_serial{},event_serial{};
    unsigned callback_ip{},callback_constant{},timer_cursor{};
    uint64_t now{};unsigned script_index{},scheduling_turn{};bool load_sent{},initialized{},publication_pending{},event_active{},prevented{},propagation{},immediate{};
    EventJob event{},default_event{};uint32_t queued_key{},last_native_key{},last_unicode{},prevented_keys[JobLimit]{};uint16_t current_target{};unsigned phase{};uint32_t active_event{};
    uint32_t event_dispatches{},timer_fires{},console_lines{},reported_errors{},reported_dispatches{},debug_notes{};
    uint16_t default_node{},default_action{},default_control{};
    char location[2048]{};char navigation_request[2048]{};
    struct ScriptSource {ResourceEntry *entry{};bool ready{},failed{};} sources[MaxScripts]{};
    void aborted()override {
        Runtime::aborted();
        if(event_active){event_active=false;if(event.action==4&&prevented)prevented_keys[event.sequence%JobLimit]=event.sequence;if(!prevented&&event.action){default_event=event;default_action=event.action;default_node=event.node;default_control=event.control;}}
    }
    ~BrowserRuntime()override {task.cancel();for(auto &f:fetches)if(f.entry)ResourceCache::unpin(f.entry);for(auto &s:sources)if(s.entry)ResourceCache::unpin(s.entry);}
    void bind(Document &d,uint32_t owner,uint32_t nav,const char *url){document=&d;generation=d.generation;tab=owner;navigation=nav;unsigned n=unsigned(length(url,2047));for(unsigned i=0;i<n;++i)location[i]=url[i];location[n]=0;}
    bool owns(uint32_t owner,uint32_t nav,uint32_t doc)const{return !cancelled&&tab==owner&&navigation==nav&&generation==doc;}
    unsigned resolve(unsigned identity)const {
        if(!alive()||!identity||identity>65535){return 0;}return ScriptDom(*document).by_identity(uint16_t(identity)).id;
    }
    Value node(unsigned id)const{return id&&id<document->node_count?Value::handle(Kind::Node,document->nodes[id].identity,generation):Value::handle(Kind::Null);}
    bool node_arg(Value value,unsigned &out){if(value.kind!=Kind::Node||value.aux!=generation||!(out=resolve(value.ref))){fail(Error::Type);return false;}return true;}
    Work initialize(){
        const char *names[]={"document","window","console","location","setTimeout","clearTimeout","setInterval","clearInterval","fetch","localStorage","sessionStorage"};
        Value values[]={Value::handle(Kind::Document),Value::handle(Kind::Window),Value::handle(Kind::Console),Value::handle(Kind::Location),method(Timeout),method(ClearTimeout),method(Interval),method(ClearTimeout),method(Fetch),Value::handle(Kind::Storage,0),Value::handle(Kind::Storage,1)};
        callback_constant=constant({});constant({});callback_ip=code_count;
        code[code_count++]={Op::Constant,0,uint16_t(callback_constant)};
        code[code_count++]={Op::Constant,0,uint16_t(callback_constant+1)};
        code[code_count++]={Op::Call,2,1};code[code_count++]={Op::Pop};code[code_count++]={Op::Halt};
        scope=0;for(unsigned i=0;i<11;++i){co_await point();unsigned name=string(names[i],length(names[i],64));co_await binding(name,true,true,true,values[i]);}initialized=true;
    }
    bool queue(unsigned id,const char *type,unsigned action=0,unsigned control=0,unsigned key=0,bool ctrl=false,bool shift=false,bool alt=false,int x=0,int y=0){
        if(cancelled||event_serial==UINT32_MAX||job_count==JobLimit||!document||id>=document->node_count)return false;
        unsigned name=0;for(unsigned i=0;i<event_type_count;++i)if(is(event_types[i],type)){name=event_types[i];break;}
        if(!name){if(event_type_count==32)return false;name=string(type,length(type,32));if(!name)return false;event_types[event_type_count++]=uint16_t(name);}
        if(equal(type,"mousemove")&&job_count&&jobs[job_count-1].type==name){jobs[job_count-1].x=x;jobs[job_count-1].y=y;return true;}
        unsigned slot=job_count++;jobs[slot]={document->nodes[id].identity,uint16_t(name),uint16_t(action),uint16_t(control?document->nodes[control].identity:0),++event_serial,key,ctrl,shift,alt,x,y};
        if(equal(type,"keydown")){queued_key=jobs[slot].sequence;jobs[slot].native_key=key;jobs[slot].queued_at=now;jobs[slot].await_text=key>127;if(key>127)jobs[slot].key=0;}
        if(equal(type,"keyup")&&key>127){jobs[slot].key=last_native_key==key?last_unicode:0;jobs[slot].printable=jobs[slot].key!=0;}
        if(action==5){jobs[slot].related_key=queued_key;for(unsigned i=0;i<slot;++i)if(jobs[i].sequence==queued_key){last_native_key=jobs[i].native_key;last_unicode=key;jobs[i].key=key;jobs[i].printable=true;jobs[i].await_text=false;break;}queued_key=0;}return true;
    }
    void supply(unsigned script,ResourceEntry *entry){
        if(script>=MaxScripts||sources[script].ready)return;
        sources[script].ready=true;sources[script].failed=!entry||entry->size>SourceLimit;
        if(!sources[script].failed){++entry->refs;sources[script].entry=entry;}
    }
    Work attributes(unsigned id,unsigned name,unsigned value,bool remove=false){
        auto &d=*document;auto &n=d.nodes[id];const char *key=text(name);
        if(size(name)==0||size(name)>64||size(value)>MaxAttrLength){fail(Error::Range);co_return;}
        for(unsigned i=0;i<size(name);++i){co_await point();char c=key[i];if(!ident(c)){fail(Error::Type);co_return;}}
        unsigned at=n.attr_count;
        for(unsigned i=0;i<n.attr_count;++i){co_await point();if(equal_ci(key,size(name),d.str(d.attrs[n.attr_start+i].name))){at=i;break;}}
        if(remove){if(at<n.attr_count){for(unsigned i=at+1;i<n.attr_count;++i){co_await point();d.attrs[n.attr_start+i-1]=d.attrs[n.attr_start+i];}--n.attr_count;}}
        else {
            uint32_t v=d.store(text(value),size(value),MaxAttrLength);if(!v){fail(Error::Memory);co_return;}
            if(at<n.attr_count)d.attrs[n.attr_start+at].value=v;
            else {
                if(n.attr_count==MaxAttrsPerNode||n.attr_count+1U>MaxAttrs-d.attr_count){fail(Error::Range);co_return;}
                unsigned start=d.attr_count;
                for(unsigned i=0;i<n.attr_count;++i){co_await point();d.attrs[d.attr_count++]=d.attrs[n.attr_start+i];}
                uint32_t k=d.store(key,size(name),64,true);if(!k){fail(Error::Memory);co_return;}
                d.attrs[d.attr_count++]={k,v};n.attr_start=uint16_t(start);++n.attr_count;
            }
        }
        // Inline declarations are refreshed through the existing CSS parser.
        if(is(name,"style")){
            co_await css_declarations_work(d,{text(value),remove?0:size(value)},n.inline_first,n.inline_count);
        }
        d.invalidate(uint16_t(id),8);++mutations;
    }
    Work stage(TreeMutation &m){
        m.count=document->node_count;m.next=document->next_node_identity;
        for(unsigned i=0;i<m.count;++i){co_await point();m.nodes[i]=document->nodes[i];}
    }
    void unlink(TreeMutation &m,unsigned id){auto &n=m.nodes[id];if(n.previous)m.nodes[n.previous].next=n.next;else if(m.nodes[n.parent].first==id)m.nodes[n.parent].first=n.next;if(n.next)m.nodes[n.next].previous=n.previous;else if(m.nodes[n.parent].last==id)m.nodes[n.parent].last=n.previous;n.parent=n.previous=n.next=0;n.detached=true;}
    bool link(TreeMutation &m,unsigned parent,unsigned child,unsigned before=0){
        if(!parent||parent>=m.count||!child||child>=m.count||before>=m.count||m.nodes[parent].is_text)return false;
        if(before && m.nodes[before].parent!=parent)return false;
        unsigned depth=0;for(unsigned p=parent;p;p=m.nodes[p].parent){if(p==child||++depth>MaxDepth)return false;}
        if(before==child){return true;}unlink(m,child);auto &n=m.nodes[child];auto &p=m.nodes[parent];
        n.parent=uint16_t(parent);n.next=uint16_t(before);n.previous=before?m.nodes[before].previous:p.last;
        if(n.previous)m.nodes[n.previous].next=uint16_t(child);else p.first=uint16_t(child);
        if(before)m.nodes[before].previous=uint16_t(child);else p.last=uint16_t(child);return true;
    }
    unsigned create(TreeMutation &m,bool is_text,const char *text,unsigned size){
        if(m.count==MaxNodes||m.next+1>=MaxNodes||size> (is_text?StringLimit:64)){fail(Error::Range);return 0;}
        unsigned id=m.count++;auto &n=m.nodes[id];n={};n.is_text=is_text;n.detached=true;n.depth=1;n.identity=uint16_t(++m.next);
        uint32_t stored=document->store(text,size,is_text?StringLimit:64,!is_text);if(!stored){fail(Error::Memory);return 0;}
        if(is_text){n.text=stored;n.text_length=size;}else n.tag=stored;return id;
    }
    Work commit(TreeMutation &m){
        unsigned count=1;m.order[0]=0;m.seen[0]=true;
        // Explicit DFS; only bounded indices cross yields. Detached trees follow.
        for(unsigned pass=0;pass<m.count;++pass){co_await point();unsigned root=pass==0?m.nodes[0].first:pass;
            if(!root||m.seen[root])continue;
            if(pass && m.nodes[root].parent && !m.seen[m.nodes[root].parent])continue;
            bool detached=pass!=0;unsigned current=root;unsigned visits=0;
            while(current){co_await point();if(current>=m.count||m.seen[current]||++visits>MaxNodes){fail(Error::Range);co_return;}
                auto &n=m.nodes[current];m.seen[current]=true;m.order[count++]=uint16_t(current);n.detached=detached;
                n.depth=uint16_t(n.parent?m.nodes[n.parent].depth+1:1);if(n.depth>MaxDepth){fail(Error::Range);co_return;}
                if(n.first){current=n.first;continue;}
                while(current && !m.nodes[current].next){co_await point();current=m.nodes[current].parent;if(current==0)break;}
                if(current)current=m.nodes[current].next;
            }
        }
        if(count!=m.count){fail(Error::Range);co_return;}
        for(unsigned i=0;i<count;++i){co_await point();m.map[m.order[i]]=uint16_t(i);}
        for(unsigned i=0;i<count;++i){co_await point();auto n=m.nodes[m.order[i]];n.parent=m.map[n.parent];n.first=m.map[n.first];n.last=m.map[n.last];n.next=m.map[n.next];n.previous=m.map[n.previous];m.ordered[i]=n;}
        // One bounded publication: at most 2,048 nodes, no yield or GUI callback.
        co_await point(MaxSliceWork);if(budget_exceeded){fail(Error::Budget);co_return;}auto &d=*document;
        for(auto &index:d.identity_index)index=0;
        for(unsigned i=0;i<count;++i){d.nodes[i]=m.ordered[i];d.node_dirty[i]=0;if(d.nodes[i].identity)d.identity_index[d.nodes[i].identity]=uint16_t(i);}
        d.node_count=count;d.next_node_identity=uint16_t(m.next);
        unsigned kept=0;for(unsigned i=0;i<d.control_count;++i){auto c=d.controls[i];c.node=m.map[c.node];if(d.nodes[c.node].identity)d.controls[kept++]=c;}d.control_count=kept;
        d.focused_control=m.map[d.focused_control];d.open_select=m.map[d.open_select];
        if(d.nodes[d.focused_control].detached||!d.nodes[d.focused_control].identity)d.focused_control=0;
        if(d.nodes[d.open_select].detached||!d.nodes[d.open_select].identity)d.open_select=0;
        for(unsigned i=0;i<d.image_count;++i)d.images[i].node=m.map[d.images[i].node];
        for(unsigned i=0;i<d.script_count;++i)d.scripts[i].node=m.map[d.scripts[i].node];
        for(auto &listener:listeners)if(listener.active&&listener.node&&!resolve(listener.node))listener.active=false;
        d.table_count=d.table_cells=0;d.table_plan_revision=0;d.invalidate(0,8);++mutations;
        for(uint16_t id=1;id<d.node_count;++id){co_await point();if(!is_control(d,id)||!d.nodes[id].identity||control(d,id))continue;
            if(d.control_count==MaxFormControls||ArenaSize-d.arena_used<MaxFormValue+1){fail(Error::Range);co_return;}
            auto &c=d.controls[d.control_count++];c={};c.node=id;c.display=d.arena_used;d.arena_used+=MaxFormValue+1;c.checked=d.has_attr(id,"checked");
            const char *value=d.attr(id,"value");if(d.tag(id,"textarea")||d.tag(id,"button")){unsigned text=d.nodes[id].first;if(text&&d.nodes[text].is_text)value=d.str(d.nodes[text].text);}
            unsigned size=unsigned(length(value,MaxFormValue));while(size&&(uint8_t(value[size])&0xc0)==0x80)--size;co_await point((size+63)/64+1);for(unsigned i=0;i<size;++i)c.value[i]=value[i];c.value[size]=0;c.length=c.cursor=c.anchor=uint16_t(size);control_display(d,c);
        }
    }
    Work inner_html(unsigned id,unsigned value);
    Work serialize(unsigned id,Value &value);
    Work event_property(unsigned node,unsigned name,bool write,Value &value);
    Work replace_text(unsigned id,unsigned value){
        struct Owner {TreeMutation *p{new TreeMutation};~Owner(){delete p;}} owner;
        if(!owner.p){fail(Error::Memory);co_return;}auto &m=*owner.p;co_await stage(m);
        if(m.nodes[id].is_text||m.nodes[id].raw){unsigned off=document->store(text(value),size(value),StringLimit);if(!off){fail(Error::Memory);co_return;}m.nodes[id].text=off;m.nodes[id].text_length=size(value);}
        else {
            // Replaced descendant handles become invalid; slots are never reused.
            for(unsigned i=1;i<m.count;++i){co_await point();unsigned p=i;unsigned steps=0;while(p&&p!=id&&++steps<=MaxDepth){co_await point();p=m.nodes[p].parent;}if(p==id&&i!=id)m.nodes[i].identity=0;}
            unsigned child=m.nodes[id].first;while(child){co_await point();unsigned next=m.nodes[child].next;unlink(m,child);child=next;}
            if(size(value)){unsigned node=create(m,true,text(value),size(value));if(!node||!link(m,id,node)){fail(Error::Range);co_return;}}
        }
        co_await commit(m);
    }
    Work property(Value base,Value key,bool write,Value &value)override;
    Work native(Value fn,Value *args,unsigned count,Value &out)override;
    Work dispatch(EventJob job);
    Work callback(Value fn,Value arg,bool has_arg);
    Work script_job(const char *source,size_t size){if(!initialized)co_await initialize();co_await compile_and_run(source,size);}
    bool tick(uint64_t time);
};
inline Work BrowserRuntime::property(Value base,Value key,bool write,Value &value){
    if(base.kind==Kind::Storage){
        auto *s=base.ref?session_storage:local_storage;if(write||key.kind!=Kind::String||!s||!s->loaded){fail(Error::Type);co_return;}
        if(is(key.ref,"length"))value=Value::num(s->count);else {unsigned id=is(key.ref,"key")?StorageKeyAt:is(key.ref,"getItem")?StorageGet:is(key.ref,"setItem")?StorageSet:is(key.ref,"removeItem")?StorageRemove:is(key.ref,"clear")?StorageClear:NativeNone;value=id?method(id,base):Value{};}co_return;
    }
    if(base.kind==Kind::Response){
        if(write||base.aux!=generation||base.ref>=fetch_count||!fetches[base.ref].entry||key.kind!=Kind::String){fail(Error::Type);co_return;}auto &f=fetches[base.ref];
        if(is(key.ref,"ok"))value=Value::boolean(f.entry->status>=200&&f.entry->status<300);else if(is(key.ref,"status"))value=Value::num(f.entry->status);else if(is(key.ref,"url"))value=Value::handle(Kind::String,string(f.url,length(f.url,2048)));else if(is(key.ref,"text")||is(key.ref,"json"))value=method(is(key.ref,"text")?ResponseText:ResponseJson,base);else value={};co_return;
    }
    co_await point();if(!alive()){fail(Error::Cancelled);co_return;}
    if(key.kind!=Kind::String){co_await Runtime::property(base,key,write,value);co_return;}
    unsigned name=key.ref;if(size(name)>64){fail(Error::Range);co_return;}
    if(base.kind==Kind::Window){
        if(size(name)>2&&text(name)[0]=='o'&&text(name)[1]=='n'){co_await event_property(0,name,write,value);co_return;}
        if(is(name,"addEventListener")||is(name,"removeEventListener")){if(write)fail(Error::Type);else value=method(is(name,"addEventListener")?AddListener:RemoveListener,Value::handle(Kind::Document));co_return;}
        if(is(name,"location")){if(write)fail(Error::Type);else value=Value::handle(Kind::Location);co_return;}
        const char *globals[]={"document","window","console","setTimeout","clearTimeout","setInterval","clearInterval","fetch","localStorage","sessionStorage"};
        for(auto *global:globals){co_await point();if(is(name,global)){if(write)fail(Error::Type);else co_await binding(name,false,false,false,value);co_return;}}
    }
    if(base.kind==Kind::Location){
        if(is(name,"href")){if(write){if(value.kind!=Kind::String || size(value.ref)>=sizeof(navigation_request)){fail(Error::Type);co_return;}
                // The browser consumes this mailbox after every slice. Publish
                // the complete bounded URL together, never a yielded prefix.
                co_await point((size(value.ref)+63)/64+1);if(budget_exceeded){fail(Error::Budget);co_return;}
                for(unsigned i=0;i<=size(value.ref);++i)navigation_request[i]=text(value.ref)[i];}
            else{value=Value::handle(Kind::String,string(location,length(location,sizeof(location))));}co_return;}
    }
    if(base.kind==Kind::Console){if(write){fail(Error::Type);co_return;}value=is(name,"log")?method(Log):is(name,"warn")?method(Warn):is(name,"error")?method(ConsoleError):Value{};co_return;}
    if(base.kind==Kind::Document){
        if(write){fail(Error::Type);co_return;}
        if(is(name,"body")||is(name,"documentElement")){
            value=Value::handle(Kind::Null);for(unsigned i=1;i<document->node_count;++i){co_await point();if(!document->nodes[i].detached&&document->tag(uint16_t(i),is(name,"body")?"body":"html")){value=node(i);break;}}co_return;
        }
        unsigned id=is(name,"getElementById")?GetId:is(name,"querySelector")?Query:is(name,"querySelectorAll")?QueryAll:is(name,"createElement")?CreateElement:is(name,"createTextNode")?CreateText:is(name,"addEventListener")?AddListener:is(name,"removeEventListener")?RemoveListener:NativeNone;
        value=id?method(id,base):Value{};co_return;
    }
    if(base.kind==Kind::Event){
        if(write || !event_active || base.aux!=active_event){fail(Error::Type);co_return;}
        if(is(name,"type"))value=Value::handle(Kind::String,event.type);
        else if(is(name,"target"))value=node(resolve(event.node));
        else if(is(name,"currentTarget"))value=current_target?node(resolve(current_target)):Value::handle(Kind::Document);
        else if(is(name,"defaultPrevented"))value=Value::boolean(prevented);
        else if(is(name,"eventPhase"))value=Value::num(phase);
        else if(is(name,"preventDefault"))value=method(Prevent,base);
        else if(is(name,"stopPropagation"))value=method(StopPropagation,base);
        else if(is(name,"ctrlKey"))value=Value::boolean(event.ctrl);
        else if(is(name,"shiftKey"))value=Value::boolean(event.shift);
        else if(is(name,"altKey"))value=Value::boolean(event.alt);
        else if(is(name,"key")||is(name,"code")){
            if(event.printable){char encoded[4];unsigned count=encode(event.key,encoded);value=Value::handle(Kind::String,string(encoded,count));co_return;}
            const char *keyname=event.key==0x48?"ArrowUp":event.key==0x50?"ArrowDown":event.key==0x4b?"ArrowLeft":event.key==0x4d?"ArrowRight":event.key==13?"Enter":event.key==9?"Tab":event.key==8?"Backspace":event.key==27?"Escape":"Unidentified";
            if(event.key>=32&&event.key<127&&event.key!=0x48&&event.key!=0x50&&event.key!=0x4b&&event.key!=0x4d){char c=char(event.key);value=Value::handle(Kind::String,string(&c,1));}else value=Value::handle(Kind::String,string(keyname,length(keyname,32)));
        }else value={};co_return;
    }
    if(base.kind==Kind::Node || base.kind==Kind::Style || base.kind==Kind::ClassList){
        unsigned id=resolve(base.ref);if(base.aux!=generation||!id){fail(Error::Type);co_return;}auto &d=*document;
        if(base.kind==Kind::Style){
            char cssname[65]{};unsigned used=0;for(unsigned i=0;i<size(name);++i){co_await point();char c=text(name)[i];if(c>='A'&&c<='Z'){if(used==63){fail(Error::Range);co_return;}cssname[used++]='-';c=lower(c);}if(used==64){fail(Error::Range);co_return;}cssname[used++]=c;}
            if(write){
                unsigned val=0;co_await convert_string(value,val);if(error!=Error::None)co_return;const char *prior=d.attr(uint16_t(id),"style");unsigned old=unsigned(length(prior,MaxAttrLength));
                if(old+used+size(val)+3>MaxAttrLength){fail(Error::Range);co_return;}
                struct Buffer{char *p{new char[MaxAttrLength+1]};~Buffer(){delete[] p;}} buffer;if(!buffer.p){fail(Error::Memory);co_return;}
                unsigned n=0;for(unsigned i=0;i<old;++i){co_await point();buffer.p[n++]=prior[i];}buffer.p[n++]=';';for(unsigned i=0;i<used;++i)buffer.p[n++]=cssname[i];buffer.p[n++]=':';for(unsigned i=0;i<size(val);++i){co_await point();buffer.p[n++]=text(val)[i];}
                unsigned v=string(buffer.p,n),key=string("style",5);co_await attributes(id,key,v);
            }else {
                value=Value::handle(Kind::String,string("",0));const auto &n=d.nodes[id];for(unsigned i=0;i<n.inline_count;++i){co_await point();const auto &decl=d.declarations[n.inline_first+i];if(equal(d.str(decl.name),cssname))value=Value::handle(Kind::String,string(d.str(decl.value),length(d.str(decl.value),MaxAttrLength)));}
            }co_return;
        }
        if(base.kind==Kind::ClassList){if(write){fail(Error::Type);co_return;}unsigned methodid=is(name,"add")?ClassAdd:is(name,"remove")?ClassRemove:is(name,"contains")?ClassContains:is(name,"toggle")?ClassToggle:NativeNone;value=methodid?method(methodid,base):Value{};co_return;}
        if(is(name,"style")||is(name,"classList")){if(write)fail(Error::Type);else value=Value::handle(is(name,"style")?Kind::Style:Kind::ClassList,base.ref,generation);co_return;}
        if(is(name,"innerHTML")){if(write){if(value.kind!=Kind::String){fail(Error::Type);co_return;}co_await inner_html(id,value.ref);}else co_await serialize(id,value);co_return;}
        if(is(name,"textContent")){
            if(write){unsigned textid=0;co_await convert_string(value,textid);if(error!=Error::None)co_return;co_await replace_text(id,textid);}
            else {
                struct Buffer{char *p{new char[StringLimit+1]};~Buffer(){delete[] p;}} buffer;if(!buffer.p){fail(Error::Memory);co_return;}unsigned n=0;
                unsigned end=id+1;while(end<d.node_count&&d.nodes[end].depth>d.nodes[id].depth){co_await point();++end;}
                for(unsigned i=id;i<end;++i){co_await point();auto &node=d.nodes[i];if(!node.is_text&&!node.raw)continue;
                    if(node.text_length>StringLimit-n){fail(Error::Range);co_return;}for(unsigned j=0;j<node.text_length;++j){co_await point();buffer.p[n++]=d.str(node.text)[j];}}
                value=Value::handle(Kind::String,string(buffer.p,n));
            }co_return;
        }
        if(is(name,"id")||is(name,"className")){
            const char *attribute=is(name,"id")?"id":"class";if(write){if(value.kind!=Kind::String){fail(Error::Type);co_return;}co_await attributes(id,string(attribute,length(attribute,8)),value.ref);}else {const char *s=d.attr(uint16_t(id),attribute);value=Value::handle(Kind::String,string(s,length(s,MaxAttrLength)));}co_return;
        }
        if(is(name,"value")||is(name,"checked")){
            auto *c=control(d,uint16_t(id));if(!c){fail(Error::Type);co_return;}
            if(is(name,"checked")){if(write){if(value.kind!=Kind::Boolean){fail(Error::Type);co_return;}c->checked=value.number!=0;control_display(d,*c);d.invalidate(uint16_t(id),1);++mutations;}else value=Value::boolean(c->checked);}
            else if(write){if(value.kind!=Kind::String||size(value.ref)>2048){fail(Error::Type);co_return;}
                if(d.tag(uint16_t(id),"select")){bool found=false;for(unsigned nodeid=d.nodes[id].first,option=0;nodeid;nodeid=d.nodes[nodeid].next){co_await point();if(!d.tag(uint16_t(nodeid),"option"))continue;const char *v=d.attr(uint16_t(nodeid),"value");if(equal(v,text(value.ref))){choose_option(d,*c,option);found=true;break;}++option;}if(!found)fail(Error::Range);}
                else {co_await point((size(value.ref)+63)/64+1);for(unsigned i=0;i<size(value.ref);++i)c->value[i]=text(value.ref)[i];c->length=uint16_t(size(value.ref));c->value[c->length]=0;c->cursor=c->anchor=c->length;c->reveal_caret=true;control_display(d,*c);d.invalidate(uint16_t(id),1);++mutations;}
            }else {
                const char *source=c->value;size_t n=c->length;
                if(d.tag(uint16_t(id),"select")){unsigned option=0;for(unsigned child=d.nodes[id].first;child;child=d.nodes[child].next){co_await point();if(!d.tag(uint16_t(child),"option"))continue;if(option++==c->option){if(d.has_attr(uint16_t(child),"value")){source=d.attr(uint16_t(child),"value");n=length(source,MaxAttrLength);}break;}}}
                value=Value::handle(Kind::String,string(source,n));
            }co_return;
        }
        if(size(name)>2 && text(name)[0]=='o'&&text(name)[1]=='n'){co_await event_property(base.ref,name,write,value);co_return;}
        unsigned methodid=is(name,"getAttribute")?GetAttribute:is(name,"setAttribute")?SetAttribute:is(name,"removeAttribute")?RemoveAttribute:is(name,"appendChild")?Append:is(name,"removeChild")?Remove:is(name,"insertBefore")?Insert:is(name,"replaceChild")?Replace:is(name,"addEventListener")?AddListener:is(name,"removeEventListener")?RemoveListener:NativeNone;
        if(methodid){if(write)fail(Error::Type);else value=method(methodid,base);co_return;}
    }
    co_await Runtime::property(base,key,write,value);
}
inline Work BrowserRuntime::inner_html(unsigned id,unsigned value){
    struct Owner{TreeMutation *tree{new TreeMutation};Document *fragment{new Document};uint16_t *map{new uint16_t[MaxNodes]{}};~Owner(){delete tree;delete fragment;delete[] map;}} owner;
    if(!owner.tree||!owner.fragment||!owner.map){fail(Error::Memory);co_return;}
    auto &m=*owner.tree;auto &fragment=*owner.fragment;
    if(!budget.consume(size(value))){fail(Error::Budget);co_return;}
    co_await parse_html_work(fragment,text(value),size(value),true);if(fragment.truncated){fail(Error::Range);co_return;}
    co_await stage(m);
    for(unsigned i=1;i<m.count;++i){co_await point();unsigned p=i,steps=0;while(p&&p!=id&&++steps<=MaxDepth){co_await point();p=m.nodes[p].parent;}if(p==id&&i!=id)m.nodes[i].identity=0;}
    unsigned child=m.nodes[id].first;while(child){co_await point();unsigned next=m.nodes[child].next;unlink(m,child);child=next;}
    unsigned body=0;for(unsigned i=1;i<fragment.node_count;++i){co_await point();if(fragment.tag(uint16_t(i),"body")){body=i;break;}}
    if(!body){fail(Error::Syntax);co_return;}owner.map[body]=uint16_t(id);
    for(unsigned i=body+1;i<fragment.node_count&&fragment.nodes[i].depth>fragment.nodes[body].depth;++i){co_await point();auto n=fragment.nodes[i];
        unsigned parent=owner.map[n.parent];if(!parent)continue;
        const char *source=n.is_text?fragment.str(n.text):fragment.str(n.tag);unsigned len=n.is_text?n.text_length:unsigned(length(source,64));
        unsigned added=create(m,n.is_text,source,len);if(!added){co_return;}owner.map[i]=uint16_t(added);
        auto &target=m.nodes[added];target.raw=n.raw;
        if(n.raw){target.text=document->store(fragment.str(n.text),n.text_length,StringLimit);target.text_length=n.text_length;}
        target.attr_start=uint16_t(document->attr_count);
        if(n.attr_count>MaxAttrs-document->attr_count){fail(Error::Range);co_return;}
        for(unsigned a=0;a<n.attr_count;++a){co_await point();auto attr=fragment.attrs[n.attr_start+a];const char *name=fragment.str(attr.name),*val=fragment.str(attr.value);
            uint32_t k=document->store(name,length(name,64),64),v=document->store(val,length(val,MaxAttrLength),MaxAttrLength);
            if(!k||!v){fail(Error::Memory);co_return;}document->attrs[document->attr_count++]={k,v};++target.attr_count;
            if(equal(name,"style"))co_await css_declarations_work(*document,{val,length(val,MaxAttrLength)},target.inline_first,target.inline_count);
        }
        if(!link(m,parent,added)){fail(Error::Range);co_return;}
    }
    co_await commit(m);
}
inline Work BrowserRuntime::native(Value fn,Value *args,unsigned count,Value &out){
    co_await point();if(!alive()){fail(Error::Cancelled);co_return;}
    Value receiver=Value::handle(Kind(fn.aux>>16),fn.aux&65535,generation);
    if(fn.ref<20||(fn.ref>=100&&fn.ref<300)){co_await Runtime::native(fn,args,count,out);co_return;}
    if(fn.ref>=StorageKeyAt&&fn.ref<=StorageClear){
        auto *s=receiver.ref?session_storage:local_storage;if(receiver.kind!=Kind::Storage||!s||!s->loaded||s->failed){fail(Error::Type);co_return;}
        if(fn.ref==StorageKeyAt){if(count!=1||args[0].kind!=Kind::Number){fail(Error::Type);co_return;}Numeric index=args[0].number;out=index.finite()&&index>=0&&index<s->count&&index==Numeric(int64_t(index))?Value::handle(Kind::String,string(s->entries[unsigned(index)].key,s->entries[unsigned(index)].key_size)):Value::handle(Kind::Null);co_return;}
        if(fn.ref==StorageClear){if(s->revision==UINT32_MAX){fail(Error::Range);co_return;}if(count){fail(Error::Type);co_return;}while(s->count){co_await point();s->remove(s->count-1);}co_return;}
        if(count!=(fn.ref==StorageSet?2U:1U)){fail(Error::Type);co_return;}unsigned key=0,value=0;co_await convert_string(args[0],key);if(fn.ref==StorageSet)co_await convert_string(args[1],value);if(error!=Error::None)co_return;if(size(key)>StorageKey||size(value)>StorageValue){fail(Error::Range);co_return;}
        co_await point(size(key)+size(value)+1);int index=s->find(text(key),size(key));
        if(fn.ref==StorageGet)out=index<0?Value::handle(Kind::Null):Value::handle(Kind::String,string(s->entries[index].value,s->entries[index].value_size));
        else if(fn.ref==StorageRemove){if(index>=0)s->remove(unsigned(index));}
        else if(!s->set(text(key),size(key),text(value),size(value))){fail(Error::Range);}
        co_return;
    }
    if(fn.ref==Fetch){
        if(count<1||count>2||args[0].kind!=Kind::String||size(args[0].ref)>2047||!Origin::parse(location).valid){fail(Error::Type);co_return;}
        out=promise();if(error!=Error::None)co_return;
        if(fetch_count==FetchLimit){Value e=error_object(Error::Range);co_await settle(out,e,true);co_return;}
        if(count==2){if(args[1].kind!=Kind::Object||args[1].ref>=object_count){Value e=error_object(Error::Type);co_await settle(out,e,true);co_return;}for(unsigned i=0;i<objects[args[1].ref].count;++i){co_await point();if(!is(objects[args[1].ref].props[i].name,"method")){Value e=error_object(Error::Type);co_await settle(out,e,true);co_return;}}Value key=Value::handle(Kind::String,string("method",6)),method_value;co_await property(args[1],key,false,method_value);if(method_value.kind!=Kind::Undefined&&(method_value.kind!=Kind::String||!is(method_value.ref,"GET"))){Value e=error_object(Error::Type);co_await settle(out,e,true);co_return;}}
        auto &f=fetches[fetch_count];for(unsigned i=0;i<size(args[0].ref);++i){co_await point();f.url[i]=text(args[0].ref)[i];}f.url[size(args[0].ref)]=0;f.promise=out;f.active=true;++fetch_count;co_return;
    }
    if(fn.ref==ResponseText||fn.ref==ResponseJson){
        if(count||receiver.kind!=Kind::Response||receiver.ref>=fetch_count){fail(Error::Type);co_return;}auto &f=fetches[receiver.ref];out=promise();if(error!=Error::None)co_return;
        if(f.failed||!f.entry||f.consumed||f.entry->size>FetchBodyLimit){Value e=error_object(Error::Type);co_await settle(out,e,true);co_return;}f.consumed=true;
        co_await point(unsigned(f.entry->size/16+1));Value body=Value::handle(Kind::String,string(reinterpret_cast<const char*>(f.entry->raw),f.entry->size));
        if(fn.ref==ResponseJson){Value parsed;co_await json(body,false,parsed);if(error!=Error::None){Error e=error;error=Error::None;Value ex=error_object(e);co_await settle(out,ex,true);}else co_await settle(out,parsed);}else co_await settle(out,body);co_return;
    }
    if(fn.ref==Log||fn.ref==Warn||fn.ref==ConsoleError){
        // No argument serialization: secrets (including page-read passwords)
        // never flow to BWS diagnostics. Retain bounded call counters only.
        if(console_lines<64){++console_lines;}co_return;
    }
    if(fn.ref==Prevent||fn.ref==StopPropagation){
        if(count||receiver.kind!=Kind::Event||!event_active||fn.number!=Numeric(active_event)){fail(Error::Type);co_return;}
        if(fn.ref==Prevent)prevented=true;else propagation=true;co_return;
    }
    if(fn.ref==Timeout||fn.ref==Interval){
        if(count<1||count>2||args[0].kind!=Kind::Function||(count==2&&args[1].kind!=Kind::Number)){fail(Error::Type);co_return;}
        Numeric delay=count==2?args[1].number:10;
        if(delay!=delay||delay>3600000){delay=3600000;}if(delay<10)delay=10;
        unsigned active=0;for(auto &timer:timers)if(timer.active)++active;if(active>=limits.timers||active>=TimerLimit){fail(Error::Range);co_return;}
        for(auto &timer:timers){co_await point();if(timer.active)continue;
            if(timer_serial==UINT32_MAX){fail(Error::Range);co_return;}uint64_t ns=uint64_t(delay)*1000000;
            if(now>UINT64_MAX-ns){fail(Error::Range);co_return;}timer={++timer_serial,now+ns,ns,args[0],fn.ref==Interval,true};out=Value::num(timer.id);co_return;
        }fail(Error::Range);co_return;
    }
    if(fn.ref==ClearTimeout){
        if(count!=1||args[0].kind!=Kind::Number){fail(Error::Type);co_return;}Numeric id=args[0].number;
        if(id<=0||id>UINT32_MAX||id!=id||id!=Numeric(int64_t(id)))co_return;
        for(auto &timer:timers){co_await point();if(timer.id==uint32_t(id))timer.active=false;}co_return;
    }
    if(fn.ref==GetId||fn.ref==Query||fn.ref==QueryAll){
        if(receiver.kind!=Kind::Document||count!=1||args[0].kind!=Kind::String||size(args[0].ref)>256){fail(Error::Type);co_return;}
        Selector selector{};if(fn.ref!=GetId){selector=Husarz::selector({text(args[0].ref),size(args[0].ref)});if(!selector.valid){fail(Error::Syntax);co_return;}}
        unsigned arr=0;if(fn.ref==QueryAll){if(array_count==ArrayLimit){fail(Error::Memory);co_return;}arr=array_count++;out=Value::handle(Kind::Array,arr);}else out=Value::handle(Kind::Null);
        uint32_t work=budget.remaining()<65536?budget.remaining():65536;
        for(unsigned i=1;i<document->node_count&&work;++i){co_await point();if(document->nodes[i].detached||document->nodes[i].is_text)continue;bool found=false;
            if(fn.ref==GetId)found=equal(document->attr(uint16_t(i),"id"),text(args[0].ref));else {unsigned before=work;co_await matches_work(*document,uint16_t(i),selector,work,found);if(!budget.consume(before-work)){fail(Error::Budget);co_return;}}
            if(found){if(fn.ref!=QueryAll){out=node(i);co_return;}auto &a=arrays[arr];if(a.count==QueryLimit){fail(Error::Range);co_return;}a.values[a.count++]=node(i);}
        }if(!work)fail(Error::Budget);co_return;
    }
    if(fn.ref==CreateElement||fn.ref==CreateText){
        if(receiver.kind!=Kind::Document||count!=1||args[0].kind!=Kind::String){fail(Error::Type);co_return;}
        unsigned value=args[0].ref;
        if(fn.ref==CreateElement){if(!size(value)||size(value)>64){fail(Error::Range);co_return;}for(unsigned i=0;i<size(value);++i){co_await point();char c=text(value)[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(i&&c>='0'&&c<='9')||c=='-')){fail(Error::Type);co_return;}}}
        struct Owner{TreeMutation *p{new TreeMutation};~Owner(){delete p;}} owner;if(!owner.p){fail(Error::Memory);co_return;}
        auto &m=*owner.p;co_await stage(m);unsigned id=create(m,fn.ref==CreateText,text(value),size(value));if(!id)co_return;unsigned identity=m.nodes[id].identity;
        co_await commit(m);out=Value::handle(Kind::Node,identity,generation);co_return;
    }
    if(fn.ref==AddListener||fn.ref==RemoveListener){
        unsigned identity=receiver.kind==Kind::Document?0:receiver.ref;
        if((receiver.kind!=Kind::Node&&receiver.kind!=Kind::Document&&receiver.kind!=Kind::Window)|| (identity&&!resolve(identity))||count<2||count>3||args[0].kind!=Kind::String||args[1].kind!=Kind::Function||size(args[0].ref)>32 || (count==3&&args[2].kind!=Kind::Boolean)){fail(Error::Type);co_return;}
        bool capture=count==3&&args[2].number!=0;
        for(auto &listener:listeners){co_await point();if(listener.active&&listener.node==identity&&is(listener.type,text(args[0].ref))&&same(listener.callback,args[1])&&listener.capture==capture){if(fn.ref==RemoveListener)listener.active=false;co_return;}}
        if(fn.ref==RemoveListener)co_return;
        for(auto &listener:listeners){co_await point();if(!listener.active){if(listener_serial==65535){fail(Error::Range);co_return;}listener={uint16_t(identity),args[0].ref,uint16_t(++listener_serial),args[1],capture,true};co_return;}}
        fail(Error::Range);co_return;
    }
    unsigned id=resolve(receiver.ref);
    if((receiver.kind!=Kind::Node&&receiver.kind!=Kind::ClassList)||!id){fail(Error::Type);co_return;}
    if(fn.ref==GetAttribute||fn.ref==SetAttribute||fn.ref==RemoveAttribute){
        if(count!=(fn.ref==SetAttribute?2U:1U)||args[0].kind!=Kind::String||(count==2&&args[1].kind!=Kind::String)){fail(Error::Type);co_return;}
        if(fn.ref==GetAttribute){const char *s=document->attr(uint16_t(id),text(args[0].ref));out=document->has_attr(uint16_t(id),text(args[0].ref))?Value::handle(Kind::String,string(s,length(s,MaxAttrLength))):Value::handle(Kind::Null);}
        else{co_await attributes(id,args[0].ref,count==2?args[1].ref:0,fn.ref==RemoveAttribute);}co_return;
    }
    if(fn.ref>=ClassAdd&&fn.ref<=ClassToggle){
        if(count!=1||args[0].kind!=Kind::String||!size(args[0].ref)||size(args[0].ref)>64){fail(Error::Type);co_return;}
        for(unsigned i=0;i<size(args[0].ref);++i)if(space(text(args[0].ref)[i])){fail(Error::Type);co_return;}
        const char *prior=document->attr(uint16_t(id),"class");unsigned n=unsigned(length(prior,MaxAttrLength)),start=0,end=0;bool found=false;
        for(unsigned p=0;p<n;){co_await point();while(p<n&&space(prior[p])){co_await point();++p;}unsigned b=p;while(p<n&&!space(prior[p])){co_await point();++p;}if(p-b==size(args[0].ref)){bool match=true;for(unsigned j=b;j<p;++j){co_await point();if(prior[j]!=text(args[0].ref)[j-b])match=false;}if(match){found=true;start=b;end=p;break;}}}
        out=Value::boolean(fn.ref==ClassToggle?!found:found);if(fn.ref==ClassContains||(fn.ref==ClassAdd&&found)||(fn.ref==ClassRemove&&!found))co_return;
        struct Buffer{char *p{new char[MaxAttrLength+1]};~Buffer(){delete[] p;}} buffer;if(!buffer.p){fail(Error::Memory);co_return;}unsigned used=0;
        if(found){for(unsigned i=0;i<n;++i){co_await point();if(i<start||i>=end)buffer.p[used++]=prior[i];}}
        else {if(n+1+size(args[0].ref)>MaxAttrLength){fail(Error::Range);co_return;}for(unsigned i=0;i<n;++i){co_await point();buffer.p[used++]=prior[i];}if(used)buffer.p[used++]=' ';for(unsigned i=0;i<size(args[0].ref);++i)buffer.p[used++]=text(args[0].ref)[i];}
        co_await attributes(id,string("class",5),string(buffer.p,used));co_return;
    }
    if(fn.ref==Append||fn.ref==Remove||fn.ref==Insert||fn.ref==Replace){
        if(count!=(fn.ref==Insert||fn.ref==Replace?2U:1U)){fail(Error::Type);co_return;}unsigned child=0,other=0;
        if(!node_arg(args[0],child)){co_return;}if(count==2&&args[1].kind!=Kind::Null&&!node_arg(args[1],other))co_return;
        if((fn.ref==Remove && document->nodes[child].parent!=id)||(fn.ref==Replace&&(!other||document->nodes[other].parent!=id))){fail(Error::Type);co_return;}
        struct Owner{TreeMutation *p{new TreeMutation};~Owner(){delete p;}} owner;if(!owner.p){fail(Error::Memory);co_return;}auto &m=*owner.p;co_await stage(m);
        if(fn.ref==Remove)unlink(m,child);
        else {if(!link(m,id,child,fn.ref==Insert||fn.ref==Replace?other:0)){fail(Error::Type);co_return;}if(fn.ref==Replace&&other!=child)unlink(m,other);}
        co_await commit(m);out=fn.ref==Replace?args[1]:args[0];co_return;
    }
    fail(Error::Type);
}
inline Work BrowserRuntime::callback(Value fn,Value arg,bool has_arg){
    budget=ScriptBudget(limits);budget_exceeded=false;error=Error::None;
    constants[callback_constant]=fn;constants[callback_constant+1]=arg;
    code[callback_ip+2].a=uint16_t(has_arg?1:0);
    if(has_arg)code[callback_ip+1].op=Op::Constant;
    else code[callback_ip+1].op=Op::Jump,code[callback_ip+1].a=uint16_t(callback_ip+2);
    if(has_arg)code[callback_ip+1].a=uint16_t(callback_constant+1);
    job_receiver=has_arg?(current_target?node(resolve(current_target)):Value::handle(Kind::Document)):Value{};co_await run(callback_ip);job_receiver={};
}
inline Work BrowserRuntime::dispatch(EventJob job){
    if(job.action==5){if(!job.related_key||prevented_keys[job.related_key%JobLimit]!=job.related_key){default_event=job;default_action=5;default_node=job.node;}co_return;}
    event=job;event_active=true;active_event=job.sequence;prevented=propagation=immediate=false;++event_dispatches;
    uint16_t path[MaxDepth+2]{};unsigned count=0;unsigned target=resolve(job.node);
    for(unsigned p=target;p&&count<MaxDepth+1;p=document->nodes[p].parent){co_await point();path[count++]=document->nodes[p].identity;}
    path[count++]=0;unsigned cutoff=listener_serial;
    // A stable handle path and listener serial cutoff make mutation deterministic.
    for(unsigned pass=0;pass<2&&!propagation;++pass){
        for(unsigned at=0;at<count&&!propagation;++at){co_await point();unsigned index=pass==0?count-1-at:at;current_target=path[index];phase=index==0?2:pass==0?1:3;
            if(current_target&&!resolve(current_target))continue;
            for(unsigned i=0;i<ListenerLimit;++i){co_await point();auto l=listeners[i];
                if(!l.active||l.serial>cutoff||l.node!=current_target||l.capture!=(pass==0)||!is(l.type,text(job.type)))continue;
                co_await callback(l.callback,Value::handle(Kind::Event,0,active_event),true);
                // A failed handler is contained; remaining listeners can proceed.
                if(error!=Error::None)error=Error::None;
            }
        }
    }
    event_active=false;
    if(job.action==4&&prevented)prevented_keys[job.sequence%JobLimit]=job.sequence;
    if(!prevented&&job.action){default_event=job;default_action=job.action;default_node=job.node;default_control=job.control;}
}
inline bool BrowserRuntime::tick(uint64_t time){
    now=time;if(!alive()){cancel();for(auto &timer:timers)timer.active=false;for(auto &listener:listeners)listener.active=false;job_count=0;return false;}
    if(task.handle){step();return true;}
    error=Error::None;budget_exceeded=false;budget=ScriptBudget(limits);driver={};driver.allow_clock=false;
    if(local_storage&&!local_storage->loaded)return false;
    if(!initialized){task=initialize();return true;}
    // Alternate pending events/timers and parser-inserted scripts. At most one
    // callback or top-level start per animation turn; no timer catch-up loop.
    for(unsigned i=0;i<fetch_count;++i)if(fetches[i].active&&fetches[i].ready){task=complete_fetch(i);return true;}
    if(microtask_count&&scheduling_turn%3!=2){++scheduling_turn;task=microtask_checkpoint();return true;}
    bool scripts_turn=(++scheduling_turn%3)==0 && script_index<document->script_count;
    // Reserve a turn for due timers even under a continuous stream of input.
    // A key awaiting its paired text event must not block unrelated callbacks.
    bool event_ready=job_count&&!(jobs[0].await_text&&!jobs[0].ctrl&&now-jobs[0].queued_at<20000000);
    bool timer_first=scheduling_turn%3==1;
    if(event_ready&&!scripts_turn&&!timer_first){auto job=jobs[0];for(unsigned i=1;i<job_count;++i)jobs[i-1]=jobs[i];--job_count;task=dispatch(job);return true;}
    for(unsigned n=0;n<TimerLimit;++n){unsigned i=(timer_cursor+n)%TimerLimit;auto &timer=timers[i];if(!scripts_turn&&timer.active&&now>=timer.due){
        timer_cursor=(i+1)%TimerLimit;auto fn=timer.callback;if(timer.repeat){timer.due=now>UINT64_MAX-timer.delay?UINT64_MAX:now+timer.delay;}else timer.active=false;
        ++timer_fires;task=callback(fn,{},false);return true;
    }}
    if(event_ready&&!scripts_turn){auto job=jobs[0];for(unsigned i=1;i<job_count;++i)jobs[i-1]=jobs[i];--job_count;task=dispatch(job);return true;}
    if(script_index<document->script_count){
        auto script=document->scripts[script_index];
        if(!document->nodes[script.node].identity||document->nodes[script.node].detached){++script_index;return true;}
        bool svg=false;for(unsigned p=script.node;p;p=document->nodes[p].parent)if(document->tag(uint16_t(p),"svg")){svg=true;break;}
        if(svg){++script_index;return true;}
        const char *type=document->str(script.type);
        if(*type&&!equal_ci(type,length(type,128),"text/javascript")&&!equal_ci(type,length(type,128),"application/javascript")){++script_index;return true;}
        if(script.truncated){++script_index;fail(Error::Range);return true;}
        const char *source=document->str(script.source);unsigned n=document->nodes[script.node].text_length;
        if(*document->str(script.url)){
            auto &s=sources[script_index];if(!s.ready)return false;
            if(s.failed||!s.entry){++script_index;return true;}source=reinterpret_cast<const char*>(s.entry->raw);n=unsigned(s.entry->size);
        }
        ++script_index;source_index=uint16_t(script_index);task=script_job(source,n);return true;
    }
    if(!load_sent){load_sent=true;unsigned body=0;for(unsigned i=1;i<document->node_count;++i)if(document->tag(uint16_t(i),"body")){body=i;break;}queue(body,"load");return true;}
    return false;
}
inline Work BrowserRuntime::event_property(unsigned node,unsigned name,bool write,Value &value){
    unsigned type=string(text(name)+2,size(name)-2);
    if(write && value.kind!=Kind::Function && value.kind!=Kind::Null && value.kind!=Kind::Undefined){fail(Error::Type);co_return;}
    for(auto &listener:listeners){co_await point();if(listener.active&&listener.property&&listener.node==node&&is(listener.type,text(type))){if(write){if(value.kind==Kind::Function)listener.callback=value;else listener.active=false;}else value=listener.callback;co_return;}}
    if(!write){value=Value::handle(Kind::Null);co_return;}if(value.kind!=Kind::Function)co_return;
    for(auto &listener:listeners){co_await point();if(!listener.active){if(listener_serial==65535){fail(Error::Range);co_return;}listener={uint16_t(node),uint16_t(type),uint16_t(++listener_serial),value,false,true,true};co_return;}}
    fail(Error::Range);
}
inline Work BrowserRuntime::serialize(unsigned id,Value &value){
    struct Output {
        char *buffer{new char[StringLimit+1]};unsigned used{};
        ~Output(){delete[] buffer;}
        bool append(const char *source,size_t n,bool escape=false){
            for(size_t i=0;i<n;++i){const char *entity=escape?(source[i]=='&'?"&amp;":source[i]=='<'?"&lt;":source[i]=='>'?"&gt;":source[i]=='"'?"&quot;":nullptr):nullptr;
                size_t k=entity?length(entity,6):1;if(k>StringLimit-used)return false;for(size_t j=0;j<k;++j)buffer[used++]=entity?entity[j]:source[i];}return true;
        }
    } out;
    if(!out.buffer){fail(Error::Memory);co_return;}auto &d=*document;unsigned current=d.nodes[id].first;
    while(current){co_await point();const auto &n=d.nodes[current];const char *tag=d.str(n.tag);bool ok=true;
        if(n.is_text){co_await point((n.text_length+63)/64);ok=out.append(d.str(n.text),n.text_length,true);}
        else {
            ok=out.append("<",1)&&out.append(tag,length(tag,64));
            for(unsigned i=0;i<n.attr_count&&ok;++i){co_await point();auto a=d.attrs[n.attr_start+i];const char *name=d.str(a.name),*val=d.str(a.value);unsigned len=unsigned(length(val,MaxAttrLength));co_await point((len+63)/64);ok=out.append(" ",1)&&out.append(name,length(name,64))&&out.append("=\"",2)&&out.append(val,len,true)&&out.append("\"",1);}
            ok=ok&&out.append(">",1);
            if(n.raw){co_await point((n.text_length+63)/64);ok=ok&&out.append(d.str(n.text),n.text_length,true);}
        }
        if(!ok){fail(Error::Range);co_return;}
        if(n.first){current=n.first;continue;}
        for(;;){co_await point();const auto &node=d.nodes[current];if(!node.is_text&&!void_tag(d.str(node.tag))){const char *name=d.str(node.tag);if(!out.append("</",2)||!out.append(name,length(name,64))||!out.append(">",1)){fail(Error::Range);co_return;}}
            if(node.next){current=node.next;break;}current=node.parent;if(current==id){current=0;break;}if(!current)break;
        }
    }
    value=Value::handle(Kind::String,string(out.buffer,out.used));
}
} } // namespace Husarz::Js
