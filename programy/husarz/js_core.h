#pragma once
#include "script.h"
#include "work.h"
#include "js_number.h"
namespace Husarz { namespace Js {
// A document arena: no host runtime, pointer-valued script data, or GC pauses.
constexpr unsigned SourceLimit=32768, TokenLimit=8192, CodeLimit=16384;
constexpr unsigned StringLimit=4096, StringCount=2048, StringBytes=128*1024;
constexpr unsigned StackLimit=256, FrameLimit=32, ScopeLimit=256, BindingLimit=1024;
constexpr unsigned ObjectLimit=128, ArrayLimit=64, PropertyLimit=32, ArrayLength=128;
constexpr unsigned FunctionLimit=128, ParseDepth=32, SliceInstructions=256, PrototypeDepth=16;
enum class Error : uint8_t { None, Syntax, Type, Reference, Range, Memory, Budget, Cancelled };
enum class Kind : uint8_t { Undefined, Null, Boolean, Number, String, Object, Array, Function, Native, Node, Document, Window, Console, Style, ClassList, Event, Location, Promise, Response, Storage };
struct Value {
    Kind kind{Kind::Undefined}; uint16_t ref{}; uint32_t aux{}; Numeric number{};
    static Value num(Numeric n){Value v;v.kind=Kind::Number;v.number=n;return v;}
    static Value boolean(bool b){auto v=num(b?1:0);v.kind=Kind::Boolean;return v;}
    static Value handle(Kind k,unsigned r=0,unsigned a=0){Value v;v.kind=k;v.ref=uint16_t(r);v.aux=a;return v;}
};
struct String {uint32_t offset{};uint16_t size{};};
struct Property {uint16_t name{};Value value{};};
struct Object {Property props[PropertyLimit]{};uint16_t count{};Value prototype{Value::handle(Kind::Null)};};
struct Array {Value values[ArrayLength]{};uint16_t count{};};
struct Binding {uint16_t name{},scope{};Value value{};bool constant{},initialized{};uint16_t next{};};
struct Scope {uint16_t parent{};bool function{},retained{};uint16_t locals{},head{};};
enum class Op : uint8_t { Constant, Load, Declare, Store, Pop, Dup, DupPair, Get, Set, Binary, Unary,
    Jump, JumpFalse, JumpTrue, Enter, Leave, Function, Call, Return, Array, Object, This, GetCall, Construct, Await, Try, EndTry, Catch, Throw, EndFinally, AbruptJump, Halt };
struct Instruction {Op op{};uint8_t flags{};uint16_t a{},b{};uint16_t line{},column{},source{};};
struct Function {uint16_t entry{},end{},params[8]{},count{};bool arrow{},async{};};
struct Closure {uint16_t function{},scope{},object{};Value lexical_this{};};
struct Frame {uint16_t ip{},scope{},base{};Value receiver{},constructed{};bool constructor{};uint16_t promise{};uint8_t mode{};uint16_t executor_promise{UINT16_MAX};};
constexpr unsigned HandlerLimit=64;
struct HandlerCode {uint16_t begin{},caught{},finalizer{},end{};};
enum class CompletionKind:uint8_t { Normal, Return, Throw, Jump };
struct Completion {CompletionKind kind{};Value value{};uint16_t target{},scope{};};
struct Handler {uint16_t code{},frame{},scope{},sp{};uint8_t phase{};Completion pending{};};
#include "js_promise_types.h"
enum TokenKind : uint16_t { End=256, Identifier, Number, StringToken, Eq, Ne, StrictEq, StrictNe, Le, Ge, And, Or, AddAssign, SubAssign, MulAssign, DivAssign, Inc, Dec, Arrow };
struct Token {uint16_t kind{},text{},line{},column{};Numeric number{};};
struct VmYield {
    bool await_ready()const noexcept{return false;}
    void await_suspend(std::coroutine_handle<> h)const noexcept{running_work->current=h;running_work->exhausted=true;}
    void await_resume()const noexcept{}
};
struct BudgetPoint {
    ScriptBudget &budget;bool &exceeded;uint32_t units{1};
    bool await_ready()const noexcept{if(!budget.consume(units)){exceeded=true;running_work->exhausted=true;return false;}return WorkPoint{units}.await_ready();}
    void await_suspend(std::coroutine_handle<> h)const noexcept{WorkPoint{units}.await_suspend(h);}
    void await_resume()const noexcept{WorkPoint{units}.await_resume();}
};
class Runtime;
class Compiler;
class Runtime : public ScriptExecutor {
  public:
    Error error{},last_error{};uint16_t error_line{},error_column{},last_error_line{},last_error_column{};
    uint16_t source_index{},error_source{},last_error_source{};
    uint32_t tab{},navigation{},generation{},instructions{},maximum_stack{},maximum_frames{},mutations{},errors{},executions{};
    bool running{},compiling{},cancelled{},budget_exceeded{};
    BudgetPoint point(unsigned units=1){return {budget,budget_exceeded,units};}
    Document *document{};
    ScriptLimits limits{};ScriptBudget budget{};
    String strings[StringCount]{};char bytes[StringBytes]{};unsigned string_count{1},bytes_used{1};
    Value constants[2048]{};unsigned constant_count{};
    Instruction code[CodeLimit]{};unsigned code_count{};
    Function functions[FunctionLimit]{};unsigned function_count{};
    Closure closures[FunctionLimit]{};unsigned closure_count{};
    Scope scopes[ScopeLimit]{};unsigned scope_count{1};
    Binding bindings[BindingLimit]{};unsigned binding_count{},binding_free{};
    Object objects[ObjectLimit]{};unsigned object_count{};
    Array arrays[ArrayLimit]{};unsigned array_count{};
    Value stack[StackLimit]{};unsigned sp{};Frame frames[FrameLimit]{};unsigned frame_count{};
    unsigned ip{},scope{};Value result{},this_value{},job_receiver{};
    bool core_initialized{};Value object_prototype{},array_prototype{},function_prototype{},string_prototype{},number_prototype{};
    Work initialize_core();
    bool native_constructor(unsigned id)const;
    Work json(Value input,bool stringify,Value &out);
    PromiseRecord promises[PromiseLimit]{};unsigned promise_count{};
    Reaction reactions[ReactionLimit]{};unsigned reaction_count{};
    Microtask microtasks[MicrotaskLimit]{};unsigned microtask_head{},microtask_count{},microtasks_run{};
    Continuation *continuations[ContinuationLimit]{};
    Value native_call{},native_args[2]{},native_result{};unsigned native_count{};uint8_t native_mode{};
    unsigned job_ip{},job_constant{};bool job_initialized{},reaction_job{};
    bool async_boundary()const{for(unsigned i=0;i<frame_count;++i)if(frames[i].mode)return true;return false;}
    Value promise(){if(promise_count==PromiseLimit){fail(Error::Memory);return {};}return Value::handle(Kind::Promise,promise_count++);}
    bool callable(Value f)const{return (f.kind==Kind::Function&&f.ref<closure_count)||f.kind==Kind::Native;}
    bool enqueue(Microtask m){if(microtask_count==MicrotaskLimit){fail(Error::Memory);return false;}microtasks[(microtask_head+microtask_count++)%MicrotaskLimit]=m;return true;}
    Work settle(Value promise_value,Value value,bool rejected=false);
    Work react(Value source,Value fulfilled,Value rejected,Value child,unsigned mode=0);
    Work microtask_checkpoint(unsigned maximum=MicrotasksPerTurn);
    Work invoke_job(Value fn,Value arg,bool has_arg=true);
    void clear_async(){microtask_count=microtask_head=0;for(auto *&c:continuations){delete c;c=nullptr;}}
    Work suspend_await(Value awaited);
    Work resume_continuation(unsigned id);
    HandlerCode handler_code[HandlerLimit]{};unsigned handler_code_count{};
    Handler handlers[HandlerLimit]{};unsigned handler_count{};Value thrown{};bool unhandled{};
    Value error_object(Error e){Value out=object(object_prototype);if(error!=Error::None)return out;const char *name=e==Error::Reference?"ReferenceError":e==Error::Range?"RangeError":e==Error::Syntax?"SyntaxError":"TypeError";auto &o=objects[out.ref];o.props[o.count++]={uint16_t(string("name",4)),Value::handle(Kind::String,string(name,length(name,32)))};o.props[o.count++]={uint16_t(string("message",7)),Value::handle(Kind::String,string("JavaScript operation failed",27))};return out;}
    Error error_category(Value v)const{if(v.kind==Kind::Object&&v.ref<object_count){const auto &o=objects[v.ref];for(unsigned i=0;i<o.count;++i)if(is(o.props[i].name,"name")&&o.props[i].value.kind==Kind::String){unsigned s=o.props[i].value.ref;if(is(s,"RangeError"))return Error::Range;if(is(s,"ReferenceError"))return Error::Reference;if(is(s,"SyntaxError"))return Error::Syntax;}}return Error::Type;}
    Work complete(Completion c){
        for(;;){
            if(c.kind==CompletionKind::Throw){unsigned floor=handler_count?handlers[handler_count-1].frame:0;unsigned boundary=frame_count;while(boundary>floor&&!frames[boundary-1].mode)--boundary;
                if(boundary>floor&&boundary<frame_count){while(frame_count>boundary){auto f=frames[--frame_count];while(scope&&!scopes[scope].function)co_await leave_scope();if(scope)co_await leave_scope();scope=f.scope;sp=f.base;this_value=f.receiver;budget.leave();}continue;}}
            if(c.kind==CompletionKind::Throw&&frame_count&&frames[frame_count-1].mode&&(!handler_count||handlers[handler_count-1].frame<frame_count)){
                auto f=frames[--frame_count];while(scope&&!scopes[scope].function)co_await leave_scope();if(scope)co_await leave_scope();ip=f.ip;scope=f.scope;sp=f.base;this_value=f.receiver;budget.leave();Value p=Value::handle(Kind::Promise,f.promise);co_await settle(p,c.value,true);if(f.mode==3){running=false;co_return;}push(f.executor_promise!=UINT16_MAX?Value::handle(Kind::Promise,f.executor_promise):p);co_return;
            }
            if(!handler_count)break;
            co_await point();auto &h=handlers[handler_count-1];auto meta=handler_code[h.code];
            if(c.kind!=CompletionKind::Throw&&h.frame!=frame_count)break;
            if((c.kind==CompletionKind::Jump||c.kind==CompletionKind::Normal)&&c.target>=meta.begin&&c.target<meta.end)break;
            while(frame_count>h.frame){auto f=frames[--frame_count];while(scope&&scope!=f.scope)co_await leave_scope();scope=f.scope;sp=f.base;this_value=f.receiver;budget.leave();}
            while(scope!=h.scope&&scope){co_await leave_scope();}scope=h.scope;sp=h.sp;
            if(h.phase==2){--handler_count;continue;}
            if(c.kind==CompletionKind::Throw&&h.phase==0&&meta.caught){h.phase=1;thrown=c.value;ip=meta.caught;co_return;}
            if(meta.finalizer){h.phase=2;h.pending=c;ip=meta.finalizer;co_return;}
            --handler_count;
        }
        if(c.kind==CompletionKind::Normal){ip=c.target;co_return;}
        if(c.kind==CompletionKind::Jump){while(scope!=c.scope&&scope)co_await leave_scope();scope=c.scope;ip=c.target;co_return;}
        if(c.kind==CompletionKind::Throw){thrown=c.value;unhandled=true;fail(error_category(c.value));co_return;}
        Value v=c.value;
        if(!frame_count){result=v;running=false;co_return;}
        auto f=frames[--frame_count];while(scope&&!scopes[scope].function)co_await leave_scope();if(scope)co_await leave_scope();
        ip=f.ip;scope=f.scope;sp=f.base;this_value=f.receiver;if(f.constructor&&!object_like(v))v=f.constructed;budget.leave();
        if(f.mode){Value p=Value::handle(Kind::Promise,f.promise);if(f.mode!=1)co_await settle(p,v);v=p;if(f.mode==3){running=false;co_return;}}if(f.executor_promise!=UINT16_MAX)v=Value::handle(Kind::Promise,f.executor_promise);push(v);
    }

    Value object(Value proto){unsigned depth=1;for(Value p=proto;p.kind!=Kind::Null;p=objects[p.ref].prototype){if(p.kind!=Kind::Object||p.ref>=object_count){fail(Error::Type);return {};}if(depth++>=PrototypeDepth){fail(Error::Range);return {};}}if(object_count==ObjectLimit){fail(Error::Memory);return {};}unsigned i=object_count++;objects[i].prototype=proto;return Value::handle(Kind::Object,i);}
    static bool object_like(Value v){return unsigned(v.kind)>=unsigned(Kind::Object);}
    Value prototype(Value v)const{if(v.kind==Kind::Object&&v.ref<object_count)return objects[v.ref].prototype;if(v.kind==Kind::Array&&v.ref<array_count)return array_prototype;if(v.kind==Kind::Function&&v.ref<closure_count)return function_prototype;if(v.kind==Kind::String)return string_prototype;if(v.kind==Kind::Number)return number_prototype;return Value::handle(Kind::Null);}
    Work task{};WorkDriver driver{};
    // Hooks are C++ capabilities. They are never represented as script values.
    virtual Work native(Value fn,Value *args,unsigned count,Value &out);
    virtual Work property(Value base,Value key,bool write,Value &value);
    virtual void aborted(){
        // At most 256 scopes and 1,024 bindings; captured chains stay owned.
        for(unsigned s=1;s<scope_count;++s)if(!scopes[s].retained){unsigned entry=scopes[s].head;while(entry){auto &b=bindings[entry-1];unsigned next=b.next;b.value={};b.initialized=false;b.next=uint16_t(binding_free);binding_free=entry;entry=next;}scopes[s].head=scopes[s].locals=0;}
        while(scope_count>1&&!scopes[scope_count-1].retained)--scope_count;
        scope=sp=frame_count=handler_count=0;
    }
    virtual bool alive()const{return !cancelled && (!document || document->generation==generation);}
    ~Runtime() override {task.cancel();clear_async();}
    void fail(Error e){if(error==Error::None){error=last_error=e;last_error_source=error_source;last_error_line=error_line;last_error_column=error_column;++errors;}}
    const char *text(unsigned s)const{return s<string_count?bytes+strings[s].offset:"";}
    unsigned size(unsigned s)const{return s<string_count?strings[s].size:0;}
    unsigned string(const char *s,size_t n){
        if(n>StringLimit || n+1>StringBytes-bytes_used || string_count==StringCount){fail(Error::Memory);return 0;}
        for(size_t p=0;p<n;){size_t used=0;uint32_t cp=utf8(s+p,n-p,used);if(!s[p] || (cp==0xfffd && (used!=3 || uint8_t(s[p])!=0xef))){fail(Error::Type);return 0;}p+=used;}
        // Strings are immutable arena allocations. Name equality is by bounded bytes.
        unsigned id=string_count++;strings[id]={bytes_used,uint16_t(n)};
        for(unsigned i=0;i<n;++i){bytes[bytes_used++]=s[i];}bytes[bytes_used++]=0;return id;
    }
    bool is(unsigned s,const char *v)const{return equal(text(s),v);}
    unsigned constant(Value v){if(constant_count==2048){fail(Error::Memory);return 0;}constants[constant_count]=v;return constant_count++;}
    bool truth(Value v)const {return v.kind!=Kind::Undefined && v.kind!=Kind::Null && !((v.kind==Kind::Boolean || v.kind==Kind::Number) && (v.number==0 || v.number!=v.number)) && !(v.kind==Kind::String && !size(v.ref));}
    bool same(Value a,Value b)const{return a.kind==b.kind && ((a.kind==Kind::Number || a.kind==Kind::Boolean)?a.number==b.number:(a.kind==Kind::String?equal(text(a.ref),text(b.ref)):a.ref==b.ref && a.aux==b.aux));}
    void push(Value v){if(sp==StackLimit)fail(Error::Range);else {stack[sp++]=v;if(sp>maximum_stack)maximum_stack=sp;}}
    Value pop(){if(!sp){fail(Error::Range);return {};}return stack[--sp];}
    unsigned new_scope(unsigned parent,bool function=false){if(scope_count==ScopeLimit){fail(Error::Memory);return 0;}unsigned s=scope_count++;scopes[s]={uint16_t(parent),function};return s;}
    Work binding(unsigned name,bool write,bool declare,bool constant,Value &value,bool function_var=false){
        unsigned target=scope;
        if(function_var)while(target && !scopes[target].function){co_await point();target=scopes[target].parent;}
        for(unsigned s=declare?target:scope;;s=scopes[s].parent){
            for(unsigned entry=scopes[s].head;entry;entry=bindings[entry-1].next){co_await point();auto &b=bindings[entry-1];
                if(b.scope!=s || !equal(text(b.name),text(name)))continue;
                if(declare && !function_var){fail(Error::Syntax);co_return;}
                if(write || declare){if(b.constant){fail(Error::Type);co_return;}b.value=value;}else value=b.value;co_return;
            }
            if(declare || !s)break;
        }
        if(!declare){fail(Error::Reference);co_return;}
        unsigned slot=0;
        if(binding_free){slot=binding_free-1;binding_free=bindings[slot].next;}
        else {if(binding_count==BindingLimit){fail(Error::Memory);co_return;}slot=binding_count++;}
        bindings[slot]={uint16_t(name),uint16_t(target),value,constant,true,scopes[target].head};scopes[target].head=uint16_t(slot+1);++scopes[target].locals;
    }
    Work leave_scope(){
        unsigned old=scope;scope=scopes[old].parent;
        if(!old || scopes[old].retained)co_return;
        if(!scopes[old].locals){if(old+1==scope_count)--scope_count;co_return;}
        unsigned entry=scopes[old].head;while(entry){co_await point();auto &b=bindings[entry-1];unsigned next=b.next;b.value={};b.initialized=false;b.next=uint16_t(binding_free);binding_free=entry;entry=next;}
        scopes[old].head=scopes[old].locals=0;if(old+1==scope_count)--scope_count;
    }
    Work convert_string(Value v,unsigned &out){
        if(v.kind==Kind::String){out=v.ref;co_return;}
        const char *literal=v.kind==Kind::Undefined?"undefined":v.kind==Kind::Null?"null":v.kind==Kind::Boolean?(v.number?"true":"false"):"[object Object]";
        if(v.kind!=Kind::Number){out=string(literal,length(literal,32));co_return;}
        char buffer[64]{};unsigned n=numeric_format(v.number,buffer);
        out=string(buffer,n);
    }
    Work binary(unsigned op,Value a,Value b,Value &out){
        if(op==Eq || op==StrictEq || op==Ne || op==StrictNe){bool v=same(a,b);if(op==Eq || op==Ne)if((a.kind==Kind::Null && b.kind==Kind::Undefined)||(b.kind==Kind::Null && a.kind==Kind::Undefined))v=true;out=Value::boolean(op==Ne || op==StrictNe?!v:v);co_return;}
        if(op=='+' && (a.kind==Kind::String || b.kind==Kind::String)){
            unsigned x=0,y=0;co_await convert_string(a,x);co_await convert_string(b,y);unsigned n=size(x),m=size(y);
            if(n>StringLimit-m){fail(Error::Range);co_return;}
            struct Buffer{char *p{};~Buffer(){delete[] p;}} buffer;buffer.p=new char[n+m+1];if(!buffer.p){fail(Error::Memory);co_return;}
            for(unsigned i=0;i<n+m;++i){co_await point();buffer.p[i]=i<n?text(x)[i]:text(y)[i-n];}
            out=Value::handle(Kind::String,string(buffer.p,n+m));co_return;
        }
        if(a.kind!=Kind::Number || b.kind!=Kind::Number){fail(Error::Type);co_return;}
        Numeric x=a.number,y=b.number;
        if(op=='<'||op==Le||op=='>'||op==Ge){out=Value::boolean(op=='<'?x<y:op==Le?x<=y:op=='>'?x>y:x>=y);co_return;}
        if(op=='%')out=Value::num(Numeric::remainder(x,y));
        else out=Value::num(op=='+'?x+y:op=='-'?x-y:op=='*'?x*y:x/y);
    }
    bool validate(unsigned begin,unsigned end){
        if(begin>=end || end>code_count)return false;
        for(unsigned i=begin;i<end;++i){const auto &c=code[i];
            if(unsigned(c.op)>unsigned(Op::Halt))return false;
            if(c.op==Op::Constant && c.a>=constant_count)return false;
            if((c.op==Op::Jump||c.op==Op::JumpFalse||c.op==Op::JumpTrue||c.op==Op::AbruptJump) && (c.a<begin||c.a>=end))return false;
            if(c.op==Op::Function && c.a>=function_count)return false;
        }return true;
    }
    Work run(unsigned start,bool resume=false){
        if(!resume){ip=start;sp=frame_count=handler_count=0;unhandled=false;scope=0;this_value=job_receiver;}running=true;++executions;
        while(running && error==Error::None && alive()){
            co_await point();if(budget_exceeded){fail(Error::Budget);break;}++instructions;
            if(ip>=code_count){fail(Error::Range);break;}
            auto c=code[ip++];error_line=c.line;error_column=c.column;error_source=c.source;
            Value a{},b{},v{};
            switch(c.op){
            case Op::Constant:if(c.a>=constant_count)fail(Error::Range);else push(constants[c.a]);break;
            case Op::Load:co_await binding(c.a,false,false,false,v);push(v);break;
            case Op::Declare:v=pop();co_await binding(c.a,true,true,c.flags==2,v,c.flags==0);break;
            case Op::Store:v=pop();co_await binding(c.a,true,false,false,v);push(v);break;
            case Op::Pop:result=pop();break;
            case Op::Dup:if(!sp)fail(Error::Range);else push(stack[sp-1]);break;
            case Op::DupPair:if(sp<2)fail(Error::Range);else{a=stack[sp-2];b=stack[sp-1];push(a);push(b);}break;
            case Op::This:push(this_value);break;
            case Op::GetCall:case Op::Get:b=pop();a=pop();co_await property(a,b,false,v);push(v);if(c.op==Op::GetCall)push(a);break;
            case Op::Set:v=pop();b=pop();a=pop();co_await property(a,b,true,v);push(v);break;
            case Op::Binary:b=pop();a=pop();co_await binary(c.a,a,b,v);push(v);break;
            case Op::Unary:a=pop();if(c.a=='!')v=Value::boolean(!truth(a));else if(a.kind!=Kind::Number)fail(Error::Type);else v=Value::num(c.a=='-'?-a.number:a.number);push(v);break;
            case Op::Jump:ip=c.a;break;
            case Op::JumpFalse:a=pop();if(!truth(a))ip=c.a;break;
            case Op::JumpTrue:a=pop();if(truth(a))ip=c.a;break;
            case Op::Enter:scope=new_scope(scope);break;
            case Op::Leave:co_await leave_scope();break;
            case Op::Function:if(c.a>=function_count){fail(Error::Range);break;}if(closure_count==FunctionLimit)fail(Error::Memory);else {for(unsigned s=scope;;s=scopes[s].parent){co_await point();scopes[s].retained=true;if(!s)break;}Value storage=object(function_prototype);if(error!=Error::None)break;
                    unsigned ci=closure_count++;closures[ci]={c.a,uint16_t(scope),storage.ref,this_value};
                    Value fn=Value::handle(Kind::Function,ci);
                    if(!functions[c.a].arrow){Value proto=object(object_prototype),key=Value::handle(Kind::String,string("prototype",9));co_await property(fn,key,true,proto);Value name=Value::handle(Kind::String,string("constructor",11));co_await property(proto,name,true,fn);}
                    push(fn);}break;
            case Op::Array:if(array_count==ArrayLimit || c.a>ArrayLength || c.a>sp)fail(Error::Range);else {auto &arr=arrays[array_count];arr.count=c.a;for(unsigned i=c.a;i-- >0;){co_await point();arr.values[i]=pop();}push(Value::handle(Kind::Array,array_count++));}break;
            case Op::Object:push(object(object_prototype));break;
            case Op::Construct:case Op::Call:{
                unsigned extra=c.flags==1?1:0;
                if(c.a>8 || sp<c.a+1U+extra){fail(Error::Range);break;}
                unsigned base=sp-c.a-1-extra;a=stack[base];Value receiver=c.flags==2?job_receiver:extra?stack[base+1]:Value{},created{};
                bool constructing=c.op==Op::Construct;
                if(constructing){
                    if(a.kind==Kind::Function&&a.ref<closure_count&&!functions[closures[a.ref].function].arrow&&!functions[closures[a.ref].function].async){
                        Value key=Value::handle(Kind::String,string("prototype",9)),proto{};co_await property(a,key,false,proto);
                        created=object(proto.kind==Kind::Object?proto:object_prototype);receiver=created;
                    }else if(a.kind!=Kind::Native||!native_constructor(a.ref)){fail(Error::Type);break;}
                }
                if(a.kind==Kind::Native){
                    native_call={};if(a.aux>>16)a.aux=(unsigned(receiver.kind)<<16)|receiver.ref;co_await native(a,stack+base+1+extra,c.a,v);
                    if(native_call.kind==Kind::Undefined){sp=base;push(v);break;}
                    a=native_call;native_result=v;constructing=false;for(unsigned i=0;i<native_count;++i)stack[base+1+i]=native_args[i];c.a=uint16_t(native_count);extra=0;
                }else native_mode=0;
                if(a.kind==Kind::Function && a.ref<closure_count){
                    if(frame_count==FrameLimit || !budget.enter()){fail(Error::Range);break;}
                    auto cl=closures[a.ref];auto f=functions[cl.function];
                    uint16_t executor=f.async&&native_mode==1?native_result.ref:UINT16_MAX;Value async_p=f.async?promise():native_result;uint8_t mode=f.async?2:native_mode;native_mode=0;
                    if(error!=Error::None)break;
                    frames[frame_count++]={uint16_t(ip),uint16_t(scope),uint16_t(base),this_value,created,constructing,async_p.ref,mode,executor};
                    if(frame_count>maximum_frames)maximum_frames=frame_count;
                    this_value=f.arrow?cl.lexical_this:receiver;scope=new_scope(cl.scope,true);if(error!=Error::None)break;
                    for(unsigned i=0;i<f.count;++i){co_await point();v=i<c.a?stack[base+1+extra+i]:Value{};co_await binding(f.params[i],true,true,false,v);}
                    sp=base;ip=f.entry;
                }else fail(Error::Type);break;
            }
            case Op::Await:v=pop();co_await suspend_await(v);if(!running)co_return;break;
            case Op::Try:if(c.a>=handler_code_count||handler_count==HandlerLimit)fail(Error::Range);else handlers[handler_count++]={c.a,uint16_t(frame_count),uint16_t(scope),uint16_t(sp)};break;
            case Op::Catch:v=thrown;co_await binding(c.a,true,true,false,v);break;
            case Op::Throw:v=pop();co_await complete({CompletionKind::Throw,v});break;
            case Op::EndTry:if(!handler_count)fail(Error::Range);else {auto end=handler_code[handlers[handler_count-1].code].end;co_await complete({CompletionKind::Normal,{},end});}break;
            case Op::EndFinally:if(!handler_count||handlers[handler_count-1].phase!=2)fail(Error::Range);else {auto pending=handlers[--handler_count].pending;co_await complete(pending);}break;
            case Op::AbruptJump:{unsigned target=scope;for(unsigned i=0;i<c.b&&target;++i)target=scopes[target].parent;co_await complete({CompletionKind::Jump,{},c.a,uint16_t(target)});break;}
            case Op::Return:v=pop();co_await complete({CompletionKind::Return,v});if(!running)co_return;break;
            case Op::Halt:running=false;co_return;
            }
            if(error!=Error::None&&!unhandled&&!budget_exceeded&&(error==Error::Type||error==Error::Reference||error==Error::Range||error==Error::Syntax)&&(handler_count||reaction_job||async_boundary())){Error e=error;error=Error::None;Value ex=error_object(e);if(error==Error::None)co_await complete({CompletionKind::Throw,ex});}
            if(ip>=code_count && error==Error::None)fail(Error::Range);
            // A smaller VM slice in addition to the shared 2,048 work-unit ceiling.
            if(!(instructions%SliceInstructions))co_await VmYield{};
        }
        if(!alive() && error==Error::None)fail(Error::Cancelled);
        // Runtime failures unwind explicit VM frames just like budget aborts.
        // Keep event dispatch alive so its remaining handlers/default can run.
        if(error!=Error::None)Runtime::aborted();
        running=false;
    }
    Work compile_and_run(const char *source,size_t n);
    ScriptResult execute(ScriptDom &dom,const char *source,size_t n,ScriptBudget &unused) override;
    bool start(const char *source,size_t n);
    bool step(){if(!task.handle)return true;bool done=task.step(driver);if(budget_exceeded){fail(Error::Budget);task.cancel();aborted();running=compiling=false;return true;}if(done){task.cancel();if(driver.failed)fail(Error::Memory);running=compiling=false;}return done;}
    void cancel(){cancelled=true;task.cancel();clear_async();running=compiling=false;}
};
struct Lexer {
    Runtime &r;const char *source;size_t size{},pos{};unsigned line{1},column{1},count{};
    Token tokens[TokenLimit]{};
    char scratch[StringLimit+1]{};
    Lexer(Runtime &runtime,const char *s,size_t n):r(runtime),source(s),size(n){}
    char peek(unsigned off=0)const{return pos+off<size?source[pos+off]:0;}
    char take(){char c=peek();if(pos<size){++pos;if(c=='\n'){++line;column=1;}else ++column;}return c;}
    static bool first(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||c=='$';}
    static bool digit(char c){return c>='0'&&c<='9';}
    Work scan(){
        if(size>SourceLimit){r.fail(Error::Range);co_return;}
        while(pos<size && r.error==Error::None){co_await WorkPoint{1};
            if(space(peek())){take();continue;}
            if(peek()=='/' && peek(1)=='/'){while(pos<size && peek()!='\n'){co_await WorkPoint{1};take();}continue;}
            if(peek()=='/' && peek(1)=='*'){take();take();while(pos<size && !(peek()=='*'&&peek(1)=='/')){co_await WorkPoint{1};take();}if(pos==size){r.fail(Error::Syntax);break;}take();take();continue;}
            if(count+1>=TokenLimit){r.fail(Error::Range);break;}
            Token t{};t.line=uint16_t(line);t.column=uint16_t(column);char c=take();t.kind=uint8_t(c);
            if(first(c)){
                unsigned n=0;scratch[n++]=c;
                while(first(peek())||digit(peek())){co_await WorkPoint{1};if(n==64){r.fail(Error::Range);break;}scratch[n++]=take();}
                t.kind=Identifier;t.text=uint16_t(r.string(scratch,n));
            }else if(digit(c)||(c=='.'&&digit(peek()))){
                unsigned begin=unsigned(pos-1),token_size=1;
                bool hex=c=='0'&&(peek()=='x'||peek()=='X');
                if(hex){take();while((peek()>='0'&&peek()<='9')||(peek()>='a'&&peek()<='f')||(peek()>='A'&&peek()<='F')){co_await WorkPoint{1};take();}}
                else {while(digit(peek())||peek()=='.'){co_await WorkPoint{1};take();}
                    if(peek()=='e'||peek()=='E'){take();if(peek()=='+'||peek()=='-')take();while(digit(peek())){co_await WorkPoint{1};take();}}}
                token_size=unsigned(pos)-begin;
                if(token_size>128)r.fail(Error::Range);
                bool ok=false;t.number=numeric_parse(source+begin,token_size,ok);t.kind=Number;
                if(!ok)r.fail(Error::Syntax);
            }else if(c=='\''||c=='"'){
                char quote=c;unsigned n=0;bool closed=false;
                while(pos<size){co_await WorkPoint{1};c=take();if(c==quote){closed=true;break;}if(c=='\n'||c=='\r'||!c){r.fail(Error::Syntax);break;}
                    if(c=='\\'){
                        if(pos==size){r.fail(Error::Syntax);break;}c=take();
                        if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c!='\\'&&c!='\''&&c!='"'){r.fail(Error::Syntax);break;}
                    }
                    if(uint8_t(c)>=128){size_t used=0;uint32_t cp=utf8(source+pos-1,size-pos+1,used);
                        if(cp==0xfffd && (used!=3 || uint8_t(c)!=0xef)){r.fail(Error::Syntax);break;}
                        if(n+used>StringLimit){r.fail(Error::Range);break;}scratch[n++]=c;for(size_t i=1;i<used;++i)scratch[n++]=take();
                    }else {if(n==StringLimit){r.fail(Error::Range);break;}scratch[n++]=c;}
                }
                if(!closed){r.fail(Error::Syntax);}t.kind=StringToken;t.text=uint16_t(r.string(scratch,n));
            }else {
                char d=peek();
                if(c=='='&&d=='>'){take();t.kind=Arrow;}
                else if(c=='='&&d=='='){take();t.kind=Eq;if(peek()=='='){take();t.kind=StrictEq;}}
                else if(c=='!'&&d=='='){take();t.kind=Ne;if(peek()=='='){take();t.kind=StrictNe;}}
                else if((c=='<'||c=='>')&&d=='='){take();t.kind=c=='<'?Le:Ge;}
                else if((c=='&'&&d=='&')||(c=='|'&&d=='|')){take();t.kind=c=='&'?And:Or;}
                else if((c=='+'||c=='-')&&d==c){take();t.kind=c=='+'?Inc:Dec;}
                else if(d=='='&&(c=='+'||c=='-'||c=='*'||c=='/')){take();t.kind=c=='+'?AddAssign:c=='-'?SubAssign:c=='*'?MulAssign:DivAssign;}
                else if(!c || uint8_t(c)>=128 || !(c=='{'||c=='}'||c=='['||c==']'||c=='('||c==')'||c=='.'||c==','||c==';'||c==':'||c=='?'||c=='+'||c=='-'||c=='*'||c=='/'||c=='%'||c=='<'||c=='>'||c=='!'||c=='='))r.fail(Error::Syntax);
            }
            tokens[count++]=t;
        }
        tokens[count++]={End,0,uint16_t(line),uint16_t(column),0};
        if(r.error!=Error::None){r.error_line=uint16_t(line);r.error_column=uint16_t(column);}
    }
};
struct Expr {unsigned lvalue{},name{};};
class Compiler {
  public:
    Runtime &r;Lexer lexer;unsigned pos{},depth{},block_depth{},function_depth{};
    struct Loop {unsigned breaks[64]{},continues[64]{},nb{},nc{},depth{};} loops[16]{};unsigned loop_count{},loop_floor{};
    Compiler(Runtime &v,const char *s,size_t n):r(v),lexer(v,s,n){}
    Token &t(){return lexer.tokens[pos<lexer.count?pos:lexer.count-1];}
    bool word(const char *s){return t().kind==Identifier && r.is(t().text,s);}
    bool accept(unsigned k){if(t().kind!=k)return false;++pos;return true;}
    bool need(unsigned k){if(accept(k))return true;r.fail(Error::Syntax);return false;}
    unsigned emit(Op op,unsigned a=0,unsigned b=0,unsigned flags=0){
        if(r.code_count==CodeLimit){r.fail(Error::Range);return 0;}
        unsigned i=r.code_count++;r.code[i]={op,uint8_t(flags),uint16_t(a),uint16_t(b),t().line,t().column,r.source_index};return i;
    }
    void patch(unsigned at,unsigned to){if(at>=r.code_count || to>=CodeLimit)r.fail(Error::Range);else r.code[at].a=uint16_t(to);}
    void literal(Value v){emit(Op::Constant,r.constant(v));}
    void semi(){if(accept(';')||t().kind=='}'||t().kind==End)return;r.fail(Error::Syntax);}
    bool nested(){if(++depth>ParseDepth){r.fail(Error::Range);--depth;return false;}return true;}
    static unsigned precedence(unsigned k){return k=='='||k==AddAssign||k==SubAssign||k==MulAssign||k==DivAssign?1:k=='?'?2:k==Or?3:k==And?4:k==Eq||k==Ne||k==StrictEq||k==StrictNe?5:k=='<'||k==Le||k=='>'||k==Ge?6:k=='+'||k=='-'?7:k=='*'||k=='/'||k=='%'?8:0;}
    bool async_context{};
    Work function(Expr &out,bool arrow=false,bool single=false,bool async=false){
        co_await WorkPoint{1};
        unsigned name=0;if(!arrow&&t().kind==Identifier){name=t().text;++pos;}
        if(r.function_count==FunctionLimit){r.fail(Error::Memory);co_return;}
        unsigned f=r.function_count++;auto &fn=r.functions[f];fn.arrow=arrow;fn.async=async;
        if(single){fn.params[fn.count++]=t().text;++pos;}else {need('(');if(!accept(')')){do{co_await WorkPoint{1};if(t().kind!=Identifier || fn.count==8){r.fail(Error::Syntax);co_return;}fn.params[fn.count++]=t().text;++pos;}while(accept(','));need(')');}}if(arrow)need(Arrow);
        unsigned skip=emit(Op::Jump);fn.entry=uint16_t(r.code_count);
        bool saved_async=async_context;async_context=async;unsigned saved_loops=loop_count,saved_blocks=block_depth,saved_floor=loop_floor;loop_floor=loop_count;block_depth=0;++function_depth;
        if(arrow&&t().kind!='{'){Expr body;co_await expression(body);emit(Op::Return);}else co_await block();literal({});emit(Op::Return);--function_depth;loop_count=saved_loops;loop_floor=saved_floor;block_depth=saved_blocks;
        async_context=saved_async;fn.end=uint16_t(r.code_count);patch(skip,r.code_count);emit(Op::Function,f);out={};(void)name;
    }
    Work primary(Expr &out){
        co_await WorkPoint{1};out={};bool constructing=word("new");if(constructing)++pos;unsigned k=t().kind;
        bool arrow=k==Identifier&&lexer.tokens[pos+1].kind==Arrow;
        if(k=='('){unsigned scan=pos+1;bool valid=true;if(lexer.tokens[scan].kind!=')'){while(scan<lexer.count){if(lexer.tokens[scan++].kind!=Identifier){valid=false;break;}if(lexer.tokens[scan].kind!=',')break;++scan;}}if(valid&&lexer.tokens[scan].kind==')'&&lexer.tokens[scan+1].kind==Arrow)arrow=true;}
        if(arrow){co_await function(out,true,k==Identifier);}
        else
        if(k==Number){literal(Value::num(t().number));++pos;}
        else if(k==StringToken){literal(Value::handle(Kind::String,t().text));++pos;}
        else if(word("true")||word("false")){literal(Value::boolean(word("true")));++pos;}
        else if(word("null")){literal(Value::handle(Kind::Null));++pos;}
        else if(word("undefined")){literal({});++pos;}
        else if(word("this")){emit(Op::This);++pos;}
        else if(word("function")){++pos;co_await function(out);}
        else if(word("async")){++pos;if(!word("function")){r.fail(Error::Syntax);co_return;}++pos;co_await function(out,false,false,true);}
        else if(k==Identifier){
            if(word("new")||word("eval")||word("delete")||word("class")||word("async")||word("await")||word("import")||word("throw")||word("try")||word("switch")){r.fail(Error::Syntax);co_return;}
            out={1,t().text};emit(Op::Load,t().text);++pos;
        }else if(accept('(')){co_await expression(out);need(')');}
        else if(accept('[')){
            unsigned count=0;if(!accept(']')){do{co_await WorkPoint{1};Expr e;co_await expression(e);if(++count>ArrayLength)r.fail(Error::Range);}while(r.error==Error::None && accept(','));need(']');}emit(Op::Array,count);
        }else if(accept('{')){
            emit(Op::Object);unsigned count=0;if(!accept('}')){do{co_await WorkPoint{1};if(t().kind!=Identifier&&t().kind!=StringToken){r.fail(Error::Syntax);break;}emit(Op::Dup);literal(Value::handle(Kind::String,t().text));++pos;need(':');Expr e;co_await expression(e);emit(Op::Set);emit(Op::Pop);if(++count>PropertyLimit)r.fail(Error::Range);}while(r.error==Error::None && accept(','));need('}');}
        }else {r.fail(Error::Syntax);co_return;}
        while(r.error==Error::None){co_await WorkPoint{1};
            if(accept('.')){if(t().kind!=Identifier){r.fail(Error::Syntax);break;}literal(Value::handle(Kind::String,t().text));++pos;emit(Op::Get);out={2,0};}
            else if(accept('[')){Expr key;co_await expression(key);need(']');emit(Op::Get);out={2,0};}
            else if(accept('(')){
                bool receiver=out.lvalue==2&&!constructing;if(receiver)r.code[r.code_count-1].op=Op::GetCall;
                unsigned count=0;if(!accept(')')){do{co_await WorkPoint{1};Expr arg;co_await expression(arg);if(++count>8)r.fail(Error::Range);}while(r.error==Error::None&&accept(','));need(')');}emit(constructing?Op::Construct:Op::Call,count,0,receiver?1:0);constructing=false;out={};
            }else if(t().kind==Inc || t().kind==Dec){
                unsigned op=t().kind;++pos;if(!out.lvalue){r.fail(Error::Syntax);break;}
                if(out.lvalue==1){emit(Op::Dup);literal(Value::num(1));emit(Op::Binary,op==Inc?'+':'-');emit(Op::Store,out.name);emit(Op::Pop);}
                else {--r.code_count;emit(Op::DupPair);emit(Op::Get);literal(Value::num(1));emit(Op::Binary,op==Inc?'+':'-');emit(Op::Set);literal(Value::num(1));emit(Op::Binary,op==Inc?'-':'+');}out={};
            }else break;
        }
        if(constructing)emit(Op::Construct,0);
    }
    Work expression(Expr &out,unsigned minimum=1){
        co_await WorkPoint{1};if(!nested())co_return;
        if(word("await")){if(!async_context){r.fail(Error::Syntax);--depth;co_return;}++pos;co_await expression(out,9);emit(Op::Await);out={};}
        else if(t().kind=='!'||t().kind=='-'||t().kind=='+'){
            unsigned op=t().kind;++pos;co_await expression(out,9);emit(Op::Unary,op);out={};
        }else co_await primary(out);
        while(r.error==Error::None){co_await WorkPoint{1};unsigned op=t().kind,p=precedence(op);if(!p||p<minimum)break;++pos;
            if(p==1){
                auto target=out;if(!target.lvalue){r.fail(Error::Syntax);break;}
                if(target.lvalue==2){--r.code_count;if(op!='='){emit(Op::DupPair);emit(Op::Get);}}
                else if(op=='=')--r.code_count;
                Expr rhs;co_await expression(rhs,p);
                if(op!='=')emit(Op::Binary,op==AddAssign?'+':op==SubAssign?'-':op==MulAssign?'*':'/');
                emit(target.lvalue==1?Op::Store:Op::Set,target.name);out={};
            }else if(op=='?'){
                unsigned no=emit(Op::JumpFalse);Expr e;co_await expression(e);need(':');unsigned end=emit(Op::Jump);patch(no,r.code_count);co_await expression(e,p);patch(end,r.code_count);out={};
            }else if(op==And || op==Or){
                emit(Op::Dup);unsigned end=emit(op==And?Op::JumpFalse:Op::JumpTrue);emit(Op::Pop);Expr rhs;co_await expression(rhs,p+1);patch(end,r.code_count);out={};
            }else {Expr rhs;co_await expression(rhs,p+1);emit(Op::Binary,op);out={};}
        }
        --depth;
    }
    Work declarations(bool semicolon=true){
        unsigned kind=word("var")?0:word("let")?1:2;++pos;
        do{co_await WorkPoint{1};if(t().kind!=Identifier){r.fail(Error::Syntax);co_return;}unsigned name=t().text;++pos;
            if(accept('=')){Expr e;co_await expression(e);}else {if(kind==2){r.fail(Error::Syntax);co_return;}literal({});}emit(Op::Declare,name,0,kind);
        }while(r.error==Error::None&&accept(','));if(semicolon)semi();
    }
    Work block(){
        co_await WorkPoint{1};if(!nested())co_return;
        if(!need('{')){--depth;co_return;}emit(Op::Enter);++block_depth;
        while(t().kind!='}'&&t().kind!=End&&r.error==Error::None){co_await WorkPoint{1};co_await statement();}
        need('}');emit(Op::Leave);--block_depth;--depth;
    }
    Work statement(){
        co_await WorkPoint{1};if(!nested())co_return;
        if(accept(';')){}
        else if(t().kind=='{')co_await block();
        else if(word("var")||word("let")||word("const"))co_await declarations();
        else if(word("function")||word("async")){
            bool async=word("async");if(async){++pos;if(!word("function")){r.fail(Error::Syntax);--depth;co_return;}}
            ++pos;if(t().kind!=Identifier){r.fail(Error::Syntax);--depth;co_return;}unsigned name=t().text;Expr e;co_await function(e,false,false,async);emit(Op::Declare,name,0,0);
        }else if(word("throw")){
            ++pos;Expr e;co_await expression(e);emit(Op::Throw);semi();
        }else if(word("try")){
            ++pos;if(r.handler_code_count==HandlerLimit){r.fail(Error::Range);--depth;co_return;}
            unsigned id=r.handler_code_count++;emit(Op::Try,id);r.handler_code[id].begin=uint16_t(r.code_count);
            co_await block();emit(Op::EndTry);bool caught=false,finalizer=false;
            if(word("catch")){caught=true;++pos;need('(');if(t().kind!=Identifier){r.fail(Error::Syntax);--depth;co_return;}unsigned name=t().text;++pos;need(')');r.handler_code[id].caught=uint16_t(r.code_count);emit(Op::Enter);++block_depth;emit(Op::Catch,name);co_await block();emit(Op::Leave);--block_depth;emit(Op::EndTry);}
            if(word("finally")){finalizer=true;++pos;r.handler_code[id].finalizer=uint16_t(r.code_count);co_await block();emit(Op::EndFinally);}
            if(!caught&&!finalizer){r.fail(Error::Syntax);}r.handler_code[id].end=uint16_t(r.code_count);
        }else if(word("if")){
            ++pos;need('(');Expr e;co_await expression(e);need(')');unsigned no=emit(Op::JumpFalse);co_await statement();
            if(word("else")){++pos;unsigned end=emit(Op::Jump);patch(no,r.code_count);co_await statement();patch(end,r.code_count);}else patch(no,r.code_count);
        }else if(word("while")||word("for")){
            bool is_for=word("for");++pos;need('(');emit(Op::Enter);++block_depth;
            if(loop_count==16){r.fail(Error::Range);--depth;co_return;}
            unsigned loop=loop_count++;loops[loop]={};loops[loop].depth=block_depth;
            if(is_for){if(word("var")||word("let")||word("const"))co_await declarations(false);else if(t().kind!=';'){Expr e;co_await expression(e);emit(Op::Pop);}need(';');}
            unsigned condition=r.code_count;
            if(is_for&&t().kind==';')literal(Value::boolean(true));else {Expr e;co_await expression(e);}
            unsigned end=emit(Op::JumpFalse),repeat=condition;
            if(is_for){need(';');unsigned body=emit(Op::Jump);repeat=r.code_count;
                if(t().kind!=')'){Expr e;co_await expression(e);emit(Op::Pop);}emit(Op::Jump,condition);patch(body,r.code_count);
            }
            need(')');co_await statement();emit(Op::Jump,repeat);patch(end,r.code_count);
            auto &l=loops[loop];for(unsigned i=0;i<l.nb;++i){co_await WorkPoint{1};patch(l.breaks[i],r.code_count);}for(unsigned i=0;i<l.nc;++i){co_await WorkPoint{1};patch(l.continues[i],repeat);}
            --loop_count;emit(Op::Leave);--block_depth;
        }else if(word("break")||word("continue")){
            bool br=word("break");++pos;if(loop_count==loop_floor){r.fail(Error::Syntax);--depth;co_return;}auto &l=loops[loop_count-1];
            unsigned jump=emit(Op::AbruptJump,0,block_depth-l.depth);if(br){if(l.nb==64)r.fail(Error::Range);else l.breaks[l.nb++]=jump;}else {if(l.nc==64)r.fail(Error::Range);else l.continues[l.nc++]=jump;}semi();
        }else if(word("return")){
            ++pos;if(!function_depth)r.fail(Error::Syntax);
            if(t().kind==';'||t().kind=='}'||t().kind==End)literal({});else {Expr e;co_await expression(e);}emit(Op::Return);semi();
        }else {Expr e;co_await expression(e);emit(Op::Pop);semi();}
        --depth;
    }
    Work compile(){
        co_await lexer.scan();if(r.error!=Error::None)co_return;
        while(t().kind!=End && r.error==Error::None){co_await WorkPoint{1};co_await statement();}
        emit(Op::Halt);if(r.error!=Error::None){r.error_line=t().line;r.error_column=t().column;}
    }
};
inline Work Runtime::compile_and_run(const char *source,size_t n){
    if(!core_initialized)co_await initialize_core();
    struct Owner {Compiler *p{};~Owner(){delete p;}} owner;
    owner.p=new Compiler(*this,source,n);if(!owner.p){fail(Error::Memory);co_return;}
    error_source=source_index;unsigned begin=code_count,handler_begin=handler_code_count;compiling=true;co_await owner.p->compile();compiling=false;
    if(error!=Error::None){last_error_line=error_line;last_error_column=error_column;co_return;}
    for(unsigned i=begin;i<code_count;++i){co_await WorkPoint{1};auto c=code[i];
        if(unsigned(c.op)>unsigned(Op::Halt)||(c.op==Op::Constant && c.a>=constant_count)||((c.op==Op::Jump||c.op==Op::JumpFalse||c.op==Op::JumpTrue||c.op==Op::AbruptJump)&&(c.a<begin||c.a>=code_count))||(c.op==Op::Function && c.a>=function_count)){fail(Error::Range);co_return;}
    }
    for(unsigned i=handler_begin;i<handler_code_count;++i){co_await WorkPoint{1};const auto &h=handler_code[i];if(!h.begin||h.end<=h.begin||h.end>=code_count||(h.caught&&(h.caught<h.begin||h.caught>=h.end))||(h.finalizer&&(h.finalizer<h.begin||h.finalizer>=h.end))){fail(Error::Range);co_return;}}
    co_await run(begin);if(error==Error::None)co_await microtask_checkpoint();
}
inline bool Runtime::start(const char *source,size_t n){
    if(task.handle || cancelled || !source || n>limits.source_bytes || n>SourceLimit)return false;
    error=Error::None;budget_exceeded=false;error_line=error_column=0;budget=ScriptBudget(limits);driver={};driver.allow_clock=false;
    // The caller owns source until task completion. Browser uses pinned resources.
    task=compile_and_run(source,n);if(!task.handle){fail(Error::Memory);return false;}return true;
}
inline ScriptResult Runtime::execute(ScriptDom &dom,const char *source,size_t n,ScriptBudget &given){document=&dom.document();generation=document->generation;bool ok=start(source,n);if(ok)budget=given;return ok?ScriptResult::Suspended:ScriptResult::Error;}
enum Native : unsigned { NativeNone=0, Push=1, Pop, CharAt, Slice, IndexOf, Lower, Upper,
    GetId=20, Query, QueryAll, CreateElement, CreateText, GetAttribute, SetAttribute, RemoveAttribute,
    Append, Remove, Insert, Replace, ClassAdd, ClassRemove, ClassContains, ClassToggle,
    AddListener, RemoveListener, Prevent, StopPropagation, Timeout, ClearTimeout, Interval, Log, Warn, ConsoleError,
    ObjectCtor=100, ObjectCreate, ObjectGetPrototype, ObjectSetPrototype, NumberCtor, NumberNaN, NumberFinite, GlobalNaN, GlobalFinite, ArrayCtor, StringCtor, FunctionCtor, ErrorCtor, TypeErrorCtor, ReferenceErrorCtor, RangeErrorCtor, SyntaxErrorCtor, PromiseCtor=200, PromiseResolve, PromiseReject, PromiseThen, PromiseCatch, ResolvePromise, RejectPromise, JsonBuiltin, JsonParse, JsonStringify, Fetch=300, ResponseText, ResponseJson, StorageKeyAt, StorageGet, StorageSet, StorageRemove, StorageClear };
inline bool Runtime::native_constructor(unsigned id)const{return id==ObjectCtor||id==ArrayCtor||id==NumberCtor||id==StringCtor||id==PromiseCtor||(id>=ErrorCtor&&id<=SyntaxErrorCtor);}
inline Value method(unsigned id,Value receiver={}){auto v=Value::handle(Kind::Native,id,(unsigned(receiver.kind)<<16)|receiver.ref);if(receiver.kind==Kind::Event)v.number=Numeric(receiver.aux);return v;}
inline Work Runtime::initialize_core(){
    object_prototype=object(Value::handle(Kind::Null));array_prototype=object(object_prototype);function_prototype=object(object_prototype);string_prototype=object(object_prototype);number_prototype=object(object_prototype);
    struct Global {const char *name;Value value;};
    Global globals[]={{"Object",method(ObjectCtor)},{"Array",method(ArrayCtor)},{"String",method(StringCtor)},{"Number",method(NumberCtor)},{"Function",method(FunctionCtor)},{"NaN",Value::num(Numeric::nan())},{"Infinity",Value::num(Numeric::infinity())},{"isNaN",method(GlobalNaN)},{"isFinite",method(GlobalFinite)},{"JSON",method(JsonBuiltin)},{"Promise",method(PromiseCtor)},{"Error",method(ErrorCtor)},{"TypeError",method(TypeErrorCtor)},{"ReferenceError",method(ReferenceErrorCtor)},{"RangeError",method(RangeErrorCtor)},{"SyntaxError",method(SyntaxErrorCtor)}};
    for(auto &g:globals){co_await point();unsigned name=string(g.name,length(g.name,32));co_await binding(name,true,true,true,g.value);}
    Value roots[]={object_prototype,array_prototype,function_prototype,string_prototype,number_prototype};unsigned constructors[]={ObjectCtor,ArrayCtor,FunctionCtor,StringCtor,NumberCtor};
    for(unsigned i=0;i<5;++i){Value key=Value::handle(Kind::String,string("constructor",11)),v=method(constructors[i]);co_await property(roots[i],key,true,v);}
    const char *array_names[]={"push","pop"};unsigned array_ids[]={Push,Pop};
    for(unsigned i=0;i<2;++i){Value key=Value::handle(Kind::String,string(array_names[i],length(array_names[i],32))),v=method(array_ids[i]);co_await property(array_prototype,key,true,v);}
    const char *string_names[]={"charAt","slice","substring","indexOf","toLowerCase","toUpperCase"};unsigned string_ids[]={CharAt,Slice,Slice,IndexOf,Lower,Upper};
    for(unsigned i=0;i<6;++i){Value key=Value::handle(Kind::String,string(string_names[i],length(string_names[i],32))),v=method(string_ids[i]);co_await property(string_prototype,key,true,v);}
    core_initialized=true;
}
inline Work Runtime::property(Value base,Value key,bool write,Value &value){
    co_await point();
    unsigned name=0;if(key.kind==Kind::String)name=key.ref;else if(key.kind==Kind::Number){co_await convert_string(key,name);}else {fail(Error::Type);co_return;}
    if(size(name)>64){fail(Error::Range);co_return;}
    if(base.kind==Kind::Promise){if(write||base.ref>=promise_count){fail(Error::Type);co_return;}value=is(name,"then")?method(PromiseThen,base):is(name,"catch")?method(PromiseCatch,base):Value{};co_return;}
    if(base.kind==Kind::Native){
        if(base.ref==JsonBuiltin&&!write){value=is(name,"parse")?method(JsonParse):is(name,"stringify")?method(JsonStringify):Value{};co_return;}
        if(base.ref==PromiseCtor&&!write){value=is(name,"resolve")?method(PromiseResolve):is(name,"reject")?method(PromiseReject):Value{};co_return;}
        if(write){fail(Error::Type);co_return;}
        if(is(name,"prototype")){value=base.ref==ObjectCtor?object_prototype:base.ref==ArrayCtor?array_prototype:base.ref==FunctionCtor?function_prototype:base.ref==StringCtor?string_prototype:base.ref==NumberCtor?number_prototype:Value{};co_return;}
        unsigned id=base.ref==ObjectCtor?(is(name,"create")?ObjectCreate:is(name,"getPrototypeOf")?ObjectGetPrototype:is(name,"setPrototypeOf")?ObjectSetPrototype:NativeNone):base.ref==NumberCtor?(is(name,"isNaN")?NumberNaN:is(name,"isFinite")?NumberFinite:NativeNone):NativeNone;
        if(id){value=method(id);co_return;}
        if(base.ref==NumberCtor){if(is(name,"NaN"))value=Value::num(Numeric::nan());else if(is(name,"POSITIVE_INFINITY"))value=Value::num(Numeric::infinity());else if(is(name,"NEGATIVE_INFINITY"))value=Value::num(-Numeric::infinity());else if(is(name,"EPSILON"))value=Value::num(Numeric::bits(0x3cb0000000000000ULL));else if(is(name,"MAX_VALUE"))value=Value::num(Numeric::bits(0x7fefffffffffffffULL));else if(is(name,"MIN_VALUE"))value=Value::num(Numeric::bits(1));else if(is(name,"MAX_SAFE_INTEGER"))value=Value::num(9007199254740991.0);else if(is(name,"MIN_SAFE_INTEGER"))value=Value::num(-9007199254740991.0);else value={};co_return;}
        value={};co_return;
    }
    if(base.kind==Kind::Object||base.kind==Kind::Function){
        if((base.kind==Kind::Object&&base.ref>=object_count)||(base.kind==Kind::Function&&base.ref>=closure_count)){fail(Error::Type);co_return;}
        Value original=base;
        unsigned own=base.kind==Kind::Function?closures[base.ref].object:base.ref;
        Value cursor=Value::handle(Kind::Object,own);
        for(unsigned depth=0;depth<PrototypeDepth;++depth){
            if(cursor.kind==Kind::Null){value={};co_return;}
            if(cursor.kind!=Kind::Object||cursor.ref>=object_count){fail(Error::Type);co_return;}auto &o=objects[cursor.ref];
            for(unsigned i=0;i<o.count;++i){co_await point();if(equal(text(o.props[i].name),text(name))){if(write)o.props[i].value=value;else {value=o.props[i].value;if(value.kind==Kind::Native)value=method(value.ref,original);}co_return;}}
            if(write){if(o.count==PropertyLimit)fail(Error::Range);else o.props[o.count++]={uint16_t(name),value};co_return;}
            cursor=o.prototype;
        }fail(Error::Range);co_return;
    }
    if(base.kind==Kind::Array){
        if(base.ref>=array_count){fail(Error::Type);co_return;}auto &a=arrays[base.ref];
        if(is(name,"length")){if(write)fail(Error::Type);else value=Value::num(a.count);co_return;}

        unsigned index=0;bool valid=size(name)>0;
        for(unsigned i=0;i<size(name);++i){co_await point();char c=text(name)[i];if(c<'0'||c>'9'||index>=ArrayLength){valid=false;break;}index=index*10+unsigned(c-'0');}
        if(!valid||index>=ArrayLength){if(write)fail(Error::Range);else {co_await property(array_prototype,key,false,value);if(value.kind==Kind::Native)value=method(value.ref,base);}co_return;}
        if(write){a.values[index]=value;if(index>=a.count)a.count=uint16_t(index+1);}else value=index<a.count?a.values[index]:Value{};co_return;
    }
    if(base.kind==Kind::String){
        if(write){fail(Error::Type);co_return;}
        if(is(name,"length")){unsigned count=0;for(unsigned i=0;i<size(base.ref);){co_await point();size_t used=0;utf8(text(base.ref)+i,size(base.ref)-i,used);i+=unsigned(used);++count;}value=Value::num(count);co_return;}
        co_await property(string_prototype,key,false,value);if(value.kind==Kind::Native)value=method(value.ref,base);co_return;
    }
    if(base.kind==Kind::Number){if(write)fail(Error::Type);else co_await property(number_prototype,key,false,value);co_return;}
    fail(Error::Type);
}
inline Work Runtime::native(Value fn,Value *args,unsigned count,Value &out){
    co_await point();Value receiver=Value::handle(Kind(fn.aux>>16),fn.aux&65535);
    if(fn.ref==JsonParse||fn.ref==JsonStringify){if(count!=1)fail(Error::Type);else co_await json(args[0],fn.ref==JsonStringify,out);co_return;}
    if(fn.ref==PromiseCtor){
        if(count!=1||!callable(args[0])){fail(Error::Type);co_return;}out=promise();if(error!=Error::None)co_return;
        native_call=args[0];native_args[0]=Value::handle(Kind::Native,ResolvePromise,out.ref);native_args[1]=Value::handle(Kind::Native,RejectPromise,out.ref);native_count=2;native_mode=1;co_return;
    }
    if(fn.ref==ResolvePromise||fn.ref==RejectPromise){if(count>1){fail(Error::Type);co_return;}co_await settle(Value::handle(Kind::Promise,fn.aux),count?args[0]:Value{},fn.ref==RejectPromise);co_return;}
    if(fn.ref==PromiseResolve||fn.ref==PromiseReject){if(count>1){fail(Error::Type);co_return;}Value v=count?args[0]:Value{};if(fn.ref==PromiseResolve&&v.kind==Kind::Promise){out=v;co_return;}out=promise();if(error==Error::None)co_await settle(out,v,fn.ref==PromiseReject);co_return;}
    if(fn.ref==PromiseThen||fn.ref==PromiseCatch){
        if(receiver.kind!=Kind::Promise||receiver.ref>=promise_count||count>2){fail(Error::Type);co_return;}out=promise();if(error==Error::None)co_await react(receiver,fn.ref==PromiseThen&&count?args[0]:Value{},fn.ref==PromiseCatch&&count?args[0]:count==2?args[1]:Value{},out);co_return;
    }
    if(fn.ref>=ErrorCtor&&fn.ref<=SyntaxErrorCtor){
        if(count>1){fail(Error::Type);co_return;}out=error_object(fn.ref==ReferenceErrorCtor?Error::Reference:fn.ref==RangeErrorCtor?Error::Range:fn.ref==SyntaxErrorCtor?Error::Syntax:Error::Type);
        if(error!=Error::None){co_return;}auto &o=objects[out.ref];if(fn.ref==ErrorCtor)o.props[0].value=Value::handle(Kind::String,string("Error",5));if(count){unsigned message=0;co_await convert_string(args[0],message);o.props[1].value=Value::handle(Kind::String,message);}co_return;
    }
    if(fn.ref==ObjectCtor){if(count>1){fail(Error::Type);co_return;}out=count&&object_like(args[0])?args[0]:object(object_prototype);co_return;}
    if(fn.ref==ObjectCreate){if(count!=1||(args[0].kind!=Kind::Object&&args[0].kind!=Kind::Null)){fail(Error::Type);co_return;}out=object(args[0]);co_return;}
    if(fn.ref==ObjectGetPrototype){if(count!=1||!object_like(args[0]))fail(Error::Type);else out=prototype(args[0]);co_return;}
    if(fn.ref==ObjectSetPrototype){
        if(count!=2||args[0].kind!=Kind::Object||args[0].ref>=object_count||(args[1].kind!=Kind::Object&&args[1].kind!=Kind::Null)){fail(Error::Type);co_return;}
        Value p=args[1];unsigned depth=0;
        while(p.kind!=Kind::Null){co_await point();if(p.kind!=Kind::Object||p.ref>=object_count||same(p,args[0])||++depth>=PrototypeDepth){fail(Error::Type);co_return;}p=prototype(p);}
        // Validate affected descendants prospectively; publish only after all walks.
        for(unsigned i=0;i<object_count;++i){Value cursor=Value::handle(Kind::Object,i);unsigned hops=0;while(cursor.kind!=Kind::Null){co_await point();if(cursor.kind!=Kind::Object||cursor.ref>=object_count||++hops>PrototypeDepth){fail(Error::Type);co_return;}cursor=same(cursor,args[0])?args[1]:prototype(cursor);}}
        objects[args[0].ref].prototype=args[1];out=args[0];co_return;
    }
    if(fn.ref==NumberCtor||fn.ref==NumberNaN||fn.ref==NumberFinite||fn.ref==GlobalNaN||fn.ref==GlobalFinite){
        if(count>1){fail(Error::Type);co_return;}Value a=count?args[0]:Value{};Numeric n=Numeric::nan();
        if(a.kind==Kind::Number||a.kind==Kind::Boolean)n=a.number;
        else if(a.kind==Kind::Null||(!count&&fn.ref==NumberCtor))n=0;
        else if(a.kind==Kind::String&&(fn.ref==NumberCtor||fn.ref==GlobalNaN||fn.ref==GlobalFinite)){
            unsigned begin=0,end=size(a.ref);while(begin<end&&space(text(a.ref)[begin])){co_await point();++begin;}while(end>begin&&space(text(a.ref)[end-1])){co_await point();--end;}bool ok=false;n=begin==end?Numeric(0):numeric_parse(text(a.ref)+begin,end-begin,ok);
        }
        out=fn.ref==NumberCtor?Value::num(n):Value::boolean((fn.ref==NumberNaN)?a.kind==Kind::Number&&n!=n:fn.ref==NumberFinite?a.kind==Kind::Number&&n.finite():fn.ref==GlobalNaN?n!=n:n.finite());co_return;
    }
    if(fn.ref==StringCtor){if(count>1){fail(Error::Type);co_return;}unsigned s=0;if(count)co_await convert_string(args[0],s);out=Value::handle(Kind::String,s);co_return;}
    if(fn.ref==ArrayCtor){if(array_count==ArrayLimit||count>ArrayLength){fail(Error::Range);co_return;}auto &a=arrays[array_count];for(unsigned i=0;i<count;++i)a.values[i]=args[i];a.count=uint16_t(count);out=Value::handle(Kind::Array,array_count++);co_return;}
    if(fn.ref==FunctionCtor){fail(Error::Type);co_return;}
    if(fn.ref==Push || fn.ref==Pop){
        if(receiver.kind!=Kind::Array || receiver.ref>=array_count){fail(Error::Type);co_return;}auto &a=arrays[receiver.ref];
        if(fn.ref==Push){if(count>ArrayLength-a.count){fail(Error::Range);co_return;}for(unsigned i=0;i<count;++i){co_await point();a.values[a.count++]=args[i];}out=Value::num(a.count);}
        else {if(count){fail(Error::Type);co_return;}out=a.count?a.values[--a.count]:Value{};if(a.count<ArrayLength)a.values[a.count]={};}co_return;
    }
    if(receiver.kind!=Kind::String || receiver.ref>=string_count){fail(Error::Type);co_return;}
    if(fn.ref==CharAt || fn.ref==Slice){
        if(count<1 || count>2 || args[0].kind!=Kind::Number || (count==2&&args[1].kind!=Kind::Number)){fail(Error::Type);co_return;}
        Numeric a=args[0].number,b=fn.ref==CharAt?a+1:count==2?args[1].number:Numeric(StringLimit);
        if(a<0){a=0;}if(b<a){b=a;}if(a>StringLimit || a!=a || b!=b){out=Value::handle(Kind::String,string("",0));co_return;}
        unsigned start=0,end=0,cp=0;
        for(unsigned i=0;i<size(receiver.ref);){co_await point();if(Numeric(cp)<a)start=i;size_t used=0;utf8(text(receiver.ref)+i,size(receiver.ref)-i,used);i+=unsigned(used);if(Numeric(cp)<a)start=i;if(Numeric(cp)<b)end=i;++cp;}
        if(end<start){end=start;}out=Value::handle(Kind::String,string(text(receiver.ref)+start,end-start));co_return;
    }
    if(fn.ref==IndexOf){
        if(count!=1||args[0].kind!=Kind::String){fail(Error::Type);co_return;}unsigned n=size(receiver.ref),m=size(args[0].ref),cp=0;
        for(unsigned i=0;i<=n;){co_await point();bool found=m<=n-i;
            for(unsigned j=0;found&&j<m;++j){co_await point();if(text(receiver.ref)[i+j]!=text(args[0].ref)[j])found=false;}
            if(found){out=Value::num(cp);co_return;}if(i==n)break;size_t used=0;utf8(text(receiver.ref)+i,n-i,used);i+=unsigned(used);++cp;
        }out=Value::num(-1);co_return;
    }
    if(fn.ref==Lower || fn.ref==Upper){
        if(count){fail(Error::Type);co_return;}struct Buffer{char *p{};~Buffer(){delete[] p;}} buffer;buffer.p=new char[StringLimit+1];if(!buffer.p){fail(Error::Memory);co_return;}
        unsigned n=0;for(unsigned i=0;i<size(receiver.ref);){co_await point();size_t used=0;uint32_t cp=utf8(text(receiver.ref)+i,size(receiver.ref)-i,used);i+=unsigned(used);
            if(fn.ref==Lower){if(cp>='A'&&cp<='Z')cp+=32;else if(cp==0x104||cp==0x106||cp==0x118||cp==0x141||cp==0x143||cp==0x15a||cp==0x179||cp==0x17b)++cp;else if(cp==0xd3)cp=0xf3;}
            else {if(cp>='a'&&cp<='z')cp-=32;else if(cp==0x105||cp==0x107||cp==0x119||cp==0x142||cp==0x144||cp==0x15b||cp==0x17a||cp==0x17c)--cp;else if(cp==0xf3)cp=0xd3;}
            char encoded[4];unsigned k=encode(cp,encoded);if(k>StringLimit-n){fail(Error::Range);co_return;}for(unsigned j=0;j<k;++j)buffer.p[n++]=encoded[j];
        }out=Value::handle(Kind::String,string(buffer.p,n));co_return;
    }
    fail(Error::Type);
}
inline Work Runtime::react(Value source,Value fulfilled,Value rejected,Value child,unsigned mode){
    if(source.kind!=Kind::Promise||source.ref>=promise_count||child.kind!=Kind::Promise||(mode!=2&&child.ref>=promise_count)){fail(Error::Type);co_return;}
    if(reaction_count==ReactionLimit){fail(Error::Memory);co_return;}
    unsigned r=reaction_count++;reactions[r]={fulfilled,rejected,child.ref,0,false,uint8_t(mode)};
    auto &p=promises[source.ref];if(p.last)reactions[p.last-1].next=uint16_t(r+1);else p.first=uint16_t(r+1);p.last=uint16_t(r+1);
    if(p.state!=PromiseState::Pending){reactions[r].queued=enqueue({uint16_t(r),source.ref});}
    co_return;
}
inline Work Runtime::settle(Value promise_value,Value value,bool rejected){
    co_await point();if(promise_value.kind!=Kind::Promise||promise_value.ref>=promise_count){fail(Error::Type);co_return;}
    auto &p=promises[promise_value.ref];if(p.state!=PromiseState::Pending||p.resolving)co_return;
    if(!rejected&&value.kind==Kind::Promise){
        if(value.ref>=promise_count){fail(Error::Type);co_return;}
        unsigned cursor=value.ref;for(unsigned depth=0;;++depth){co_await point();if(cursor==promise_value.ref||depth==32){value=error_object(Error::Type);rejected=true;break;}auto &a=promises[cursor];if(!a.adopting)break;cursor=a.adopting-1;}
        if(!rejected){p.adopting=uint16_t(value.ref+1);p.resolving=true;co_await react(value,{},{},promise_value,1);co_return;}
    }
    p.state=rejected?PromiseState::Rejected:PromiseState::Fulfilled;p.result=value;p.adopting=0;
    for(unsigned r=p.first;r;r=reactions[r-1].next){co_await point();auto &reaction=reactions[r-1];if(!reaction.queued)reaction.queued=enqueue({uint16_t(r-1),promise_value.ref});if(error!=Error::None)co_return;}
}
inline Work Runtime::invoke_job(Value fn,Value arg,bool has_arg){
    if(!job_initialized){
        if(code_count+5>=CodeLimit){fail(Error::Memory);co_return;}job_ip=code_count;job_constant=constant({});constant({});
        code[code_count++]={Op::Constant,0,uint16_t(job_constant)};code[code_count++]={Op::Constant,0,uint16_t(job_constant+1)};
        code[code_count++]={Op::Call,0,1};code[code_count++]={Op::Pop};code[code_count++]={Op::Halt};job_initialized=true;
    }
    constants[job_constant]=fn;constants[job_constant+1]=arg;code[job_ip+2].a=uint16_t(has_arg?1:0);
    code[job_ip+1].op=has_arg?Op::Constant:Op::Jump;code[job_ip+1].a=uint16_t(has_arg?job_constant+1:job_ip+2);
    reaction_job=true;co_await run(job_ip);reaction_job=false;
}
inline Work Runtime::microtask_checkpoint(unsigned maximum){
    Value saved=result;unsigned processed=0;
    while(microtask_count&&processed++<maximum&&alive()&&error==Error::None){
        co_await point();if(budget_exceeded)co_return;auto job=microtasks[microtask_head];microtask_head=(microtask_head+1)%MicrotaskLimit;--microtask_count;++microtasks_run;
        if(job.resume){co_await resume_continuation(job.continuation);continue;}
        if(job.reaction>=reaction_count||job.source>=promise_count){fail(Error::Range);co_return;}
        auto reaction=reactions[job.reaction];auto source=promises[job.source];Value child=Value::handle(Kind::Promise,reaction.child);
        if(reaction.mode==1){promises[child.ref].resolving=false;co_await settle(child,source.result,source.state==PromiseState::Rejected);continue;}
        if(reaction.mode==2){unsigned id=reaction.child;if(id>=ContinuationLimit||!continuations[id]){fail(Error::Range);co_return;}auto &c=*continuations[id];c.settled=source.result;c.rejected=source.state==PromiseState::Rejected;enqueue({0,0,uint16_t(id),true});continue;}
        Value fn=source.state==PromiseState::Fulfilled?reaction.fulfilled:reaction.rejected;
        if(!callable(fn)){co_await settle(child,source.result,source.state==PromiseState::Rejected);continue;}
        co_await invoke_job(fn,source.result);if(error==Error::None)co_await settle(child,result);
        else if(unhandled){error=Error::None;unhandled=false;co_await settle(child,thrown,true);}else co_return;
    }
    result=saved;
}

inline Work Runtime::suspend_await(Value awaited){
    unsigned boundary=frame_count;
    while(boundary&&frames[boundary-1].mode!=2&&frames[boundary-1].mode!=3)--boundary;
    if(!boundary){fail(Error::Syntax);co_return;}--boundary;
    unsigned slot=0;while(slot<ContinuationLimit&&continuations[slot])++slot;
    if(slot==ContinuationLimit){fail(Error::Memory);co_return;}
    auto *c=new Continuation;if(!c){fail(Error::Memory);co_return;}continuations[slot]=c;
    auto caller=frames[boundary];unsigned base=caller.base;c->sp=uint16_t(sp-base);c->frame_count=uint16_t(frame_count-boundary);c->scope=uint16_t(scope);c->ip=uint16_t(ip);c->receiver=this_value;c->promise=caller.promise;c->active=true;
    for(unsigned i=0;i<c->sp;++i){co_await point();c->stack[i]=stack[base+i];}
    for(unsigned i=0;i<c->frame_count;++i){co_await point();c->frames[i]=frames[boundary+i];c->frames[i].base=uint16_t(c->frames[i].base-base);budget.leave();}
    c->frames[0].ip=c->frames[0].scope=c->frames[0].base=0;c->frames[0].receiver={};c->frames[0].mode=3;
    unsigned kept=0;for(unsigned i=0;i<handler_count;++i){co_await point();if(handlers[i].frame>boundary){c->handlers[c->handler_count]=handlers[i];c->handlers[c->handler_count++].frame=uint16_t(handlers[i].frame-boundary);}else handlers[kept++]=handlers[i];}handler_count=kept;
    // Suspended and captured environments use the same bounded document arena.
    for(unsigned s=scope;;s=scopes[s].parent){co_await point();scopes[s].retained=true;if(!s)break;}
    scope=caller.scope;ip=caller.ip;sp=caller.base;this_value=caller.receiver;frame_count=boundary;
    if(caller.mode==3)running=false;else push(Value::handle(Kind::Promise,caller.executor_promise!=UINT16_MAX?caller.executor_promise:caller.promise));
    Value source=awaited;
    if(source.kind!=Kind::Promise){source=promise();if(error==Error::None)co_await settle(source,awaited);}
    if(error==Error::None)co_await react(source,{},{},Value::handle(Kind::Promise,slot),2);
}
inline Work Runtime::resume_continuation(unsigned id){
    if(id>=ContinuationLimit||!continuations[id]||!alive()){fail(Error::Cancelled);co_return;}
    struct Owner{Continuation *p{};~Owner(){delete p;}} owner{continuations[id]};auto *c=owner.p;continuations[id]=nullptr;
    sp=c->sp;frame_count=c->frame_count;handler_count=c->handler_count;scope=c->scope;ip=c->ip;this_value=c->receiver;running=true;
    for(unsigned i=0;i<sp;++i){co_await point();stack[i]=c->stack[i];}
    for(unsigned i=0;i<frame_count;++i){co_await point();frames[i]=c->frames[i];if(!budget.enter())fail(Error::Range);}
    for(unsigned i=0;i<handler_count;++i){co_await point();handlers[i]=c->handlers[i];}
    Value settled=c->settled;bool rejected=c->rejected;delete owner.p;owner.p=nullptr;
    if(rejected)co_await complete({CompletionKind::Throw,settled});else push(settled);
    if(running&&error==Error::None)co_await run(ip,true);
}

} } // namespace Husarz::Js

#include "js_json.h"
