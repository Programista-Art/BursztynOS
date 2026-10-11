#include "sterowniki/usb/hid_pointer.h"
#include "pointing_diag.h"
#include "pointer_coordinates.h"
#include "xhci_pointing_diag.h"
#include "sterowniki/usb/xhci_physical_diag.h"

namespace {
uint16_t le16(const uint8_t* b) { return uint16_t(b[0]) | uint16_t(b[1])<<8; }
int64_t sign_value(uint32_t v,unsigned bits) {
    return (v & (uint32_t(1)<<(bits-1))) ? int64_t(v)-(int64_t(1)<<bits) : int64_t(v);
}
struct Global { uint32_t page{},size{},count{}; int64_t min{},max{}; uint8_t id{}; };
struct Local {
    uint32_t usages[128]{}; unsigned count{}; uint32_t lo{},hi{}; bool range{},has_hi{};
};
}
bool usb_hid_find_interface(const uint8_t* b,size_t n,UsbHidInterface& out) {
    out={};
    if(!b||n<9||b[0]!=9||b[1]!=2||le16(b+2)>n||le16(b+2)<9||le16(b+2)>4096||!b[5])return false;
    const size_t total=le16(b+2); UsbHidInterface candidate{}; bool match=false,hid=false,found=false;size_t companion_offset=0;
    // Validate the entire configuration, including descriptors after our endpoint.
    for(size_t p=9;p<total;) {
        if(total-p<2||b[p]<2||b[p]>total-p)return false;
        const uint8_t len=b[p],type=b[p+1];
        if(type==4) {
            if(len<9)return false;
            match=b[p+5]==3&&b[p+3]==0;hid=false;
            candidate={}; candidate.configuration=b[5];candidate.interface_number=b[p+2];
            candidate.subclass=b[p+6];candidate.protocol=b[p+7];
        } else if(type==0x21&&match) {
            if(len<6||unsigned(6+3*b[p+5])>len)return false;
            candidate.hid_bcd=le16(b+p+2);
            for(unsigned j=0;j<b[p+5];++j)if(b[p+6+3*j]==0x22) {
                candidate.report_length=le16(b+p+7+3*j);hid=true;
            }
        } else if(type==5&&match) {
            if(len<7)return false;
            const uint16_t raw=le16(b+p+4);
            if(hid && (b[p+2]&0x80) && !(b[p+2]&0x70) && (b[p+2]&15) &&
               (b[p+3]&3)==3 && (raw&0x7ff) && (raw&0x7ff)<=HID_POINTER_REPORT_MAX && !(raw&0xe000) && b[p+6] &&
               candidate.report_length && candidate.report_length<=HID_POINTER_DESCRIPTOR_MAX && !found) {
                candidate.endpoint=b[p+2];candidate.packet_size=raw&0x7ff;candidate.transactions=uint8_t((raw>>11)&3);candidate.interval=b[p+6];
                out=candidate;found=true;companion_offset=p+len;
            }
        }
        if(type==0x30 && found && p==companion_offset) {
            if(len<6 || b[p+2]>15 || b[p+3]!=0)return false;
            out.companion=true;out.max_burst=b[p+2];out.bytes_per_interval=le16(b+p+4);
        }
        p+=len;
    }
#if BURSZTYNDIAGUSBHID
    if(found){const int64_t v[]={out.interface_number,3,out.subclass,out.protocol,out.hid_bcd,out.report_length,out.endpoint,out.packet_size};
        pointing_diag("[USB-HID] interface/class/subclass/protocol/bcdHID/report-length/endpoint/mps",v,8);}
#endif
    return found;
}
bool hid_pointer_compile(const uint8_t* data,size_t length,HidPointerLayout& out) {
    out={};HidPointerLayout layout{};
    if(!data||!length||length>HID_POINTER_DESCRIPTOR_MAX)return false;
    Global g{},stack[4]{};unsigned sp=0;Local local{};
    bool collection[16]{};unsigned depth=0,items=0;
    bool x=false,y=false,relative_x=false,relative_y=false;
    auto report_index=[&]()->int {
        for(unsigned i=0;i<layout.id_count;++i)if(layout.ids[i]==g.id)return int(i);
        if(layout.id_count==HID_POINTER_IDS_MAX)return -1;
        const unsigned i=layout.id_count++;layout.ids[i]=g.id;return int(i);
    };
    for(size_t p=0;p<length;) {
        if(++items>1024)return false;
        uint8_t prefix=data[p++]; if(prefix==0xFE)return false; // unsupported long items
        unsigned size=prefix&3;if(size==3)size=4;
        if(size>length-p)return false;
        uint32_t v=0;for(unsigned j=0;j<size;++j)v|=uint32_t(data[p++])<<(8*j);
        const unsigned type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1) {
            switch(tag) {
                case 0: if(v>65535)return false;g.page=v;break;
                case 1: if(!size)return false;g.min=sign_value(v,size*8);break;
                case 2: if(!size)return false;g.max=g.min<0?sign_value(v,size*8):int64_t(v);break;
                case 7: if(!v||v>32)return false;g.size=v;break;
                case 8: if(!v||v>255)return false;g.id=uint8_t(v);layout.numbered=true;break;
                case 9: if(!v||v>128)return false;g.count=v;break;
                case 10: if(sp==4)return false;stack[sp++]=g;break;
                case 11: if(!sp)return false;g=stack[--sp];break;
                default: break; // physical/unit metadata does not change bit layout
            }
        } else if(type==2) {
            const uint32_t usage=size==4?v:(g.page<<16)|(v&65535);
            if(tag==0) { if(local.count==128)return false;local.usages[local.count++]=usage; }
            else if(tag==1) {local.lo=usage;local.range=true;}
            else if(tag==2) {local.hi=usage;local.has_hi=true;}
            else if(tag==10)return false; // alternate usage sets not supported
        } else if(type==0) {
            auto usage_at=[&](unsigned j)->uint32_t {
                if(local.count)return local.usages[j<local.count?j:local.count-1];
                if(local.range&&local.has_hi&&local.hi>=local.lo) {
                    const uint64_t next=uint64_t(local.lo)+j;return next<=local.hi?uint32_t(next):local.hi;
                }
                return 0;
            };
            if(local.range&&(!local.has_hi||local.hi<local.lo||(local.hi>>16)!=(local.lo>>16)))return false;
            if(tag==10) {
                if(depth==16)return false;
                const uint32_t u=usage_at(0);
                collection[depth]=(depth&&collection[depth-1]) || u==0x10001 || u==0x10002;
                ++depth;
            } else if(tag==12) {if(!depth)return false;--depth;}
            else if(tag==8) {
                if(!g.size||!g.count)return false;
                const int ri=report_index();if(ri<0)return false;
                const uint32_t bits=g.size*g.count,offset=layout.bits[ri];
                if(bits>HID_POINTER_REPORT_MAX*8-offset)return false;
                layout.bits[ri]=uint16_t(offset+bits);
                if(!(v&1)&&(v&2)&&depth&&collection[depth-1]) {
                    for(unsigned j=0;j<g.count;++j) {
                        const uint32_t u=usage_at(j);const uint16_t page=u>>16,usage=u&65535;
                        if(!((page==1&&(usage==0x30||usage==0x31||usage==0x38)) ||
                             (page==9&&usage>=1&&usage<=5)||(page==12&&usage==0x238)||
                             (page==13&&usage==0x42)))continue;
                        if(layout.field_count==HID_POINTER_FIELDS_MAX||g.min<INT32_MIN||g.max>INT32_MAX||g.max<g.min)return false;
                        const int64_t representable_min=g.min<0?-(int64_t(1)<<(g.size-1)):0;
                        const int64_t representable_max=(int64_t(1)<<(g.min<0?g.size-1:g.size))-1;
                        if(g.min<representable_min||g.max>representable_max)return false;
                        if(page==9&&(g.min!=0||g.max!=1))return false;
                        if(page==13){if(g.min!=0||g.max!=1)return false;layout.contact=true;}
                        const bool relative=v&4;
                        if(page==1&&(usage==0x30||usage==0x31)) {
                            if(!relative&&g.max<=g.min)return false;
                            if(usage==0x30){x=true;relative_x=relative;layout.x={int32_t(g.min),int32_t(g.max),0,0,0};}
                            else {y=true;relative_y=relative;layout.y={int32_t(g.min),int32_t(g.max),0,0,0};}
                        }
                        // Reject duplicate controls within a report, ambiguous ranges
                        // between reports, and absolute wheels.
                        if((usage==0x38||page==12)&&!relative)return false;
                        for(unsigned k=0;k<layout.field_count;++k) {
                            const auto& old=layout.fields[k];
                            if(old.page==page&&old.usage==usage &&
                               (old.report_id==g.id||old.minimum!=g.min||old.maximum!=g.max||old.relative!=relative))return false;
                        }
                        layout.fields[layout.field_count++]={uint16_t(offset+j*g.size),page,usage,uint8_t(g.size),g.id,relative,int32_t(g.min),int32_t(g.max)};
                    }
                }
            } else if(tag!=9&&tag!=11)return false;
            local={};
        } else return false;
    }
    if(depth||sp||!x||!y||relative_x!=relative_y)return false;
    if(layout.numbered) for(unsigned i=0;i<layout.id_count;++i)
        if(!layout.ids[i] || size_t((layout.bits[i]+7)/8+1)>HID_POINTER_REPORT_MAX)return false;
    layout.absolute=!relative_x;out=layout;
#if BURSZTYNDIAGUSBHID
    for(unsigned i=0;i<out.field_count;++i){const auto& f=out.fields[i];
        const int64_t v[]={f.page,f.usage,f.report_id,f.offset,f.size,f.minimum,f.maximum,f.relative};
        pointing_diag("[USB-HID] page/usage/id/bit-offset/size/min/max/relative",v,8);}
#endif
    return true;
}
bool hid_pointer_decode(const HidPointerLayout& l,const uint8_t* report,size_t length,
                        InputEvent* events,size_t capacity,size_t& count) {
    count=0;if(!report||!events||!l.id_count||!length||capacity<l.field_count+1U)return false;
    uint8_t id=0;if(l.numbered){id=*report++;--length;}
    int ri=-1;for(unsigned i=0;i<l.id_count;++i)if(l.ids[i]==id)ri=int(i);
    if(ri<0||length<(l.bits[ri]+7U)/8)return false;
    InputEvent decoded[HID_POINTER_FIELDS_MAX+1];size_t n=0;
    for(unsigned i=0;i<l.field_count;++i) {
        const auto& f=l.fields[i];if(f.report_id!=id)continue;
        uint32_t raw=0;for(unsigned b=0;b<f.size;++b) {
            const unsigned bit=f.offset+b;raw|=uint32_t((report[bit/8]>>(bit%8))&1)<<b;
        }
        int64_t val=f.minimum<0?sign_value(raw,f.size):int64_t(raw);
        if((f.relative||f.page==9||f.page==13)&&(val<f.minimum||val>f.maximum))return false;
        if(val<f.minimum)val=f.minimum;
        if(val>f.maximum)val=f.maximum;
        if(f.page==13)decoded[n++]={INPUT_EV_KEY,INPUT_BTN_TOUCH,val?1U:0U};
        else if(f.page==9)decoded[n++]={INPUT_EV_KEY,uint16_t(INPUT_BTN_LEFT+f.usage-1),val?1U:0U};
        else {
            const uint16_t code=f.page==12?INPUT_REL_HWHEEL:f.usage==0x38?INPUT_REL_WHEEL:
                f.usage==0x30?(f.relative?INPUT_REL_X:INPUT_ABS_X):(f.relative?INPUT_REL_Y:INPUT_ABS_Y);
            decoded[n++]={uint16_t(f.relative?INPUT_EV_REL:INPUT_EV_ABS),code,uint32_t(val)};
        }
    }
    if(!n)return false;
    decoded[n++]={INPUT_EV_SYN,INPUT_SYN_REPORT,0};
    for(size_t i=0;i<n;++i)events[i]=decoded[i];
    count=n;return true;
}
bool hid_pointer_register(InputDevice& dev,const HidPointerLayout& layout,uint32_t id) {
    if(!dev.registered) {
        const char* name=layout.absolute?"usb-tablet":"usb-mouse";
        for(size_t i=0;name[i];++i)dev.name[i]=name[i];
        dev.kinds=layout.absolute?INPUT_DEVICE_KIND_TABLET:INPUT_DEVICE_KIND_MOUSE;
        dev.pointer_mode=layout.absolute?INPUT_POINTER_ABSOLUTE:INPUT_POINTER_RELATIVE;
        dev.abs_contact_required=layout.contact;
        dev.bus_type=INPUT_BUS_USB;dev.device_id=id;dev.abs_x=layout.x;dev.abs_y=layout.y;
        if(!input_register_device(&dev))return false;
    }
    pointing_snapshot("hid-registered");
    return true;
}
bool hid_pointer_publish(InputDevice& dev,const HidPointerLayout& layout,const uint8_t* b,size_t n,uint32_t id) {
    InputEvent events[HID_POINTER_FIELDS_MAX+1];size_t count=0;
    if(!hid_pointer_decode(layout,b,n,events,HID_POINTER_FIELDS_MAX+1,count)) {
        pointing_count(pointing_counters.hid_rejected); return false;
    }
    // Some report IDs can describe buttons alone. Absolute readiness needs both axes.
    bool x=false,y=false;
    for(size_t i=0;i<count;++i) {
        x |= events[i].type==INPUT_EV_ABS && events[i].code==INPUT_ABS_X;
        y |= events[i].type==INPUT_EV_ABS && events[i].code==INPUT_ABS_Y;
    }
    if(layout.absolute && (!x || !y) &&
       (!dev.registered || dev.pointer_state!=INPUT_POINTER_PROVEN_ACTIVE)) {
        pointing_count(pointing_counters.hid_rejected); return false;
    }
    if(!dev.registered && !hid_pointer_register(dev,layout,id))return false;
#if BURSZTYNDIAGXHCIPOINTING || BURSZTYNDIAGXHCIPHYSICALMOUSE
    static unsigned trace_reports=0;
    if(trace_reports<64 && (BURSZTYNDIAGXHCIPOINTING || dev.usb_controller==INPUT_USB_CONTROLLER_XHCI)) {
        ++trace_reports;int32_t rx=0,ry=0,wheel=0;uint32_t buttons=0;
        for(size_t i=0;i<count;++i) {
            if(events[i].type==INPUT_EV_KEY && events[i].value && events[i].code>=INPUT_BTN_LEFT && events[i].code<=INPUT_BTN_EXTRA)buttons|=1U<<(events[i].code-INPUT_BTN_LEFT);
            if(events[i].type==INPUT_EV_REL && events[i].code==INPUT_REL_WHEEL)wheel=input_signed_value(events[i].value);
            if((events[i].type==INPUT_EV_ABS && events[i].code==INPUT_ABS_X) ||
                (events[i].type==INPUT_EV_REL && events[i].code==INPUT_REL_X))rx=input_signed_value(events[i].value);
            if((events[i].type==INPUT_EV_ABS && events[i].code==INPUT_ABS_Y) ||
                (events[i].type==INPUT_EV_REL && events[i].code==INPUT_REL_Y))ry=input_signed_value(events[i].value);
        }
        const int64_t v[]={dev.device_id,dev.usb_controller,layout.numbered?b[0]:0,rx,ry,
            layout.x.min,layout.x.max,layout.y.min,layout.y.max,dev.pointer_state,
            dev.pointer_state!=INPUT_POINTER_PROVEN_ACTIVE,buttons,wheel,dev.pointer_mode};
        pointing_diag("[USB POINTER] id/controller/reportID/decodedX/Y/logicalMinMaxX/Y/state/first-valid/buttons/wheel/mode",v,14);
    }
#endif
    pointing_count(pointing_counters.hid_accepted);
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    const auto* old_owner=input_get_pointer_owner();const auto old_state=dev.pointer_state;
#endif
    if(!input_arm_pointer(&dev) || !input_prove_pointer(&dev))return false;
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    if(dev.usb_controller==INPUT_USB_CONTROLLER_XHCI && (old_state!=dev.pointer_state || old_owner!=input_get_pointer_owner())) {
        const int64_t v[]={dev.device_id,old_state,dev.pointer_state,old_owner?int64_t(old_owner->device_id):-1,
            input_get_pointer_owner()?int64_t(input_get_pointer_owner()->device_id):-1};
        xhci_physical_diag("[XHCI-PHYSICAL live pointer] id/stateBefore/stateAfter/ownerBefore/ownerAfter",v,5);
    }
#endif
#if BURSZTYNDIAGUSBHID
    static unsigned reports=0;
    if(reports<32){++reports;for(size_t i=0;i<count;++i){const int64_t v[]={events[i].type,events[i].code,int32_t(events[i].value)};
        pointing_diag("[USB-HID] decoded type/code/value",v,3);}}
#endif
    input_dispatch_events(&dev,events,count);return true;
}
void hid_pointer_remove(InputDevice& dev) {
    if(!dev.registered)return;
    InputEvent evs[6];for(unsigned i=0;i<5;++i)evs[i]={INPUT_EV_KEY,uint16_t(INPUT_BTN_LEFT+i),0};
    evs[5]={INPUT_EV_SYN,INPUT_SYN_REPORT,0};input_dispatch_events(&dev,evs,6);
    input_unregister_device(&dev);
}

bool hid_boot_mouse_layout(HidPointerLayout& layout,bool wheel) {
    // HID 1.11 Appendix B: buttons and signed screen-right/screen-down deltas.
    static const uint8_t base[]={5,1,9,2,0xa1,1,9,1,0xa1,0,5,9,0x19,1,0x29,3,
        0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,0x75,5,0x95,1,0x81,1,
        5,1,9,0x30,9,0x31,0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,6};
    uint8_t descriptor[sizeof(base)+10];size_t n=0;
    for(auto v:base)descriptor[n++]=v;
    if(wheel){const uint8_t w[]={9,0x38,0x95,1,0x81,6};for(auto v:w)descriptor[n++]=v;}
    descriptor[n++]=0xc0;descriptor[n++]=0xc0;
    return hid_pointer_compile(descriptor,n,layout);
}

bool hid_boot_mouse_has_wheel(const HidPointerLayout& layout) {
    // Boot bytes after X/Y are device-specific. Never invent a wheel from their
    // length alone: require a matching, descriptor-defined conventional layout.
    if(layout.numbered || layout.absolute)return false;
    bool x=false,y=false,wheel=false;
    for(unsigned i=0;i<layout.field_count;++i){const auto& f=layout.fields[i];
        if(f.page!=1 || !f.relative || f.size!=8 || f.report_id)continue;
        x|=f.usage==0x30 && f.offset==8;
        y|=f.usage==0x31 && f.offset==16;
        wheel|=f.usage==0x38 && f.offset==24 && f.minimum==-127 && f.maximum==127;
    }
    return x&&y&&wheel;
}
