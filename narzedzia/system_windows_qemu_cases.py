"""Native release acceptance. QMP reads kernel state, never writes guest memory."""
import json
import re
import struct
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def run(out,screenshot,click,move,key,wait,ticks,launch,health,hmp,symbol,kernel_sha,iso_sha,phase="full"):
    def read(address,n):
        dump=hmp(f'xp /{n}bx 0x{address:x}')
        result=[]
        for line in dump.splitlines():
            if ':' in line:result.extend(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]+)',line.split(':',1)[1]))
        if len(result)!=n:raise AssertionError('Incomplete read-only observer')
        return bytes(result)
    layout_exe=ROOT/'.bsf-test/qemu-process-layout'
    subprocess.run(['g++','-std=c++17',str(ROOT/'narzedzia/qemu_process_layout.cpp'),'-o',str(layout_exe)],check=True)
    layout=json.loads(subprocess.check_output([str(layout_exe)],text=True))
    processes=symbol('tablica_procesow')
    layers=symbol('tablica_warstw')
    titles=symbol('(anonymous namespace)::tytul_okna')
    states=symbol('(anonymous namespace)::stan_okna')
    visibility=symbol('(anonymous namespace)::widocznosc_warstwy')
    completed_frames=symbol('(anonymous namespace)::klatka_gotowa')
    focus_address=symbol('aktywny_pid_gui')
    scale_address=symbol('(anonymous namespace)::skala')
    config_address=symbol('bsf_config_request(proces*, unsigned long, unsigned long, unsigned long)::active')
    def config():return struct.unpack('<6I',read(config_address,24))
    def scale():return struct.unpack('<I',read(scale_address,4))[0]
    def focus():return struct.unpack('<i',read(focus_address,4))[0]
    def windows():
        meta=read(layers,16*40);names=read(titles,16*48);status=read(states,16*4);visible=read(visibility,16)
        result=[]
        for pid in range(1,16):
            title=names[pid*48:(pid+1)*48].split(b'\0')[0].decode('utf-8','replace')
            if title and meta[pid*40+32]:
                _,z,x,y,w,h=struct.unpack_from('<6i',meta,pid*40)
                result.append(dict(pid=pid,title=title,z=z,x=x,y=y,w=w,h=h,state=struct.unpack_from('<I',status,pid*4)[0],visible=bool(visible[pid])))
        return result
    def win(title):return next((w for w in windows() if w['title']==title),None)
    def idle(title):
        def blocked():
            w=win(title)
            if not w:return False
            address=processes+w['pid']*layout['process_size']+layout['state_offset']
            return struct.unpack('<i',read(address,4))[0] in (4,5)
        wait(blocked,title+' consumed queued input and completed repaint',240,interval=.1)
    def settle(n=250):
        start=ticks();wait(lambda:ticks()-start>=n,'native repaint',120)
    def presented(title):
        w=win(title)
        if not w or not w['visible'] or not read(completed_frames+w['pid'],1)[0]:return False
        im=screenshot('window-ready-probe');r,g,b=im.getpixel((w['x']+w['w']-25,w['y']+22))
        return r>150 and g<130 and b<130
    def opened(title,query):
        launch(query);wait(lambda:presented(title),title+' complete frame presented',240,interval=1);idle(title)
        return win(title)
    def chrome(title,offset):
        w=win(title);click(w['x']+w['w']-offset,w['y']+14);settle()
    def close(title):
        activate(title)
        chrome(title,16);wait(lambda:win(title) is None,title+' closed',120)
    def activate(title):
        ws=windows();index=next(i for i,w in enumerate(ws) if w['title']==title)
        click(66+index*116+56,700);wait(lambda:focus()==win(title)['pid'],title+' taskbar focus',90);idle(title)
    def geo(px):return json.loads((ROOT/f'.bsf-test/support-layout-{px}.json').read_text())
    def hit(rect):
        w=win('Ustawienia');click(w['x']+rect[0]+rect[2]//2,w['y']+28+rect[1]+rect[3]//2)
    def page(index,px):
        hit(geo(px)['nav'][index]);idle('Ustawienia')
        if index==0:
            # The doubled System title crosses the ordinary heading's blank
            # inter-line band. This confirms the page, not merely button focus.
            def system_visible():
                w=win('Ustawienia');x=w['x']+geo(px)['table'][0]-1
                y=w['y']+28+14+{12:14,16:19,24:28}[px]
                im=screenshot('system-title-probe');area=im.crop((x,y,x+350,y+10))
                return sum(1 for r,g,b in area.getdata() if min(r,g,b)>120 and max(r,g,b)-min(r,g,b)<40)>30
            wait(system_visible,'System page content visible',180,interval=1)
    def apply_size(old,new):
        page(3,old);hit(geo(old)['size']);idle('Ustawienia')
        w=win('Ustawienia');rect=geo(old)['size'];line={12:14,16:19,24:28}[old];row=max(22,line+6)
        click(w['x']+rect[0]+rect[2]//2,w['y']+28+rect[1]+rect[3]+6+{12:0,16:2,24:5}[new]*row+row//2)
        idle('Ustawienia');hit(geo(old)['apply']);wait(lambda:config()[3]==new,'live font Apply '+str(new),180)
        idle('Ustawienia');page(0,new)

    # Reproduce the formerly failing order before any newer Settings window exists.
    display=opened('Ustawienia ekranu','ekranu')
    settings=opened('Ustawienia','ustawienia')
    assert display['pid']!=settings['pid']
    evidence=[dict(stage='display-then-settings',windows=windows())]
    if phase!="late":
        # Existing exact-path singleton requests must focus their own window.
        for title,query in [('Ustawienia ekranu','ekranu'),('Ustawienia','ustawienia')]:
            before={w['pid'] for w in windows()};launch(query);settle(400)
            assert {w['pid'] for w in windows()}==before
            assert focus()==win(title)['pid']
        activate('Ustawienia ekranu');activate('Ustawienia')
        # Native keyboard switch must distinguish the two retained processes.
        key('alt','tab');settle();assert focus()==display['pid']
        key('alt','tab');settle();assert focus()==settings['pid']
        screenshot('settings-both-processes')

    # Compare actual outside-corner pixels against the lower scene obtained by
    # minimizing this window. This includes the other native window underneath.
    corner_results=[]
    def corners(label):
        wait(lambda:presented('Ustawienia'),'Settings complete corner frame',180,interval=1)
        w=win('Ustawienia');move(1100,660);settle();top=screenshot(label)
        chrome('Ustawienia',64);assert win('Ustawienia')['state']==2
        lower=screenshot(label+'-lower');activate('Ustawienia')
        restored=screenshot(label+'-restored')
        checked=0
        # Default theme radius=10; independent circle coverage oracle.
        radius=10
        for dy in range(radius):
            inset=radius-int((radius*radius-(radius-1-dy)**2)**.5)
            for dx in range(inset):
                for x in (w['x']+dx,w['x']+w['w']-1-dx):
                    for y in (w['y']+dy,w['y']+w['h']-1-dy):
                        assert top.getpixel((x,y))==lower.getpixel((x,y)),(label,x,y,'corner mismatch')
                        assert restored.getpixel((x,y))==lower.getpixel((x,y)),(label,x,y,'restore mismatch')
                        checked+=1
        assert checked>0
        corner_results.append(dict(stage=label,compared_pixels=checked,bounds=w))
    if phase!="late":
        corners('settings-corners-initial')
        # Move with ordinary titlebar capture, retaining both independent apps.
        w=win('Ustawienia');move(w['x']+200,w['y']+14);hmp('mouse_button 1');settle(50)
        move(w['x']+240,w['y']+44);hmp('mouse_button 0');settle(400)
        assert (win('Ustawienia')['x'],win('Ustawienia')['y'])!=(w['x'],w['y'])
        corners('settings-corners-moved')
        chrome('Ustawienia',40);wait(lambda:win('Ustawienia')['state']==1,'maximized',120);settle()
        def square_presented():
            if not presented('Ustawienia'):return False
            im=screenshot('maximized-ready-probe');w=win('Ustawienia');color=im.getpixel((20,0))
            return im.getpixel((0,0))==color==im.getpixel((w['w']-1,0)) and color[0]>100 and color[1]>50 and color[2]<100
        wait(square_presented,'complete maximized square-edge frame',240,interval=1)
        w=win('Ustawienia');im=screenshot('settings-maximized-square')
        assert w['x']==0 and w['y']==0
        # The accent top strip must reach the screen edges without a rounded gap.
        assert im.getpixel((0,0))==im.getpixel((20,0))
        assert im.getpixel((w['w']-1,0))==im.getpixel((20,0))
        chrome('Ustawienia',40);wait(lambda:win('Ustawienia')['state']==0,'restored',120);settle()
        corners('settings-corners-after-maximize')

        # Reopen resets geometry and confirms System is the default page again.
        close('Ustawienia');assert win('Ustawienia ekranu') is not None
        chrome('Ustawienia ekranu',64) # Keep the process alive while reaching desktop shortcuts.
        move(200,151);hmp('mouse_button 2');settle(64);hmp('mouse_button 0');settle(125)
        screenshot('desktop-settings-context');click(225,166)
        wait(lambda:presented('Ustawienia'),'desktop Settings launch',240,interval=1);settle(250)
        assert config()[3]==16
        activate('Ustawienia ekranu');activate('Ustawienia')
    if phase=="core":
        health()
        (out/'concurrent-processes.json').write_text(json.dumps(evidence,indent=2)+'\n')
        (out/'corner-pixels.json').write_text(json.dumps(corner_results,indent=2)+'\n')
        return dict(result='PASS',mode='system-windows-core',taskbar=True,alt_tab=True,singleton_per_executable=True,
                    desktop_launch=True,corner_checks=corner_results,no_guest_fault=True,kernel_sha256=kernel_sha,iso_sha256=iso_sha)
    captures=[]
    for px in (16,12,24):
        if config()[3]!=px:apply_size(config()[3],px)
        move(1100,660);captures.append('settings-system-'+str(px));screenshot(captures[-1])
        # Scroll the System page using native wheel input; verify distinct content.
        w=win('Ustawienia');move(w['x']+600,w['y']+400)
        before=screenshot('system-scroll-before-probe').crop((w['x']+250,w['y']+100,w['x']+800,w['y']+480)).tobytes()
        for _ in range(12):
            hmp('mouse_move 0 0 -1');settle(64)
        idle('Ustawienia');after=screenshot('settings-system-scrolled-'+str(px))
        if px!=12:assert after.crop((w['x']+250,w['y']+100,w['x']+800,w['y']+480)).tobytes()!=before
        assert win('Ustawienia ekranu') is not None
    # Restore the baseline font for the dedicated clock/Support regression run.
    apply_size(24,16)
    activate('Ustawienia ekranu')
    before_scale=win('Ustawienia')
    for target in (125,100):
        w=win('Ustawienia ekranu');factor=scale()/100
        click(w['x']+int(300*factor),w['y']+int(201*factor));idle('Ustawienia ekranu')
        # Select a measured popup row through native pointer input. Direction
        # key repeats must not silently substitute a different target scale.
        row=max(round(22*factor),{1:19,1.25:24}[factor]+round(6*factor))
        index=1 if target==125 else 0
        click(w['x']+int(300*factor),w['y']+round(216*factor)+round(6*factor)+index*row+row//2)
        idle('Ustawienia ekranu')
        click(w['x']+int(65*factor),w['y']+w['h']-int(23*factor))
        wait(lambda:scale()==target,'Display Settings Apply '+str(target),180);idle('Ustawienia ekranu');idle('Ustawienia')
        assert win('Ustawienia') is not None
        screenshot('both-settings-scale-'+str(target))
    assert win('Ustawienia')['pid']==before_scale['pid']
    activate('Ustawienia');corners('settings-corners-after-display-scale')
    close('Ustawienia ekranu');assert win('Ustawienia') is not None
    evidence.append(dict(stage='settings-survives-display-close',windows=windows()))
    opened('Ustawienia ekranu','ekranu');close('Ustawienia')
    assert win('Ustawienia ekranu') is not None
    close('Ustawienia ekranu')
    # Reverse initial launch order as well.
    opened('Ustawienia','ustawienia');opened('Ustawienia ekranu','ekranu')
    evidence.append(dict(stage='settings-then-display',windows=windows()))
    close('Ustawienia')
    # Direct shell launch is intentionally unsupported: its PZB 4 process may
    # not create the PZB 3 Settings app (nor privileged Display Settings). Preserve
    # that security boundary rather than granting privileges for a UI test.
    close('Ustawienia ekranu')
    health()
    (out/'concurrent-processes.json').write_text(json.dumps(evidence,indent=2)+'\n')
    (out/'corner-pixels.json').write_text(json.dumps(corner_results,indent=2)+'\n')
    return dict(result='PASS',mode='system-windows-'+phase,font_sizes=[12,16,24],taskbar=True,alt_tab=phase!='late',
                singleton_per_executable=phase!='late',both_launch_orders=True,both_close_orders=True,desktop_launch=phase!='late',shell_launch_supported=False,shell_launch_restriction='PZB 4 cannot create more-trusted system apps',display_scale_change=[125,100],
                corner_checks=corner_results,system_screenshots=captures,no_guest_fault=True,
                kernel_sha256=kernel_sha,iso_sha256=iso_sha)
