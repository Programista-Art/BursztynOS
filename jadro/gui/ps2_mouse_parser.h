#pragma once
#include <stdint.h>

struct Ps2MouseReport { int32_t dx{}, dy{}, wheel{}; uint8_t buttons{}; };
// Hardware-independent stream decoder. Command replies belong to the command
// transaction, never to this stream. Y is normalized to screen-down here.
struct Ps2MouseParser {
    uint8_t bytes[4]{}, index{}, id{};
    bool streaming{};
    uint32_t rejected{}, overflow{};
    bool configure(uint8_t device_id) {
        reset(); streaming=false;
        if(device_id!=0 && device_id!=3 && device_id!=4) return false;
        id=device_id; return true;
    }
    void reset() { index=0; for(auto& b:bytes)b=0; }
    bool feed(uint8_t status, uint8_t b, Ps2MouseReport& out) {
        if(!streaming || !(status&1) || !(status&0x20)) return false;
        if(status&0xC0) { reset(); ++rejected; return false; }
        if(index==0 && !(b&8)) { ++rejected; return false; }
        bytes[index++]=b;
        if(index!=(id==0?3:4)) return false;
        index=0;
        if(bytes[0]&0xC0) { ++overflow; return false; }
        // Explicit 8-bit sign extension; no unsigned integer promotion.
        const auto signed8=[](uint8_t v)->int32_t { return v<128?int32_t(v):int32_t(v)-256; };
        // Reject incompatible sign bits rather than interpreting corrupted data
        // as a 9-bit +/-255 movement. Standard usable deltas are signed bytes.
        if(bool(bytes[0]&0x10)!=bool(bytes[1]&0x80) ||
           bool(bytes[0]&0x20)!=bool(bytes[2]&0x80)) { ++rejected; return false; }
        out={signed8(bytes[1]),-signed8(bytes[2]),0,uint8_t(bytes[0]&7)};
        if(id==3) out.wheel=signed8(bytes[3]);
        if(id==4) {
            if(bytes[3]&0xC0) { ++rejected; return false; }
            int32_t w=bytes[3]&15; out.wheel=w>=8?w-16:w;
            out.buttons|=uint8_t((bytes[3]&0x30)>>1);
        }
        return true;
    }
};
