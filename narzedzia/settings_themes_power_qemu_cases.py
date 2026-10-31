"""Release UI acceptance: real input, screenshots and read-only kernel observations."""
import json
import re
import struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def run(out,screenshot,click,move,key,wait,ticks,launch,health,hmp,symbol,kernel_sha,iso_sha):
    def read(address,n):
        data=[]
        for line in hmp(f'xp /{n}bx 0x{address:x}').splitlines():
            if ':' in line:data.extend(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]+)',line.split(':',1)[1]))
        assert len(data)==n
        return bytes(data)
    theme_address=symbol('g_aktywny_motyw')
    preview_address=symbol('g_podglad_aktywny')
    popup_address=symbol('(anonymous namespace)::application_popup')
    titles=symbol('(anonymous namespace)::tytul_okna');layers=symbol('tablica_warstw')
    def theme():return read(theme_address+140,32).split(b'\0')[0].decode()
    def preview():return bool(read(preview_address,1)[0])
    def popup():return bool(read(popup_address,1)[0])
    def window():
        names=read(titles,16*48);meta=read(layers,16*40)
        for pid in range(1,16):
            if names[pid*48:(pid+1)*48].split(b'\0')[0]==b'Ustawienia':
                _,z,x,y,w,h=struct.unpack_from('<6i',meta,pid*40)
                return dict(pid=pid,x=x,y=y,w=w,h=h)
    def settle(count=80):
        start=ticks();wait(lambda:ticks()-start>=count,'completed UI repaint',90,interval=.05)
    geometry=json.loads((ROOT/'.bsf-test/support-layout-16.json').read_text())
    def hit(rect):
        w=window();click(w['x']+rect[0]+rect[2]//2,w['y']+28+rect[1]+rect[3]//2);settle()
    def select_theme(index):
        w=window();rect=geometry['themes_list']
        # Sans 16 release rows are 25 px (19 px font + 6 px list padding).
        click(w['x']+rect[0]+30,w['y']+28+rect[1]+5+index*25+12);settle()
    def opened():
        launch('ustawienia');wait(lambda:window() is not None,'native Settings',180,interval=.1);settle(250)
        hit(geometry['nav'][2]);screenshot('settings-themes')
    def start_power(name,hover=False):
        click(28,700);wait(popup,'Start open',90);settle()
        if hover:
            # Fixed lower section in the default 1280x720, 100%, Sans 16 release.
            move(180,652);settle(120)
        else:key('tab');key('spc');settle()
        assert popup();im=screenshot(name)
        color=struct.unpack('<I',read(theme_address+16,4))[0]
        rgb=((color>>16)&255,(color>>8)&255,color&255)
        assert im.getpixel((15,500))==rgb,('Start theme background',name,rgb,im.getpixel((15,500)))
        assert im.getpixel((22,550))==rgb,('power popup theme background',name,rgb,im.getpixel((22,550)))
    opened();original=theme();samples=[]
    # Observe the actual countdown pixels while the desktop uses the same theme.
    # A host test checks every second and the exact dirty rectangle separately.
    select_theme(0);hit(geometry['themes_preview']);wait(preview,'countdown preview active',120)
    move(1100,600);settle()
    def status_pixels(name):
        w=window();x,y,width,height=geometry['themes_status']
        return screenshot(name).crop((w['x']+x,w['y']+28+y,w['x']+x+width,w['y']+28+y+height))
    first=status_pixels('countdown-first');assert len(set(first.getdata()))>2,'countdown label must be visible'
    settle(550)
    second=status_pixels('countdown-next');assert first.tobytes()!=second.tobytes(),'visible countdown must advance'
    hit(geometry['themes_cancel']);wait(lambda:not preview(),'countdown explicit restore',120)
    assert theme()==original
    # No-result text occupies the first app row and clears when matches return.
    click(28,700);wait(popup,'Start for empty search',90)
    for char in 'zzzzzz':key(char)
    settle();empty_image=screenshot('start-empty-search')
    start_layout=json.loads((ROOT/'.bsf-test/start-polish-layout-16.json').read_text())
    x,y,width,height=start_layout['results'];row=start_layout['row']
    top_box=(x+8,y,x+width-8,y+2*row)
    assert len(set(empty_image.crop(top_box).getdata()))>2,'empty message must render at the top'
    middle=empty_image.crop((x+8,y+height//2,x+width-8,y+height//2+row))
    assert len(set(middle.getdata()))==1,'empty message must not be vertically centered'
    key('ctrl','a');key('backspace');settle();restored=screenshot('start-search-restored')
    assert restored.crop(top_box).tobytes()!=empty_image.crop(top_box).tobytes(),'matching rows must replace the message'
    key('esc');wait(lambda:not popup(),'Start dismissed after Escape',90)
    # On-disk registration order; list de-duplicates the compatibility aliases.
    themes=['bursztynnocny','baltyckanoc','grafit','bursztynklasyczny','bursztynjasny']
    # Use the list's actual keyboard selection, so aliases and Polish names are irrelevant.
    for i,target in enumerate(themes):
        print("Theme acceptance:",target,flush=True)
        select_theme(i)
        hit(geometry['themes_preview']);wait(lambda:preview() and theme()==target,'preview '+target,120,interval=.05)
        screenshot('preview-'+target);start_power('power-'+target)
        wait(lambda:not preview(),'kernel 15-second rollback '+target,180,interval=.1)
        assert theme()==original
        color=struct.unpack('<I',read(theme_address+16,4))[0];rgb=((color>>16)&255,(color>>8)&255,color&255)
        def repainted():
            im=screenshot('rollback-palette-probe')
            return im.getpixel((15,500))==rgb and im.getpixel((22,550))==rgb
        wait(repainted,'Start and power popup repainted after rollback '+target,30,interval=.1)
        screenshot('rollback-start-'+target)
        print('Rollback observed; closing power',flush=True)
        key('esc');settle();assert popup(),'Escape must leave Start open after closing power popup'
        key('esc');wait(lambda:not popup(),'Start dismissed after Escape',90)
        samples.append(dict(id=target,rollback=True));health()
    # Apply cancels the deadline; reopening reads the same active and persisted choice.
    select_theme(2);hit(geometry['themes_preview'])
    wait(lambda:preview() and theme()=='grafit','preview before Apply',120,interval=.05)
    hit(geometry['themes_apply']);wait(lambda:not preview() and theme()=='grafit','Apply commit',120,interval=.1)
    start=ticks();wait(lambda:ticks()-start>4000,'no delayed rollback after Apply',180,interval=.1);assert theme()=='grafit'
    w=window();click(w['x']+w['w']-16,w['y']+14);wait(lambda:window() is None,'Settings close',90)
    opened();assert theme()=='grafit';screenshot('settings-persisted')
    select_theme(1);hit(geometry['themes_preview'])
    wait(preview,'preview before close',120);w=window();click(w['x']+w['w']-16,w['y']+14)
    wait(lambda:window() is None and not preview(),'close cancels preview',120);assert theme()=='grafit'
    start_power('power-hover',hover=True);key('esc');settle();key('esc');settle()
    start_power('power-keyboard');key('up');key('down');key('esc');settle();assert popup();key('esc');wait(lambda:not popup(),'Start dismissed after Escape',90)
    start_power('power-outside');click(1150,600);settle();assert not popup()
    health()
    return dict(result='PASS',mode='settings-themes-power',themes=samples,apply_cancels_rollback=True,
                visible_countdown=True,empty_search_top_row=True,settings_reopen=True,close_rollback=True,power_mouse_keyboard=True,unsupported_actions_disabled=True,
                kernel_sha256=kernel_sha,iso_sha256=iso_sha)
