"""Ordinary release UI modesets; observations never write guest state."""
import json,re,struct,subprocess
from pathlib import Path

def run(out,screenshot,click,move,key,wait,ticks,launch,health,hmp,symbol,kernel_sha,iso_sha,input_kind='usb',q=None):
    from power_png_qemu_observer import PowerObserver
    power_observer=PowerObserver(hmp,symbol)
    def read(address,n):
        data=[]
        for line in hmp(f'xp /{n}bx 0x{address:x}').splitlines():
            if ':' in line:data.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',line.split(':',1)[1]))
        assert len(data)==n
        return bytes(data)
    def u32(name):return struct.unpack('<I',read(symbol(name),4))[0]
    def cursor():return [u32('mysz_x'),u32('mysz_y')]
    def geometry():
        values=struct.unpack('<16I',read(symbol('committed_geometry'),64))
        return dict(width=values[2],height=values[3],scale=values[4],generation=values[1],
                    work_area=list(values[5:9]),taskbar=list(values[9:13]),mouse_bounds=list(values[13:15]),
                    desktop_generation=u32('(anonymous namespace)::desktop_geometry_generation'),cursor=cursor())
    root=Path(__file__).resolve().parents[1]
    observer=json.loads((root/'.bsf-test/display-observer-layout.json').read_text())
    wm_symbols=subprocess.check_output(['nm','-C',str(out/'wm.elf' if (out/'wm.elf').exists() else root/'menedzer_okien.elf')],text=True)
    wm_address=int(re.search(r'^([0-9a-f]+) [bBdD] \(anonymous namespace\)::desktop_layout$',wm_symbols,re.M)[1],16)
    def desktop():
        # Read-only page-table walk of PID 1. Process ABI offsets are checked in pzb.h.
        cr3=struct.unpack('<Q',read(symbol('tablica_procesow')+144+0x38,8))[0]
        cache={}
        def physical(va):
            table=cr3 & 0x000ffffffffff000
            for shift in (39,30,21,12):
                slot=table+((va>>shift)&511)*8
                if slot not in cache:cache[slot]=struct.unpack('<Q',read(slot,8))[0]
                entry=cache[slot];assert entry&1,('unmapped desktop',hex(va),hex(cr3))
                if shift==12 or (shift in (30,21) and entry&128):
                    return (entry&0x000ffffffffff000&~((1<<shift)-1))+(va&((1<<shift)-1))
                table=entry&0x000ffffffffff000
        def user(va,n):
            data=b''
            while n:
                count=min(n,4096-(va&4095));data+=read(physical(va),count);va+=count;n-=count
            return data
        count=struct.unpack('<i',user(wm_address+observer['count_offset'],4))[0]
        assert 16<=count<=144,count
        x0,y0,cw,ch=struct.unpack('<4i',user(wm_address+observer['origin_offset'],16))
        data=user(wm_address,count*observer['position_size'])
        anchors=[]
        for i in range(count):
            offset=i*observer['position_size']
            col,row,span,ox,oy=struct.unpack_from('<5i',data,offset)
            anchor=struct.unpack_from('<5i',data,offset+observer['anchor_offset'])
            anchors.append(dict(index=i,canonical=list(anchor),placed=bool(data[offset+20]),
                                cell=[x0+col*cw+ox,y0+row*ch+oy]))
        return dict(origin=[x0,y0],pitch=[cw,ch],anchors=anchors)
    layers=symbol('tablica_warstw');titles=symbol('(anonymous namespace)::tytul_okna')
    def window():
        names=read(titles,16*48);meta=read(layers,16*40)
        for pid in range(1,16):
            if names[pid*48:(pid+1)*48].split(b'\0')[0].decode('utf-8','replace')=='Ustawienia ekranu':
                _,z,x,y,w,h=struct.unpack_from('<6i',meta,pid*40)
                return dict(pid=pid,x=x,y=y,w=w,h=h)
    def edge(x,y):
        # Physical protocols receive bounded deltas; VirtIO carries a full
        # signed displacement. Wait only for input consumption, never a timer.
        for _ in range(65):
            old=cursor()
            if old==[x,y]:return
            step=8192 if input_kind=='virtio' else 100
            hmp(f'mouse_move {max(-step,min(step,x-old[0]))} {max(-step,min(step,y-old[1]))}')
            try:
                wait(lambda:cursor()==[x,y] if input_kind=='virtio' else cursor()!=old,
                     'input displacement consumed after commit',10,interval=.01)
            except TimeoutError:
                evidence=dict(target=[x,y],before=old,current=geometry(),ticks=ticks())
                (out/'edge-failure.json').write_text(json.dumps(evidence,indent=2))
                screenshot('edge-failure');raise AssertionError(evidence)
        raise AssertionError(('unreachable edge',x,y,cursor(),geometry()))
    if 'BURSZTYN_DISPLAY_SMOKE' not in __import__('os').environ:
        for name in ['geom1','geom2']:
            move(1100,500);hmp('mouse_button 2');hmp('mouse_button 0')
            key('home');key('ret')
            for code in name:key(code)
            key('ret')
    launch('ekranu');wait(lambda:window() is not None,'Display Settings window',180)
    def presented():
        win=window();im=screenshot('display-ready-probe')
        r,g,b=im.getpixel((win['x']+win['w']-25,win['y']+22))
        return r>150 and g<130 and b<130
    wait(presented,'Display Settings first complete frame',180,interval=.1)
    if 'BURSZTYN_TASKBAR_LOWER' in __import__('os').environ:
        win=window();old_y=win['y'];destination=geometry()['work_area'][3]-win['h']-8
        move(win['x']+win['w']//2,old_y+14);hmp('mouse_button 1')
        started=ticks();wait(lambda:ticks()>started+8,'titlebar capture',20,interval=.01)
        move(win['x']+win['w']//2,destination+14);hmp('mouse_button 0')
        wait(lambda:abs(window()['y']-destination)<=2,'Display Settings near lower work-area edge',60,interval=.01)
        screenshot('display-lower-window')
    before_image=screenshot('display-initial')
    bar_color=before_image.getpixel((before_image.width-2,before_image.height-2))
    first_frames=[]
    smoke='BURSZTYN_DISPLAY_SMOKE' in __import__('os').environ
    baseline=None if smoke else desktop();seen={(1280,720,100):baseline};records=[]
    targets=[(1920,1080,125),(1920,1080,100),(1920,1080,125),(1920,1080,150),
             (1920,1080,200),(1920,1080,150),(1920,1080,125),(1920,1080,100),
             (1280,720,100),(1920,1080,100),(1280,720,125),(1920,1080,100),(1280,720,100)]
    if 'BURSZTYN_DISPLAY_SMOKE' in __import__('os').environ:
        targets=[(1920,1080,100),(1920,1080,125),(1920,1080,150),(1920,1080,200),(1280,720,200),(1280,720,150),(1280,720,125),(1280,720,100)]
    if 'BURSZTYN_BSF_MATRIX' in __import__('os').environ:
        targets=[(800,600,100),(1024,768,100),(1280,720,100),(1366,768,100),(1920,1080,125),
                 (1920,1080,150),(1920,1080,175),(1920,1080,200),(2560,1440,100),(1280,720,100)]
    if __import__('os').environ.get('BURSZTYN_BSF_MATRIX')=='1366':targets=[(1366,768,100),(2560,1440,100),(1280,720,100)]
    for w,h,s in targets:
        old=geometry();win=window();factor=old['scale']/100
        # Select exact popup rows through the native list. No direction-key
        # repeat or duplicate key representation can alter the target.
        layout=json.loads((root/f'.bsf-test/display-layout-{old["scale"]}.json').read_text())
        row=layout['row']
        popup_address=symbol('(anonymous namespace)::application_popup')
        if (w,h)!=(old['width'],old['height']):
            mode=layout['mode'];click(win['x']+mode[0]+mode[2]//2,win['y']+mode[1]+mode[3]//2)
            wait(lambda:read(popup_address,1)[0]!=0,'native mode popup',30,interval=.01)
            popup=struct.unpack('<4i',read(popup_address+12,16))
            if 'BURSZTYN_BSF_MATRIX' in __import__('os').environ:
                assert q is not None
                move(popup[0]+popup[2]//2,popup[1]+popup[3]//2)
                q.call('input-send-event',{'events':[{'type':'btn','data':{'button':'wheel-up','down':down}} for _ in range(8) for down in (True,False)]})
                started=ticks();wait(lambda:ticks()-started>=80,'resolution list scrolls to first row',60)
                index={(800,600):0,(1024,768):1,(1280,720):2,(1366,768):4,(1920,1080):6,(2560,1440):7}[(w,h)]
                screenshot('mode-popup-'+str(len(records)))
                click(popup[0]+round(200*factor),popup[1]+round(6*factor)+index*row+row//2)
            else:
                screenshot('mode-popup-'+str(len(records)))
                click(popup[0]+round(200*factor),popup[1]+round(6*factor)+({(1280,720):2,(1920,1080):6}[(w,h)]-2)*row+row//2)
            wait(lambda:read(popup_address,1)[0]==0,'mode popup selection committed',30,interval=.01)
        if s!=old['scale']:
            scale=layout['scale'];click(win['x']+scale[0]+scale[2]//2,win['y']+scale[1]+scale[3]//2)
            wait(lambda:read(popup_address,1)[0]!=0,'native scale popup',30,interval=.01)
            popup=struct.unpack('<4i',read(popup_address+12,16))
            click(popup[0]+round(200*factor),popup[1]+round(6*factor)+(s-100)//25*row+row//2)
            wait(lambda:read(popup_address,1)[0]==0,'scale popup selection committed',30,interval=.01)
        x=win['x']+round(65*factor);y=win['y']+win['h']-round(23*factor)
        move(x,y);hmp('mouse_button 1')
        wait(lambda:read(symbol('zaktualizuj_mysze_wspolne(long, long, unsigned char, char const*)::poprzednie_przyciski_gui'),1)[0]&1,'mouse press consumed',10,interval=.01)
        # Button release triggers Apply; no click helper sleep after this point.
        hmp('mouse_button 0')
        wait(lambda:geometry()['generation']!=old['generation'],'display commit generation',180,interval=.01)
        committed=geometry();immediate=screenshot('immediate-apply-'+str(len(records)))
        assert (committed['width'],committed['height'],committed['scale'])==(w,h,s),committed
        # Validate visible pixels as well as the canonical metadata. Looking at
        # DisplayGeometry alone missed the retained old-position strip.
        def observe_frame(image,state,label):
            assert state['taskbar'][1]+state['taskbar'][3]==state['height'],state
            assert state['taskbar'][1]==state['height']-state['taskbar'][3],state
            assert image.size==(state['width'],state['height']),state
            assert image.getpixel((state['width']-2,state['height']-2))==bar_color,('missing bottom taskbar',label,state)
            client=window()
            if label.startswith('wm-'):
                assert client['y']+client['h']<=state['work_area'][3],('window outside work area',client,state)
            first_frames.append(dict(label=label,geometry=state,bottom_color=list(bar_color),window=client))
        observe_frame(immediate,committed,'immediate-'+str(len(records)))
        record=dict(committed=committed);records.append(record)
        if (old['width'],old['height'])!=(w,h):key('ret')
        # Native Enter confirms without depending on asynchronous button layout.
        # Slow host observation must not consume the guest
        # confirmation deadline before input is sent.
        win=window();factor=s/100
        wait(lambda:window()['w']==round(640*factor),'Display Settings resized for committed scale',120,interval=.01)
        win=window()
        layout=json.loads((Path(__file__).resolve().parents[1]/f'.bsf-test/display-layout-{s}-pending.json').read_text())
        wait(lambda:geometry()['desktop_generation']==committed['generation'],'desktop generation acknowledgement',120,interval=.01)
        observe_frame(screenshot('first-wm-'+str(len(records))),geometry(),'wm-'+str(len(records)))
        edges=[]
        for px,py in [(0,h-1),(w-1,h-1),(0,0),(w-1,0),(w//2,h-1)]:
            edge(px,py);edges.append(cursor())
        after_edges=geometry()
        assert (after_edges['generation'],after_edges['width'],after_edges['height'],after_edges['scale'])==(committed['generation'],w,h,s),after_edges
        record.update(edges=edges,after_edges=after_edges)
        (out/'first-frames.json').write_text(json.dumps(first_frames,indent=2))
        if not smoke:
            current=desktop();record['desktop']=current
            assert [a['canonical'] for a in current['anchors']]==[a['canonical'] for a in baseline['anchors']],('canonical drift',current,baseline)
            if (w,h,s) in seen:assert current==seen[(w,h,s)],('rendered anchor drift',w,h,s)
            else:seen[(w,h,s)]=current
        screenshot('confirmed-'+str(len(records)))
        # Open the bottom-right icon and its existing submenu in every committed
        # geometry. The published union includes Start's translated shadow.
        bar=committed['taskbar'];click(bar[0]+round(28*factor),bar[1]+bar[3]//2)
        wait(lambda:read(popup_address,1)[0]!=0,'Start after display commit',60)
        key('tab');key('spc');wait(power_observer.opened,'power popup opened after modeset',90)
        started=ticks();wait(lambda:ticks()-started>=80,'power popup repaint after modeset',60)
        bounds=struct.unpack('<4i',read(popup_address+12,16));work=committed['work_area']
        assert bounds[0]>=work[0] and bounds[1]>=work[1],('popup origin',bounds,work)
        shadow=(2*s+50)//100  # SkalaUI rounds halves up, unlike Python round().
        assert bounds[0]+bounds[2]<=work[0]+work[2]+shadow and bounds[1]+bounds[3]<=work[1]+work[3]+shadow,('popup extent including shadow',bounds,work)
        assert bounds[0]+bounds[2]<=w and bounds[1]+bounds[3]<=h,('popup off screen',bounds,w,h)
        def power_frame_ready():
            # Popup metadata precedes its compositor submission. The first
            # failed capture can still contain only Start, without the submenu.
            # Wait for the unchanged full pixel oracle, never weaken it.
            try:power_observer.verify(screenshot('power-ready-probe'),s);return True
            except AssertionError:return False
        wait(power_frame_ready,'complete native power popup pixels',90,interval=.3)
        image=screenshot('power-scale-'+str(len(records)))
        record['power_png']=power_observer.verify(image,s)
        key('esc');key('esc')
        wait(lambda:read(popup_address,1)[0]==0,'Start dismissed after modeset',60)
        record['power_popup_bounds']=list(bounds)
        (out/'progress.json').write_text(json.dumps(dict(baseline=baseline,transitions=records),indent=2))
    health()
    return dict(result='PASS',input_kind=input_kind,desktop_anchor_validation=not smoke,baseline=baseline,transitions=records,kernel_sha256=kernel_sha,iso_sha256=iso_sha)
