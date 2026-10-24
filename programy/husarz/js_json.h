#pragma once
namespace Husarz {namespace Js {
constexpr unsigned JsonSourceLimit=4096,JsonDepth=16,JsonValues=512;
struct JsonCodec {
    Runtime &r;const char *source{};unsigned length{},pos{},nodes{},used{};char buffer[StringLimit+1]{};Value path[JsonDepth]{};
    explicit JsonCodec(Runtime &vm):r(vm){}
    void skip(){while(pos<length&&(source[pos]==' '||source[pos]=='\n'||source[pos]=='\r'||source[pos]=='\t'))++pos;}
    bool put(char c){if(used==StringLimit){r.fail(Error::Range);return false;}buffer[used++]=c;return true;}
    Work quoted(unsigned &out){
        if(pos>=length||source[pos++]!='"'){r.fail(Error::Syntax);co_return;}unsigned start=used;bool closed=false;
        while(pos<length){co_await r.point();unsigned char c=source[pos++];if(c=='"'){closed=true;break;}if(c<32){r.fail(Error::Syntax);break;}
            if(c=='\\'){if(pos==length){r.fail(Error::Syntax);break;}c=source[pos++];
                if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c=='b')c='\b';else if(c=='f')c='\f';
                else if(c=='u'){
                    uint32_t cp=0;for(unsigned j=0;j<4;++j){if(pos>=length){r.fail(Error::Syntax);co_return;}char h=source[pos++];unsigned d=h>='0'&&h<='9'?unsigned(h-'0'):h>='a'&&h<='f'?unsigned(h-'a'+10):h>='A'&&h<='F'?unsigned(h-'A'+10):16;if(d==16){r.fail(Error::Syntax);co_return;}cp=cp*16+d;}
                    if(cp>=0xd800&&cp<=0xdbff){
                        if(length-pos<6||source[pos]!='\\'||source[pos+1]!='u'){r.fail(Error::Syntax);co_return;}pos+=2;uint32_t low=0;
                        for(unsigned j=0;j<4;++j){char h=source[pos++];unsigned d=h>='0'&&h<='9'?unsigned(h-'0'):h>='a'&&h<='f'?unsigned(h-'a'+10):h>='A'&&h<='F'?unsigned(h-'A'+10):16;if(d==16){r.fail(Error::Syntax);co_return;}low=low*16+d;}
                        if(low<0xdc00||low>0xdfff){r.fail(Error::Syntax);co_return;}cp=0x10000+(cp-0xd800)*1024+low-0xdc00;
                    }
                    // Embedded NUL is outside the browser's current string ABI.
                    if(!cp||(cp>=0xdc00&&cp<=0xdfff)){r.fail(Error::Syntax);co_return;}char encoded[4];unsigned n=encode(cp,encoded);for(unsigned j=0;j<n;++j)put(encoded[j]);continue;
                }else if(c!='"'&&c!='\\'&&c!='/'){r.fail(Error::Syntax);break;}
            }if(!put(char(c)))break;
        }
        if(!closed){r.fail(Error::Syntax);co_return;}out=r.string(buffer+start,used-start);used=start;
    }
    Work parse(Value &out,unsigned depth=0){
        co_await r.point();if(depth==JsonDepth||++nodes>JsonValues){r.fail(Error::Range);co_return;}skip();if(pos==length){r.fail(Error::Syntax);co_return;}char c=source[pos];
        if(c=='"'){unsigned s=0;co_await quoted(s);out=Value::handle(Kind::String,s);co_return;}
        if(c=='{'||c=='['){
            bool array=c=='[';++pos;skip();if(array){if(r.array_count==ArrayLimit){r.fail(Error::Memory);co_return;}out=Value::handle(Kind::Array,r.array_count++);}else out=r.object(r.object_prototype);
            char end=array?']':'}';if(pos<length&&source[pos]==end){++pos;co_return;}
            unsigned count=0;
            for(;;){co_await r.point();unsigned key=0;if(!array){skip();co_await quoted(key);skip();if(pos==length||source[pos++]!=':'){r.fail(Error::Syntax);co_return;}}
                Value v;co_await parse(v,depth+1);if(r.error!=Error::None)co_return;
                if(array){auto &a=r.arrays[out.ref];if(a.count==ArrayLength){r.fail(Error::Range);co_return;}a.values[a.count++]=v;}
                else {Value k=Value::handle(Kind::String,key);co_await r.property(out,k,true,v);}
                if(++count>PropertyLimit&&!array){r.fail(Error::Range);co_return;}skip();if(pos==length){r.fail(Error::Syntax);co_return;}char next=source[pos++];if(next==end)break;if(next!=','){r.fail(Error::Syntax);co_return;}
            }co_return;
        }
        const char *literal=c=='t'?"true":c=='f'?"false":c=='n'?"null":nullptr;
        if(literal){for(unsigned i=0;literal[i];++i)if(pos==length||source[pos++]!=literal[i]){r.fail(Error::Syntax);co_return;}out=c=='n'?Value::handle(Kind::Null):Value::boolean(c=='t');co_return;}
        unsigned start=pos;if(source[pos]=='-')++pos;
        if(pos==length){r.fail(Error::Syntax);co_return;}
        if(source[pos]=='0')++pos;else {if(source[pos]<'1'||source[pos]>'9'){r.fail(Error::Syntax);co_return;}while(pos<length&&source[pos]>='0'&&source[pos]<='9'){co_await r.point();++pos;}}
        if(pos<length&&source[pos]=='.'){++pos;unsigned first=pos;while(pos<length&&source[pos]>='0'&&source[pos]<='9'){co_await r.point();++pos;}if(pos==first){r.fail(Error::Syntax);co_return;}}
        if(pos<length&&(source[pos]=='e'||source[pos]=='E')){++pos;if(pos<length&&(source[pos]=='+'||source[pos]=='-'))++pos;unsigned first=pos;while(pos<length&&source[pos]>='0'&&source[pos]<='9'){co_await r.point();++pos;}if(pos==first){r.fail(Error::Syntax);co_return;}}
        bool ok=false;out=Value::num(numeric_parse(source+start,pos-start,ok));if(!ok)r.fail(Error::Syntax);
    }
    Work escape(unsigned s){
        put('"');for(unsigned i=0;i<r.size(s);++i){co_await r.point();unsigned char c=r.text(s)[i];if(c=='"'||c=='\\'){put('\\');put(char(c));}else if(c<32){const char *hex="0123456789abcdef";put('\\');put('u');put('0');put('0');put(hex[c>>4]);put(hex[c&15]);}else put(char(c));}put('"');
    }
    Work stringify(Value v,unsigned depth=0){
        co_await r.point();if(depth==JsonDepth||++nodes>JsonValues){r.fail(Error::Range);co_return;}
        if(v.kind==Kind::String){co_await escape(v.ref);co_return;}
        if(v.kind==Kind::Number&&v.number.finite()){char number[64];unsigned n=numeric_format(v.number,number);for(unsigned i=0;i<n;++i)put(number[i]);co_return;}
        if(v.kind!=Kind::Object&&v.kind!=Kind::Array){const char *s=v.kind==Kind::Boolean?(v.number?"true":"false"):"null";for(unsigned i=0;s[i];++i)put(s[i]);co_return;}
        for(unsigned i=0;i<depth;++i)if(r.same(path[i],v)){r.fail(Error::Type);co_return;}path[depth]=v;
        bool array=v.kind==Kind::Array;if((array&&v.ref>=r.array_count)||(!array&&v.ref>=r.object_count)){r.fail(Error::Type);co_return;}
        put(array?'[':'{');unsigned count=array?r.arrays[v.ref].count:r.objects[v.ref].count,written=0;
        for(unsigned i=0;i<count;++i){co_await r.point();Value child=array?r.arrays[v.ref].values[i]:r.objects[v.ref].props[i].value;
            if(!array&&(child.kind==Kind::Undefined||child.kind==Kind::Function||child.kind==Kind::Native))continue;
            if(written++)put(',');if(!array){co_await escape(r.objects[v.ref].props[i].name);put(':');}co_await stringify(child,depth+1);if(r.error!=Error::None)co_return;
        }put(array?']':'}');
    }
};
inline Work Runtime::json(Value input,bool stringify,Value &out){
    struct Owner{JsonCodec *p{};~Owner(){delete p;}} owner;owner.p=new JsonCodec(*this);if(!owner.p){fail(Error::Memory);co_return;}auto &j=*owner.p;
    if(stringify){if(input.kind==Kind::Undefined||input.kind==Kind::Function||input.kind==Kind::Native){out={};co_return;}co_await j.stringify(input);if(error==Error::None)out=Value::handle(Kind::String,string(j.buffer,j.used));}
    else {if(input.kind!=Kind::String||size(input.ref)>JsonSourceLimit){fail(Error::Range);co_return;}j.source=text(input.ref);j.length=size(input.ref);co_await j.parse(out);j.skip();if(j.pos!=j.length)fail(Error::Syntax);}
}
} }
