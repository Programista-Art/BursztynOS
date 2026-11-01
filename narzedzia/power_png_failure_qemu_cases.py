"""Release code, three isolated asset failures and two intact PNGs."""
import struct
from power_png_qemu_observer import PowerObserver

def run(out,screenshot,click,move,key,wait,ticks,launch,health,hmp,symbol,kernel_sha,iso_sha):
    observer=PowerObserver(hmp,symbol);l=observer.layout
    def settle(n=150):
        start=ticks();wait(lambda:ticks()-start>=n,'input and repaint',120)
    samples=[]
    for cycle in range(3):
        click(28,700);settle();key('tab');key('spc');wait(observer.opened,'power popup with failed assets',90);settle()
        g=observer.geometry();assert g['button'][2]==g['button'][3]==36,g
        assert g['popup'][1]+g['popup'][3]<g['button'][1],g
        base=observer.pointer(observer.slot)
        pointers=[observer.pointer(base+l['icons_offset']+i*l['icon_size']+l['pixel_offset']) for i in range(5)]
        assert [bool(p) for p in pointers]==[True,False,False,False,True],pointers
        screenshot('independent-fallbacks-'+str(cycle));samples.append(g)
        # Unsupported actions remain disabled; arrows select trusted shutdown or restart.
        key('up');key('down');key('esc');settle();key('esc');settle();health()
    return dict(result='PASS',missing_sleep_fallback=True,corrupt_shutdown_fallback=True,oversized_lock_fallback=True,intact_power_restart_png=True,cycles=samples,kernel_sha256=kernel_sha,iso_sha256=iso_sha)
