#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../tapetybtp.cpp"

using Bytes = std::vector<uint8_t>;
using Pixels = std::vector<uint32_t>;
constexpr uint32_t sentinel = 0x12345678U;
static size_t checks = 0;
static void require(bool ok, const char* name) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
static void u32(Bytes& b, size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[at+i] = uint8_t(v >> (8*i));
}
static Bytes header(uint32_t w, uint32_t h) {
    Bytes b(14);
    b[0]='B'; b[1]='T'; b[2]='P'; b[3]='1'; b[12]=32; b[13]=1;
    u32(b,4,w); u32(b,8,h);
    return b;
}
static void packet(Bytes& b, uint8_t n, uint32_t c) {
    b.push_back(n);
    for (unsigned i=0; i<4; ++i) b.push_back(uint8_t(c >> (8*i)));
}
static Bytes solid(uint32_t w, uint32_t h, uint32_t c) {
    Bytes b=header(w,h);
    for (uint64_t n=uint64_t(w)*h; n; ) {
        unsigned run=unsigned(std::min<uint64_t>(255,n));
        packet(b,uint8_t(run),c); n-=run;
    }
    return b;
}
static BtpWynik decode(const Bytes& b, Pixels& fb, uint32_t w, uint32_t h,
                       uint32_t stride, unsigned offset=1) {
    // Exactly sized allocation: ASan detects even a one-byte payload overread.
    auto* storage=static_cast<uint8_t*>(std::malloc(b.size()+offset+1));
    require(storage!=nullptr,"test allocation");
    auto* p=storage+offset+1;
    if (!b.empty()) std::memcpy(p,b.data(),b.size());
    auto result=btpwczytajirysuj(p,b.size(),fb.data(),w,h,stride,fb.size()*4);
    std::free(storage);
    return result;
}
static void error(const char* name, const Bytes& b, BtpWynik want,
                  uint32_t w=10, uint32_t h=10, uint32_t stride=13) {
    Pixels fb(size_t(stride)*h,sentinel), before=fb;
    require(decode(b,fb,w,h,stride)==want,name);
    require(fb==before,"error must leave destination unchanged");
}
static uint32_t le(const Bytes& b, size_t at) {
    uint32_t n=0;
    for (unsigned i=0;i<4;++i) n+=uint32_t(b[at+i])*(uint32_t(1)<<(i*8));
    return n;
}
// Independent reference: expand into a temporary test vector, index by / and %.
static bool reference(const Bytes& b, Pixels& dst, uint32_t w, uint32_t h,
                      uint32_t stride) {
    if (b.size()<14 || b[0]!='B' || b[1]!='T' || b[2]!='P' || b[3]!='1' ||
        b[12]!=32 || b[13]>1) return false;
    uint32_t sw=le(b,4), sh=le(b,8);
    if (!sw || !sh || sw>w || sh>h || sw>8192 || sh>8192) return false;
    size_t total=size_t(sw)*sh;
    Pixels raw;
    if (b[13]==0) {
        if (b.size()-14!=total*4) return false;
        for (size_t at=14;at<b.size();at+=4) raw.push_back(le(b,at));
    } else for (size_t at=14;at<b.size();) {
        if (b.size()-at<5 || !b[at] || b[at]>total-raw.size()) return false;
        uint32_t color=le(b,at+1);
        raw.insert(raw.end(),b[at],color);
        at+=5;
    }
    if (raw.size()!=total) return false;
    for (size_t i=0;i<total;++i)
        if (raw[i]) dst[(i/sw)*stride+i%sw]=raw[i];
    return true;
}
static void compare(const Bytes& b, uint32_t w, uint32_t h, uint32_t stride,
                    unsigned alignment) {
    Pixels actual(size_t(stride)*h,sentinel), want=actual, before=actual;
    bool valid=reference(b,want,w,h,stride);
    auto result=decode(b,actual,w,h,stride,alignment);
    require((result==BTPOK)==valid,"reference validity");
    require(actual==(valid?want:before),"reference pixels / no partial writes");
}
static uint32_t random_state=0x731a93bfU;
static uint32_t rng() {
    random_state^=random_state<<13; random_state^=random_state>>17;
    random_state^=random_state<<5; return random_state;
}
static Bytes readfile(const char* path) {
    FILE* f=std::fopen(path,"rb"); require(f!=nullptr,"open Python fixture");
    Bytes b; int c;
    while ((c=std::fgetc(f))!=EOF) b.push_back(uint8_t(c));
    std::fclose(f); return b;
}
int main(int argc, char** argv) {
    require(argc==2,"Python fixture bundle argument");
    const uint32_t colors[]={0xff000000,0xffffffff,0xff1a0b00,0xff280f00,
        0xffffbf00,0xffffcf40,0xffe58a00,0xff794400,0xff123456,
        0x00000000,0x00123456,0x01000000,0x80123456};
    for (uint32_t c:colors) {
        Pixels fb(1,sentinel);
        require(decode(solid(1,1,c),fb,1,1,1)==BTPOK,"1x1 / sample color");
        require(fb[0]==(c?c:sentinel),"explicit native color / transparency");
    }
    compare(solid(10,10,0xffffbf00),13,12,17,0);
    compare(solid(255,1,0xff123456),255,1,259,1);
    compare(solid(1,255,0xff123456),3,255,5,2);
    compare(solid(31,19,0xffe58a00),33,21,39,3);
    Bytes rows=header(3,3);
    packet(rows,4,0xff112233); packet(rows,2,0xff445566); packet(rows,3,0xff778899);
    compare(rows,5,4,8,1);
    const Bytes good=solid(1,1,0xff123456);
    Bytes b=good; b[0]='X'; error("bad magic",b,BTPBLADMAGIC);
    for (size_t n=0;n<14;++n) { b=good; b.resize(n); error("truncated header",b,BTPBLADZAMALYPLIK); }
    b=good; u32(b,4,0); error("zero width",b,BTPBLADWYMIAROW);
    b=good; u32(b,8,0); error("zero height",b,BTPBLADWYMIAROW);
    b=good; b[12]=24; error("unsupported bpp",b,BTPBLADBPP);
    for (uint8_t compression:{uint8_t(2),uint8_t(255)}) {
        b=good; b[13]=compression; error("unsupported compression",b,BTPBLADKOMPRESJI);
    }
    b=good; b[14]=0; error("zero run",b,BTPBLADRLE);
    for (size_t n=14;n<19;++n) { b=good; b.resize(n); error("truncated packet",b,BTPBLADRLE); }
    b=good; b[14]=2; error("overlong run",b,BTPBLADRLE);
    b=header(3,1); packet(b,10,0xff000000); error("3 remaining / run 10",b,BTPBLADRLE);
    b=header(3,1); packet(b,2,0xff000000); error("incomplete image",b,BTPBLADRLE);
    b=header(3,1); packet(b,1,0xff000000); packet(b,0,0xff000000);
    error("late malformed run / atomic failure",b,BTPBLADRLE);
    b=good; packet(b,1,0xff000000); error("extra packet",b,BTPBLADRLE);
    b=good; b.push_back(0); error("trailing byte",b,BTPBLADRLE);
    b=header(UINT32_MAX,UINT32_MAX);
    error("source size overflow",b,BTPBLADPRZEPELNIENIA);
    b=header(65536,65536); error("32-bit product wrapping attempt",b,BTPBLADWYMIAROW);
    b=header(8193,1); error("dimension cap",b,BTPBLADWYMIAROW);
    b=header(11,1); error("destination narrower",b,BTPBLADWYMIAROW);
    b=header(1,11); error("destination shorter",b,BTPBLADWYMIAROW);
    compare(solid(8192,1,0xff000000),8192,1,8192,0);
    compare(solid(1,8192,0xffffffff),1,8192,3,0);

    uint32_t pixel=sentinel;
    require(btpwczytajirysuj(nullptr,19,&pixel,1,1,1,4)==BTPBLADARGUMENTU,"null input");
    require(btpwczytajirysuj(good.data(),19,nullptr,1,1,1,4)==BTPBLADARGUMENTU,"null destination");
    require(btpwczytajirysuj(good.data(),19,&pixel,0,1,1,4)==BTPBLADWYMIAROW,"zero fb width");
    require(btpwczytajirysuj(good.data(),19,&pixel,1,0,1,4)==BTPBLADWYMIAROW,"zero fb height");
    require(btpwczytajirysuj(good.data(),19,&pixel,2,1,1,4)==BTPBLADBUFORA,"short stride");
    require(btpwczytajirysuj(good.data(),19,&pixel,1,1,0,4)==BTPBLADBUFORA,"zero stride");
    require(btpwczytajirysuj(good.data(),19,&pixel,1,1,1,3)==BTPBLADBUFORA,"short capacity");
    require(btpwczytajirysuj(good.data(),19,&pixel,1,UINT32_MAX,UINT32_MAX,SIZE_MAX)==BTPBLADPRZEPELNIENIA,"destination product overflow");
    require(btpwczytajirysuj(good.data(),SIZE_MAX,&pixel,1,1,1,4)==BTPBLADPRZEPELNIENIA,"input address wrap");
    auto* high=reinterpret_cast<uint32_t*>(UINTPTR_MAX-3);
    require(btpwczytajirysuj(good.data(),19,high,1,2,1,8)==BTPBLADPRZEPELNIENIA,"destination address wrap");
    alignas(uint32_t) uint8_t overlapping[64]={};
    std::memcpy(overlapping,good.data(),good.size());
    require(btpwczytajirysuj(overlapping,19,reinterpret_cast<uint32_t*>(overlapping),1,1,1,4)==BTPBLADARGUMENTU,"input/output overlap");
    require(btpwczytajirysuj(overlapping,19,reinterpret_cast<uint32_t*>(overlapping+16),1,1,1,4)==BTPBLADARGUMENTU,"partial overlap");
    require(btpwczytajirysuj(overlapping,19,reinterpret_cast<uint32_t*>(overlapping+1),1,1,1,4)==BTPBLADARGUMENTU,"unaligned destination");
    require(pixel==sentinel,"argument errors leave output unchanged");

    // Python emits independent expected BGRA pixels alongside each real BTP file.
    Bytes bundle=readfile(argv[1]);
    require(bundle.size()>=4,"bundle count");
    size_t position=4;
    uint32_t fixture_count=le(bundle,0);
    size_t fixture_pixels=0;
    for (uint32_t f=0;f<fixture_count;++f) {
        require(bundle.size()-position>=12,"bundle metadata");
        uint32_t length=le(bundle,position), width=le(bundle,position+4), height=le(bundle,position+8);
        position+=12;
        size_t rawsize=size_t(width)*height*4;
        require(width && height && width<=8192 && height<=8192,"bundle dimensions");
        require(length<=bundle.size()-position,"bundle BTP length");
        Bytes encoded(bundle.begin()+position,bundle.begin()+position+length);
        position+=length;
        require(rawsize<=bundle.size()-position,"bundle expected pixels length");
        uint32_t stride=width+5;
        Pixels fb(size_t(stride)*(height+2),sentinel);
        require(decode(encoded,fb,width+2,height+2,stride,f%8)==BTPOK,"Python encoder / C++ decoder");
        for (size_t y=0;y<height+2;++y) for (size_t x=0;x<stride;++x) {
            uint32_t want=sentinel;
            if (x<width && y<height) {
                uint32_t c=le(bundle,position+(y*width+x)*4);
                if(c) want=c;
            }
            require(fb[y*stride+x]==want,"Python pixels / native packing / padding");
        }
        fixture_pixels+=rawsize/4;
        position+=rawsize;
    }
    require(position==bundle.size(),"bundle exact end");
    std::printf("Python compatibility: %u fixtures, %zu source pixels\n",fixture_count,fixture_pixels);

    // RAW errors must leave every pixel untouched, including late truncation.
    Bytes raw=header(3,2); raw[13]=0;
    for (unsigned i=0;i<6;++i) {
        size_t at=raw.size(); raw.resize(at+4); u32(raw,at,0xff123456U+i);
    }
    compare(raw,5,4,9,1);
    b=raw; b.pop_back(); error("RAW one byte short",b,BTPBLADRAW);
    b=raw; b.push_back(0); error("RAW one byte long",b,BTPBLADRAW);
    b=raw; b[13]=2; error("RAW invalid mode",b,BTPBLADKOMPRESJI);
    b=raw; b[12]=24; error("RAW wrong bpp",b,BTPBLADBPP);
    b=raw; b[0]='X'; error("RAW bad magic",b,BTPBLADMAGIC);
    b=raw; u32(b,4,0); error("RAW zero width",b,BTPBLADWYMIAROW);
    b=raw; u32(b,8,0); error("RAW zero height",b,BTPBLADWYMIAROW);
    b=raw; u32(b,4,UINT32_MAX); u32(b,8,UINT32_MAX);
    error("RAW dimension overflow",b,BTPBLADPRZEPELNIENIA);
    for (uint32_t c:colors) {
        b=header(1,1); b[13]=0; b.resize(18); u32(b,14,c);
        compare(b,2,2,3,3);
    }

    for (unsigned test=0;test<100000;++test) {
        uint32_t w=1+rng()%17,h=1+rng()%17,stride=w+rng()%6;
        if (test%3==0) {
            b.resize(rng()%200);
            for (auto& byte:b) byte=uint8_t(rng());
            if (test%6==0 && b.size()>=14) {
                Bytes hd=header(1+rng()%20,1+rng()%20);
                std::copy(hd.begin(),hd.end(),b.begin());
            }
        } else {
            uint32_t sw=1+rng()%w,sh=1+rng()%h;
            b=header(sw,sh);
            for (uint32_t left=sw*sh;left;) {
                uint32_t run=1+rng()%std::min(255U,left);
                packet(b,uint8_t(run),test%7==0?0:rng()); left-=run;
            }
            if (test%3==1) switch (rng()%5) {
                case 0: b[rng()%b.size()]^=uint8_t(1U<<(rng()%8)); break;
                case 1: b.resize(rng()%b.size()); break;
                case 2: packet(b,uint8_t(rng()),rng()); break;
                case 3: b[14]=0; break;
                case 4: u32(b,4,rng()); break;
            }
        }
        compare(b,w,h,stride,test%8);
    }
    std::printf("RLE differential fuzz: 100000 cases passed\n");
    for (unsigned test=0;test<100000;++test) {
        uint32_t w=1+rng()%17,h=1+rng()%17,stride=w+rng()%6;
        uint32_t sw=1+rng()%w,sh=1+rng()%h;
        b=header(sw,sh); b[13]=0; b.resize(14+size_t(sw)*sh*4);
        for(size_t at=14;at<b.size();at+=4) u32(b,at,test%7==0?0:rng());
        if(test%3!=0) switch(rng()%8) {
            case 0: b.resize(rng()%b.size()); break;
            case 1: b.push_back(uint8_t(rng())); break;
            case 2: u32(b,4,rng()); break;
            case 3: u32(b,8,rng()); break;
            case 4: b[13]=uint8_t(rng()); break;
            case 5: b[12]=uint8_t(rng()); break;
            case 6: b[rng()%14]^=uint8_t(1U<<(rng()%8)); break;
            case 7: b[14+rng()%(b.size()-14)]=uint8_t(rng()); break;
        }
        compare(b,w,h,stride,test%8);
    }
    std::printf("RAW differential fuzz: 100000 cases passed\n");
    std::printf("PASS: %zu checks; RLE 100000 + RAW 100000 fuzz cases\n",checks);
}
