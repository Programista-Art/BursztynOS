#pragma once
#include <vector>
#include <cstdint>
// Standard Generic Desktop Mouse/Pointer collection. Buttons, padding, wheel,
// axes; parameters exercise bit offsets and signed/nonzero logical ranges.
inline std::vector<uint8_t> tablet_descriptor(bool numbered=false,unsigned padding=5,int16_t minimum=0,int16_t maximum=32767,bool relative=false,unsigned axis_bits=16) {
    std::vector<uint8_t> d={5,1,9,2,0xA1,1,9,1,0xA1,0};
    if(numbered)d.insert(d.end(),{0x85,7});
    const uint8_t buttons[]={5,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2};
    d.insert(d.end(),buttons,buttons+sizeof(buttons));
    if(padding)d.insert(d.end(),{0x75,uint8_t(padding),0x95,1,0x81,3});
    d.insert(d.end(),{5,1,9,0x38,0x15,0x81,0x25,0x7F,0x75,8,0x95,1,0x81,6});
    d.insert(d.end(),{9,0x30,9,0x31,0x16,uint8_t(minimum),uint8_t(uint16_t(minimum)>>8),0x26,uint8_t(maximum),uint8_t(uint16_t(maximum)>>8),0x75,uint8_t(axis_bits),0x95,2,0x81,uint8_t(relative?6:2),0xC0,0xC0});
    return d;
}
inline std::vector<uint8_t> tablet_report(const HidPointerLayout& l,int32_t x,int32_t y,uint8_t buttons=0,int32_t wheel=0,uint8_t id=0) {
    if(l.numbered&&!id)id=7;
    unsigned bits=0;for(unsigned i=0;i<l.id_count;++i)if(l.ids[i]==id)bits=l.bits[i];
    std::vector<uint8_t> report((bits+7)/8+(l.numbered?1:0),0);if(l.numbered)report[0]=id;
    for(unsigned i=0;i<l.field_count;++i){const auto& f=l.fields[i];if(f.report_id!=id)continue;
        const uint32_t v=f.page==9?((buttons>>(f.usage-1))&1):f.usage==0x30?uint32_t(x):f.usage==0x31?uint32_t(y):uint32_t(wheel);
        for(unsigned j=0;j<f.size;++j)if((v>>j)&1){const unsigned bit=f.offset+j+(l.numbered?8:0);report[bit/8]|=uint8_t(1U<<(bit%8));}}
    return report;
}
inline std::vector<uint8_t> usb_configuration(unsigned report_length=64) {
    return {9,2,34,0,1,1,0,0x80,50, 9,4,0,0,1,3,0,0,0,
        9,0x21,0x11,1,0,1,0x22,uint8_t(report_length),uint8_t(report_length>>8), 7,5,0x81,3,8,0,10};
}
