#pragma once
#include <stdint.h>
namespace Husarz { namespace Js {
// The one JS Number representation. Integer bridges always check before casting.
struct Numeric {
    double value{};
    Numeric()=default;
    Numeric(double n):value(n){}
    static Numeric bits(uint64_t n){double d;__builtin_memcpy(&d,&n,8);return d;}
    uint64_t bits()const{uint64_t n;__builtin_memcpy(&n,&value,8);return n;}
    bool finite()const{return (bits()&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL;}
    static Numeric nan(){return bits(0x7ff8000000000000ULL);}
    static Numeric infinity(){return bits(0x7ff0000000000000ULL);}
    explicit operator bool()const{return value!=0 && value==value;}
    explicit operator int64_t()const{return finite()&&value>=-9223372036854775808.0&&value<9223372036854775808.0?int64_t(value):0;}
    explicit operator uint64_t()const{return finite()&&value>=0&&value<18446744073709551616.0?uint64_t(value):0;}
    explicit operator unsigned()const{return finite()&&value>=0&&value<=4294967295.0?unsigned(value):0;}
    friend Numeric operator+(Numeric a,Numeric b){return a.value+b.value;}
    friend Numeric operator-(Numeric a,Numeric b){return a.value-b.value;}
    Numeric operator-()const{return -value;}
    friend Numeric operator*(Numeric a,Numeric b){return a.value*b.value;}
    friend Numeric operator/(Numeric a,Numeric b){return a.value/b.value;}
    Numeric &operator+=(Numeric b){return *this=*this+b;}Numeric &operator-=(Numeric b){return *this=*this-b;}
    Numeric &operator*=(Numeric b){return *this=*this*b;}Numeric &operator/=(Numeric b){return *this=*this/b;}
    friend bool operator==(Numeric a,Numeric b){return a.value==b.value;}
    friend bool operator!=(Numeric a,Numeric b){return a.value!=b.value;}
    friend bool operator<(Numeric a,Numeric b){return a.value<b.value;}
    friend bool operator>(Numeric a,Numeric b){return a.value>b.value;}
    friend bool operator<=(Numeric a,Numeric b){return a.value<=b.value;}
    friend bool operator>=(Numeric a,Numeric b){return a.value>=b.value;}
    static Numeric remainder(Numeric a,Numeric b){
        if(!a.finite()||b.value==0||b.value!=b.value)return nan();
        double x=a.value<0?-a.value:a.value,y=b.value<0?-b.value:b.value;
        if(x<y)return a;
        // Powers of two preserve exact significands; bounded by binary64 exponents.
        double step=y;unsigned shifts=0;
        while(step<=x/2 && shifts<2098){step*=2;++shifts;}
        for(;;){if(x>=step)x-=step;if(!shifts--)break;step*=0.5;}
        return a.bits()>>63?-x:x;
    }
};
// Bounded decimal conversion; no libc, integer overflow, or extended precision ABI.
inline Numeric numeric_parse(const char *s,unsigned n,bool &ok){
    ok=false;unsigned i=0;bool neg=false;if(i<n&&(s[i]=='-'||s[i]=='+'))neg=s[i++]=='-';
    if(n-i==8&&s[i]=='I'&&s[i+1]=='n'&&s[i+2]=='f'&&s[i+3]=='i'&&s[i+4]=='n'&&s[i+5]=='i'&&s[i+6]=='t'&&s[i+7]=='y'){ok=true;return neg?-Numeric::infinity():Numeric::infinity();}
    if(n-i>2&&s[i]=='0'&&(s[i+1]=='x'||s[i+1]=='X')){
        i+=2;double v=0;unsigned start=i;for(;i<n;++i){unsigned d=s[i]>='0'&&s[i]<='9'?unsigned(s[i]-'0'):s[i]>='a'&&s[i]<='f'?unsigned(s[i]-'a'+10):s[i]>='A'&&s[i]<='F'?unsigned(s[i]-'A'+10):16;if(d==16)return Numeric::nan();v=v*16+d;}ok=i>start;return neg?-v:v;
    }
    uint64_t mantissa=0;unsigned kept=0,digits=0;int exponent=0;bool dot=false,significant=false;
    for(;i<n;++i){char c=s[i];if(c=='.'&&!dot){dot=true;continue;}if(c<'0'||c>'9')break;++digits;
        if(dot){--exponent;}if(c!='0')significant=true;
        if(significant){if(kept<19){mantissa=mantissa*10+unsigned(c-'0');++kept;}else ++exponent;}
    }
    if(!digits)return Numeric::nan();
    if(i<n&&(s[i]=='e'||s[i]=='E')){++i;bool minus=false;if(i<n&&(s[i]=='+'||s[i]=='-'))minus=s[i++]=='-';unsigned start=i,e=0;for(;i<n&&s[i]>='0'&&s[i]<='9';++i)if(e<10000)e=e*10+unsigned(s[i]-'0');if(i==start)return Numeric::nan();exponent+=minus?-int(e):int(e);}
    if(i!=n){return Numeric::nan();}ok=true;double v=double(mantissa);
    if(!mantissa)return neg?-0.0:0.0;
    if(exponent>400){return neg?-Numeric::infinity():Numeric::infinity();}if(exponent< -400)return neg?-0.0:0.0;
    // Split underflow scaling: pow(10,324) itself is not representable.
    if(exponent< -308){v/=1e308;exponent+=308;}
    double power=1,base=10;unsigned e=unsigned(exponent<0?-exponent:exponent);
    while(e){if(e&1)power*=base;e>>=1;if(e)base*=base;}
    v=exponent<0?v/power:v*power;return neg?-v:v;
}
inline unsigned numeric_format(Numeric number,char *out){
    double x=number.value;unsigned n=0;
    if(x!=x){out[0]='N';out[1]='a';out[2]='N';return 3;}
    if(!number.finite()){if(x<0)out[n++]='-';const char *s="Infinity";for(unsigned i=0;i<8;++i)out[n++]=s[i];return n;}
    if(x==0){out[0]='0';return 1;}if(x<0){out[n++]='-';x=-x;}
    int exponent=0;double normalized=x;
    while(normalized>=10){normalized/=10;++exponent;}while(normalized<1){normalized*=10;--exponent;}
    uint64_t chosen=0;unsigned precision=17;double scale=1;
    for(unsigned p=1;p<=17;++p){
        uint64_t digits=uint64_t(normalized*scale+0.5);char candidate[48];unsigned k=0;uint64_t divisor=1;for(unsigned j=1;j<p;++j)divisor*=10;
        // A rounding carry is handled by an extra leading digit and exponent below.
        uint64_t q=digits;char rev[24];unsigned used=0;do{rev[used++]=char('0'+q%10);q/=10;}while(q);while(used)candidate[k++]=rev[--used];
        candidate[k++]='e';int e=exponent-int(p)+1;if(e<0){candidate[k++]='-';e=-e;}q=unsigned(e);used=0;do{rev[used++]=char('0'+q%10);q/=10;}while(q);while(used)candidate[k++]=rev[--used];
        bool ok=false;Numeric roundtrip=numeric_parse(candidate,k,ok);chosen=digits;
        if(ok&&roundtrip.value==x){precision=p;break;}scale*=10;
    }
    char digits[24];unsigned count=0;do{digits[count++]=char('0'+chosen%10);chosen/=10;}while(chosen);
    for(unsigned i=0;i<count/2;++i){char c=digits[i];digits[i]=digits[count-1-i];digits[count-1-i]=c;}
    int decimal=exponent-int(precision)+1+int(count);
    while(count>1&&digits[count-1]=='0')--count;
    if(decimal>0&&decimal<=21){for(int i=0;i<decimal;++i)out[n++]=unsigned(i)<count?digits[i]:'0';if(unsigned(decimal)<count){out[n++]='.';for(unsigned i=unsigned(decimal);i<count;++i)out[n++]=digits[i];}}
    else if(decimal<=0&&decimal> -6){out[n++]='0';out[n++]='.';for(int i=0;i< -decimal;++i)out[n++]='0';for(unsigned i=0;i<count;++i)out[n++]=digits[i];}
    else {out[n++]=digits[0];if(count>1){out[n++]='.';for(unsigned i=1;i<count;++i)out[n++]=digits[i];}out[n++]='e';int e=decimal-1;out[n++]=e<0?'-':'+';if(e<0)e=-e;char rev[8];unsigned k=0;do{rev[k++]=char('0'+e%10);e/=10;}while(e);while(k)out[n++]=rev[--k];}
    return n;
}
} }
