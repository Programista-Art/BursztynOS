#!/usr/bin/env python3
"""Replay the actual previous/current drivers; simulated time, no VM or sleeps."""
import os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
AUDIT=ROOT/'docs/acer-boot-qemu-audit-2026-10-11'
SOURCE=r'''
#define main fixture_main
#include FIXTURE
#undef main
int main(){fixture();clock_step=100000;control_delay=400000000;
    put(registers,0x80+PORTSC_BASE+8*16,PORT_CCS|(1U<<10)|(1U<<17));
#ifdef AFTER
    BootTiming::first_visible_splash=false;
#endif
    const auto entry=mock_now;CHECK(xhci_inicjalizuj_pierwszy());const auto ready=mock_now;
    uint64_t maximum=0;const auto controls_before=control_requests;
    for(unsigned n=0;n<20;++n){const auto b=mock_now;usb_obsluz();maximum=std::max(maximum,mock_now-b);}
    const auto pre_splash=mock_now-entry;
#ifdef AFTER
    CHECK(control_requests==controls_before);CHECK(!xhc.wanted_command);
    BootTiming::framebuffer_published();BootTiming::splash_started();
    for(unsigned n=0;n<1000&&!xhc.devices[2].hid_transfer_oczekiwany;++n){
        mock_now+=10000000;progress_clock();const auto b=mock_now;usb_obsluz();maximum=std::max(maximum,mock_now-b);
    }
    CHECK(maximum<1000000);
#else
    CHECK(maximum>2800000000ULL);
#endif
    CHECK(xhc.devices[2].hid_transfer_oczekiwany);
    printf("controller-ready-ns=%llu pre-first-frame-critical-work-ns=%llu max-service-ns=%llu total-to-HID-armed-ns=%llu controls=%u\n",
        (unsigned long long)(ready-entry),(unsigned long long)pre_splash,(unsigned long long)maximum,
        (unsigned long long)(mock_now-entry),control_requests-controls_before);
    clean();
}
'''
flags=['-std=c++17','-O1','-g','-I'+str(ROOT),'-DBURSZTYN_VIRTIO_HOST_TEST','-DBURSZTYN_XHCI_HOST_TEST','-DBURSZTYN_IDT_HOST_TEST','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-ffunction-sections','-fdata-sections']
sources=['idt.cpp','pci.cpp','sterowniki/usb/xhci_ring.cpp','sterowniki/usb/hid_pointer.cpp','input_device.cpp']
with tempfile.TemporaryDirectory(prefix='bursztyn-before-after-') as temporary:
 work=Path(temporary)
 for source,target in [('xhci-before.cpp.txt','xhci.cpp'),('fixture-before.cpp.txt','fixture.inc'),('boot-phase-before.h.txt','boot_phase_timing.h')]:
  (work/target).write_bytes((AUDIT/source).read_bytes())
 (work/'benchmark.cpp').write_text(SOURCE)
 for mode in ['before','after']:
  fixture=work/'fixture.inc' if mode=='before' else ROOT/'narzedzia/test_xhci_physical_mouse.cpp'
  defines=['-DFIXTURE="'+str(fixture)+'"']
  if mode=='before':defines+=['-DBURSZTYN_XHCI_SOURCE="'+str(work/'xhci.cpp')+'"']
  else:defines+=['-DAFTER=1']
  binary=work/mode
  subprocess.run(['g++',*flags,*defines,str(work/'benchmark.cpp'),*sources,'-Wl,--gc-sections','-o',str(binary)],cwd=ROOT,check=True)
  output=subprocess.check_output([str(binary)],cwd=ROOT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'),text=True)
  (AUDIT/(mode+'-critical-path-simulation.txt')).write_text(output)
  print(mode+': '+output.strip(),flush=True)
print('Before/after deterministic replay: 2 driver variants PASS (100 us simulated clock read increments; 400 ms device control latency; 10 ms scheduler intervals after publication)')
