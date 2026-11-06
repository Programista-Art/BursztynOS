// Deliberately force the pre-Mimo keyboard mode and inject unsupported REL.
// This tests the historical claim, alongside the full two-queue fixture.
#include "pointing_host_stubs.h"
int main() {
    InputDevice keyboard{},mouse{};
    keyboard.kinds=INPUT_DEVICE_KIND_KEYBOARD;keyboard.bus_type=INPUT_BUS_VIRTIO;
    keyboard.pointer_mode=INPUT_POINTER_RELATIVE;keyboard.device_id=0x700;
    mouse.kinds=INPUT_DEVICE_KIND_MOUSE;mouse.bus_type=INPUT_BUS_VIRTIO;
    mouse.pointer_mode=INPUT_POINTER_RELATIVE;mouse.device_id=0x800;
    CHECK(input_register_device(&keyboard));CHECK(input_register_device(&mouse));
    const InputEvent unsupported[]={{INPUT_EV_REL,INPUT_REL_X,uint32_t(-27)},
        {INPUT_EV_REL,INPUT_REL_Y,44},{INPUT_EV_SYN,INPUT_SYN_REPORT,0}};
    for(unsigned i=0;i<1000;++i) {
        input_dispatch_events(&keyboard,unsupported,3);
        CHECK(mouse.rel_x==0 && mouse.rel_y==0 && motions==0);
    }
#if FORENSIC_BEFORE_MIMO
    CHECK(keyboard.rel_x==-27000 && keyboard.rel_y==44000);
#else
    CHECK(keyboard.rel_x==0 && keyboard.rel_y==0);
#endif
    const InputEvent small[]={{INPUT_EV_REL,INPUT_REL_X,3},
        {INPUT_EV_REL,INPUT_REL_Y,uint32_t(-2)},{INPUT_EV_SYN,INPUT_SYN_REPORT,0}};
    input_dispatch_events(&mouse,small,3);
    CHECK(motions==1 && px==643 && py==358);
    CHECK(input_pointer_is_active(&mouse) && !input_pointer_is_active(&keyboard));
    std::printf("Previous-claim isolation: before_Mimo=%d keyboard_pending=%d,%d mouse_cursor=%d,%d checks=%u PASS\n",
        FORENSIC_BEFORE_MIMO,keyboard.rel_x,keyboard.rel_y,px,py,checks);
    input_reset_all();
}
