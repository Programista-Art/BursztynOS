#pragma once
// Both production transports consume this identical VirtualBox-style stream.
// The submit callback must retire a real TD/TRB, service it and assert one requeue.
template<class Submit> void pointer_transport_stream(const HidPointerLayout& layout,InputDevice& pointer,Submit submit){
    const unsigned before=checks;
    for(unsigned i=0;i<600;++i){
        const int values[]={0,16384,32767,16384};
        const int x=values[i%4],y=values[(i/4)%4];const uint8_t buttons=uint8_t(i%8);
        auto report=tablet_report(layout,x,y,buttons);submit(report);
        CHECK(pointer.pointer_state==INPUT_POINTER_PROVEN_ACTIVE);
        CHECK(input_get_pointer_owner()==&pointer);
        CHECK(px==int(int64_t(x)*(width-1)/32767));CHECK(py==int(int64_t(y)*(height-1)/32767));
        CHECK(last_buttons==buttons);
    }
    printf("Shared VirtualBox tablet stream: %u checks PASS\n",checks-before);
}
