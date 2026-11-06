#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <vector>
#include "diagnostyka_systemu.h"
#include "programy/diagnostyka/model.h"
#include "testy/pointing_host_stubs.h"
static uint8_t user_space[4096];
static bool copy_failure{};
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* ptr,size_t size){
 const uintptr_t a=reinterpret_cast<uintptr_t>(ptr),b=reinterpret_cast<uintptr_t>(user_space);
 return a>=b&&a-b<=sizeof(user_space)&&size<=sizeof(user_space)-(a-b);
}
bool skopiuj_do_przestrzeni_uzytkownika(void* to,const void* from,size_t size){
 if(copy_failure||!czy_bezpieczny_zakres_uzytkownika_do_zapisu(to,size))return false;
 std::memcpy(to,from,size);return true;
}
static DiagAppModel model;
static DiagSummary summary(){DiagSummary s{};diag_get_summary(&s);return s;}
static void snapshot(){model.summary=summary();for(unsigned i=0;i<model.summary.devices;++i)CHECK(diag_get_device(i,&model.devices[i]));for(unsigned i=0;i<model.summary.logs;++i)CHECK(diag_get_log(i,&model.logs[i]));model.valid=true;model.rebuild();}
int main(){
 diag_test_reset();CHECK(summary().devices==0);CHECK(summary().serious==1);CHECK(summary().errors==2);
 auto ps2=diag_device(DIAG_PS2,"PS/2","ps2keyboard",DIAG_NOT_FOUND,false,"Brak PS/2");
 CHECK(diag_publish(1,ps2)==1);CHECK(summary().errors==2);
 input_reset_all();InputDevice keyboard{},pointer{};
 std::strcpy(keyboard.name,"USB keyboard");keyboard.kinds=INPUT_DEVICE_KIND_KEYBOARD;keyboard.bus_type=INPUT_BUS_USB;
 std::strcpy(pointer.name,"USB mouse");pointer.kinds=INPUT_DEVICE_KIND_MOUSE;pointer.bus_type=INPUT_BUS_USB;
 CHECK(input_register_device(&keyboard));CHECK(summary().keyboard==1);CHECK(summary().pointer==0);
 CHECK(input_register_device(&pointer));CHECK(summary().pointer==0); // registration alone is not an armed pointer
 CHECK(input_arm_pointer(&pointer));CHECK(summary().pointer==1);CHECK(summary().serious==0);
 CHECK(summary().categories[DIAG_KEYBOARD]==DIAG_OK);CHECK(summary().categories[DIAG_MOUSE]==DIAG_OK);
 CHECK(summary().errors==0); // PS/2 absence does not override usable USB.
 auto touch=diag_device(DIAG_TOUCHPAD,"Touchpad","hidi2c",DIAG_DETECTED_UNSUPPORTED,true,"Brak HID-over-I2C");touch.code=DIAG_CODE_I2C_DRIVER;
 diag_publish(0,touch);CHECK(summary().pointer==1);CHECK(summary().categories[DIAG_TOUCHPAD]==DIAG_DETECTED_UNSUPPORTED);
 CHECK(summary().serious==0);CHECK(summary().warnings==1);
 auto xhci=diag_device(DIAG_XHCI,"xHCI","xhci",DIAG_OK,true,"Kontroler gotowy");xhci.controller=0;
 const auto parent=diag_publish(0,xhci);xhci.controller=1;diag_publish(1,xhci);
 auto usb=diag_device(DIAG_USB,"Composite","usb",DIAG_OK,true,"3 HID");usb.parent=parent;usb.hid_interfaces=3;
 const auto usb_id=diag_publish(0,usb);
 for(unsigned i=0;i<3;++i){auto h=diag_device(DIAG_HID,"HID","usbhid",DIAG_OK,true,"Gotowy");h.parent=usb_id;h.interface_number=i;diag_publish(i,h);}
 CHECK(summary().categories[DIAG_USB]==DIAG_OK);CHECK(summary().devices==10);
 const auto generation=summary().generation;diag_publish(1,xhci);CHECK(summary().generation==generation);
 xhci.status=DIAG_TIMEOUT;xhci.code=DIAG_CODE_XHCI_INIT;diag_publish(1,xhci);
 CHECK(summary().errors==1);CHECK(summary().serious==0);CHECK(summary().categories[DIAG_USB]==DIAG_OK);
 snapshot();CHECK(model.valid);CHECK(model.driver_count==model.summary.drivers);CHECK(model.summary.errors==1);
 CHECK(model.devices[model.order[0]].id==1); // driver failure does not prevent a usable app model.
 char output[32768];size_t length=0;CHECK(model.report(output,sizeof(output),&length));
 CHECK(std::strstr(output,"INPUT-USB-XHCI-0001"));CHECK(std::strstr(output,"interface=2"));CHECK(std::strstr(output,"PS/2"));
 CHECK(!model.report(output,16,&length));CHECK(output[15]==0);
 CHECK(!model.report(output,0,&length));
 auto p=diag_parse_preferences("autostart=0\nproblems=0\n",22);CHECK(!p.autostart&&!p.problems);
 p=diag_parse_preferences("autostart=1\r\nproblems=1\n",23);CHECK(p.autostart&&p.problems);
 p=diag_parse_preferences("autostart=2\nautostart=00\n",24);CHECK(p.autostart&&p.problems);
 p=diag_parse_preferences(nullptr,0);CHECK(p.autostart&&p.problems);
 CHECK(!std::strcmp(diag_code_text(DIAG_CODE_XHCI_INIT),"INPUT-USB-XHCI-0001"));
 CHECK(!std::strcmp(diag_code_text(DIAG_CODE_NO_POINTER),"INPUT-PTR-0001"));
 CHECK(diag_code_text(999)[0]==0);
 DiagDevice d{};CHECK(!diag_get_device(UINT32_MAX,&d));CHECK(!diag_get_device(0,nullptr));
 DiagLog log{};CHECK(!diag_get_log(UINT32_MAX,&log));CHECK(!diag_get_log(0,nullptr));
 const uint64_t addr=reinterpret_cast<uint64_t>(user_space);
 CHECK(diag_read_request(DIAG_GET_SUMMARY,addr,sizeof(DiagSummary),0,DIAG_ABI)==1);
 CHECK(diag_read_request(DIAG_GET_DEVICE,addr,sizeof(DiagDevice),0,DIAG_ABI)==1);
 CHECK(diag_read_request(DIAG_GET_DRIVER,addr,sizeof(DiagDevice),0,DIAG_ABI)==1);
 CHECK(diag_read_request(DIAG_GET_LOG,addr,sizeof(DiagLog),0,DIAG_ABI)==1);
 for(uint64_t cap=0;cap<sizeof(DiagDevice);++cap)CHECK(!diag_read_request(DIAG_GET_DEVICE,addr,cap,0,DIAG_ABI));
 CHECK(!diag_read_request(DIAG_GET_DEVICE,addr,65537,0,DIAG_ABI));
 CHECK(!diag_read_request(DIAG_GET_DEVICE,0,sizeof(d),0,DIAG_ABI));
 CHECK(!diag_read_request(DIAG_GET_DEVICE,UINT64_MAX-1,sizeof(d),0,DIAG_ABI));
 CHECK(!diag_read_request(DIAG_GET_DEVICE,addr+4095,sizeof(d),0,DIAG_ABI));
 CHECK(!diag_read_request(DIAG_GET_DEVICE,addr,sizeof(d),UINT64_MAX,DIAG_ABI));
 CHECK(!diag_read_request(DIAG_GET_SUMMARY,addr,sizeof(DiagSummary),1,DIAG_ABI));
 CHECK(!diag_read_request(DIAG_GET_DEVICE,addr,sizeof(d),0,2));CHECK(!diag_read_request(99,addr,sizeof(d),0,DIAG_ABI));
 copy_failure=true;CHECK(!diag_read_request(DIAG_GET_DEVICE,addr,sizeof(d),0,DIAG_ABI));copy_failure=false;
 CHECK(input_unregister_device(&keyboard));CHECK(summary().keyboard==0);CHECK(summary().serious==1);
 CHECK(input_unregister_device(&pointer));CHECK(summary().pointer==0);CHECK(summary().errors==3);
 auto startup=summary();CHECK(!diag_should_autostart({false,false},startup,20000000000ULL));
 CHECK(!diag_should_autostart({false,true},startup,14000000000ULL));
 CHECK(diag_should_autostart({false,true},startup,15000000000ULL));
 startup.serious=0;CHECK(!diag_should_autostart({false,true},startup,20000000000ULL));
 CHECK(diag_should_autostart({true,false},startup,1000000000ULL));
 CHECK(!diag_should_autostart({true,false},startup,999999999ULL));
 diag_withdraw_tree(usb_id);CHECK(diag_get_device(usb_id-1,&d));CHECK(d.status==DIAG_INACTIVE);
 for(unsigned i=usb_id;i<usb_id+3;++i){CHECK(diag_get_device(i,&d));CHECK(d.status==DIAG_INACTIVE);CHECK(!(d.flags&DIAG_FLAG_DETECTED));}
 const auto before_event=summary();diag_log(DIAG_HDA,DIAG_TIMEOUT,DIAG_CODE_HDA_INIT,"timeout");
 CHECK(summary().errors==before_event.errors+1);diag_log(DIAG_HDA,DIAG_WARNING,0,"warning");CHECK(summary().warnings==before_event.warnings+1);
 input_reset_all();diag_test_reset();
 InputDevice virtio{};virtio.bus_type=INPUT_BUS_VIRTIO;virtio.kinds=INPUT_DEVICE_KIND_KEYBOARD|INPUT_DEVICE_KIND_TABLET;
 CHECK(input_register_device(&virtio));diag_publish(1,ps2);CHECK(summary().keyboard&&summary().pointer&&!summary().serious);
 auto hda=diag_device(DIAG_HDA,"HDA","hda",DIAG_TIMEOUT,true,"timeout");hda.code=DIAG_CODE_HDA_INIT;
 diag_publish(0,hda);hda.status=DIAG_INACTIVE;diag_publish(1,hda);
 CHECK(summary().categories[DIAG_HDA]==DIAG_TIMEOUT);CHECK(!summary().serious);
 hda.status=DIAG_OK;hda.code=0;diag_publish(2,hda);CHECK(summary().categories[DIAG_HDA]==DIAG_OK);CHECK(!summary().serious);
 diag_set_status(3,DIAG_INIT_FAILED,DIAG_CODE_HDA_INIT,"error");
 CHECK(diag_get_device(2,&d)&&d.status==DIAG_INIT_FAILED);diag_set_status(UINT32_MAX,DIAG_CRITICAL,0,"ignored");
 diag_log(DIAG_SYSTEM,DIAG_CRITICAL,0,"system failure");CHECK(summary().serious&&summary().status==DIAG_CRITICAL);

 input_unregister_device(&virtio);input_reset_all();diag_test_reset();
 for(uint32_t i=0;i<DIAG_MAX_DEVICES;++i)CHECK(diag_publish(i,ps2)==i+1);
 CHECK(diag_publish(DIAG_MAX_DEVICES,ps2)==0);CHECK(summary().dropped_devices==1);CHECK(summary().devices==DIAG_MAX_DEVICES);
 // Input readiness survives a full device store: it comes from the common registry.
 keyboard={};keyboard.kinds=INPUT_DEVICE_KIND_KEYBOARD;keyboard.bus_type=INPUT_BUS_USB;
 pointer={};pointer.kinds=INPUT_DEVICE_KIND_MOUSE;pointer.bus_type=INPUT_BUS_USB;
 CHECK(input_register_device(&keyboard));CHECK(input_register_device(&pointer));CHECK(input_arm_pointer(&pointer));
 CHECK(summary().keyboard&&summary().pointer&&!summary().serious);input_reset_all();

 ps2.status=DIAG_OK;CHECK(diag_publish(0,ps2)==1);CHECK(summary().devices==DIAG_MAX_DEVICES);
 for(unsigned i=0;i<DIAG_MAX_LOG+10;++i)diag_log(DIAG_USB,DIAG_WARNING,0,"message");
 CHECK(summary().logs==DIAG_MAX_LOG);CHECK(summary().dropped_logs>0);
 CHECK(diag_get_log(0,&log));CHECK(std::strcmp(log.text,"Brak PS/2")==0); // Boot evidence is retained.
 diag_test_reset();char oversized[512];std::memset(oversized,'X',sizeof(oversized));oversized[511]=0;
 diag_log(DIAG_USB,DIAG_TIMEOUT,DIAG_CODE_HID_INIT,oversized);CHECK(diag_get_log(0,&log));CHECK(log.flags&DIAG_FLAG_TRUNCATED);CHECK(log.text[227]==0);
 CHECK(diag_log_matches(log,1));CHECK(diag_log_matches(log,3));CHECK(!diag_log_matches(log,2));
 diag_test_reset();
 diag_capture_serial("[xHCI] Device Context address=0xFFFF900012345000 pointer=1234567890123 raw=00ABCDEF BAR=0xC00000C000 USBSTS=0xFFFFFFFF");
 CHECK(diag_get_log(0,&log));CHECK(log.flags&DIAG_FLAG_REDACTED);
 CHECK(!std::strstr(log.text,"FFFF900012345000")&&!std::strstr(log.text,"1234567890123")&&!std::strstr(log.text,"00ABCDEF"));
 CHECK(std::strstr(log.text,"BAR=0xC00000C000")&&std::strstr(log.text,"USBSTS=0xFFFFFFFF"));
 auto sensitive=diag_device(DIAG_HDA,"HDA","hda",DIAG_TIMEOUT,true,"DMA=0x12ABC000");diag_publish(0,sensitive);
 CHECK(diag_get_device(0,&d));CHECK(d.flags&DIAG_FLAG_REDACTED);CHECK(!std::strstr(d.message,"12ABC000"));
 std::memset(sensitive.message,'X',sizeof(sensitive.message));diag_publish(0,sensitive);
 CHECK(diag_get_device(0,&d));CHECK(d.message[159]==0&&(d.flags&DIAG_FLAG_TRUNCATED));
 sensitive.category=DIAG_CATEGORY_COUNT;CHECK(!diag_publish(1,sensitive));sensitive.category=DIAG_HDA;
 sensitive.status=DIAG_CRITICAL+1;CHECK(!diag_publish(1,sensitive));sensitive.status=DIAG_TIMEOUT;
 sensitive.transport=DIAG_TRANSPORT_FIRMWARE+1;CHECK(!diag_publish(1,sensitive));
 diag_capture_serial("[USB] ptr=0x1234 address=42 VID=0x0B05 PID=0x1866");CHECK(diag_get_log(2,&log));
 CHECK(!std::strstr(log.text,"0x1234")&&!std::strstr(log.text,"address=42"));
 CHECK(std::strstr(log.text,"VID=0x0B05")&&std::strstr(log.text,"PID=0x1866"));


 // Concurrent readers receive whole records; early store uses no allocation.
 diag_test_reset();std::thread writer([]{for(unsigned i=0;i<4000;++i){auto v=diag_device(DIAG_XHCI,"xhci","xhci",i&1?DIAG_OK:DIAG_TIMEOUT,true,"state");v.value=i;diag_publish(i%8,v);}});
 for(unsigned i=0;i<4000;++i){auto s=summary();CHECK(s.devices<=8);if(s.devices){CHECK(diag_get_device(0,&d));CHECK(d.size==sizeof(d));CHECK(d.name[sizeof(d.name)-1]==0);}}
 writer.join();CHECK(summary().devices==8);
 printf("System diagnostics: %u checks PASS\n",checks);
}
