"""Real mouse rubber-band and component demo observations, no guest writes."""
import json
import re
import struct
import subprocess
from pathlib import Path


def run(out, screenshot, click, move, key, wait, ticks, launch, health, hmp,
        symbol, kernel_sha, iso_sha):
    root=Path(__file__).resolve().parents[1]
    layout=json.loads((root/'.bsf-test/gui-observer-layout.json').read_text())
    def read(address,n):
        data=[]
        for line in hmp(f'xp /{n}bx 0x{address:x}').splitlines():
            if ':' in line:data.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',line.split(':',1)[1]))
        assert len(data)==n
        return bytes(data)
    def user(pid,address,n):
        cr3=struct.unpack('<Q',read(symbol('tablica_procesow')+pid*144+0x38,8))[0]
        def physical(va):
            table=cr3 & 0x000ffffffffff000
            for shift in (39,30,21,12):
                entry=struct.unpack('<Q',read(table+((va>>shift)&511)*8,8))[0]
                assert entry&1,('unmapped observer',pid,hex(va))
                if shift==12 or (shift in (30,21) and entry&128):
                    return (entry&0x000ffffffffff000&~((1<<shift)-1))+(va&((1<<shift)-1))
                table=entry&0x000ffffffffff000
        data=b''
        while n:
            count=min(n,4096-(address&4095));data+=read(physical(address),count);address+=count;n-=count
        return data
    def address(elf,name,pid=1):
        symbols=subprocess.check_output(['nm','-C',str(elf)],text=True)
        raw=int(re.search(r'^([0-9a-f]+) [bBdD] '+re.escape(name)+r'$',symbols,re.M)[1],16)
        base=struct.unpack('<Q',read(symbol('uklady_procesow')+pid*72,8))[0] or 0x1000000
        return base+(raw-0x1000000 if raw>=0x1000000 else raw)
    titles=symbol('(anonymous namespace)::tytul_okna');layers=symbol('tablica_warstw')
    def window():
        names=read(titles,16*48);meta=read(layers,16*40)
        for pid in range(1,16):
            if names[pid*48:(pid+1)*48].startswith(b'BursztynUI - Pokaz'):
                _,z,x,y,w,h=struct.unpack_from('<6i',meta,pid*40)
                return dict(pid=pid,x=x,y=y,w=w,h=h)
    wm=out/'wm.elf' if (out/'wm.elf').exists() else root/'menedzer_okien.elf'
    gesture=address(wm,'(anonymous namespace)::desktop_gesture',1)
    def band():return list(struct.unpack('<4i',user(1,gesture+layout['gesture_band'],16)))
    click(1100, 610)
    screenshot('selection-before')
    move(8, 310)
    hmp('mouse_button 1')
    start = ticks()
    wait(lambda: ticks() > start + 8, 'desktop press', 30)
    frames = []
    # Remain pressed throughout. Cross icon silhouettes and their labels in
    # both directions; repeated stationary captures observe reconstruction.
    for i, (x, y) in enumerate([(430, y) for y in
                               (280, 220, 170, 120, 92, 85, 78, 72, 65,
                                72, 78, 85, 92, 120, 170, 220, 280)]):
        move(x, y)
        for j in range(3):
            name = f'selection-{i:02}-{j}'
            im=screenshot(name)
            ink=[]
            for left,right in [(32,130),(150,246),(275,365),(395,485)]:
                ink.append(sum(min(p)>150 and max(p)-min(p)<60 for p in im.crop((left,58,right,96)).getdata()))
            assert min(ink)>15,('blank label frame',name,ink)
            frames.append(dict(name=name, x=x, y=y, tick=ticks(),band=band(),label_ink=ink))
        wait(lambda:band()==[8,min(y,310),423,abs(310-y)+1],'completed rubber-band update',60,interval=.01)
        health()
    hmp('mouse_button 0')
    move(1100, 610)
    screenshot('selection-released')
    launch('demo')
    wait(lambda:window() is not None,'component demo window',180,interval=.1)
    win=window()
    demo=address(root/'demo_ui.elf','g_demo',win['pid'])
    slider=struct.unpack('<Q',user(win['pid'],demo+11*8,8))[0]
    assert slider
    def slider_value():return struct.unpack('<i',user(win['pid'],slider+layout['slider_value'],4))[0]
    rect=struct.unpack('<4i',user(win['pid'],slider+layout['control_rect'],16))
    x,y,w,h=rect;x+=win['x'];y+=win['y']
    assert w>0 and h>0
    screenshot('demo-initial')
    values=[]
    move(x+w//2,y+h//2);hmp('mouse_button 1')
    for i,offset in enumerate((w//4,w//2,3*w//4,w//2,w//4,w//2)):
        move(x+offset,y+h//2)
        im=screenshot(f'slider-drag-{i}')
        values.append(slider_value())
    hmp('mouse_button 0')
    # The mouse establishes keyboard focus through the actual event router.
    value=slider_value();key('right')
    wait(lambda:slider_value()!=value,'slider keyboard update',30)
    screenshot('slider-keyboard')
    assert len(set(values))>=3,values
    health()
    (out/'selection-frames.json').write_text(json.dumps(frames, indent=2))
    return dict(result='PASS', mode='gui-regressions', frames=len(frames),slider_values=values,
                kernel_sha256=kernel_sha, iso_sha256=iso_sha)
