"""Ordinary release UI modesets; observations never write guest state."""
import json,re,struct,subprocess
from pathlib import Path

def run(out,screenshot,click,move,key,wait,ticks,launch,health,hmp,symbol,kernel_sha,iso_sha,input_kind='usb'):
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
    wm_symbols=subprocess.check_output(['nm','-C',str(root/'menedzer_okien.elf')],text=True)
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
    screenshot('display-initial')
    baseline=desktop();seen={(1280,720,100):baseline};records=[]
    targets=[(1920,1080,125),(1920,1080,100),(1920,1080,125),(1920,1080,150),
             (1920,1080,200),(1920,1080,150),(1920,1080,125),(1920,1080,100),
             (1280,720,100),(1920,1080,100),(1280,720,125),(1920,1080,100),(1280,720,100)]
    if 'BURSZTYN_DISPLAY_SMOKE' in __import__('os').environ:targets=[(1920,1080,125),(1280,720,100)]
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
        committed=geometry();screenshot('immediate-apply-'+str(len(records)))
        assert (committed['width'],committed['height'],committed['scale'])==(w,h,s),committed
        edges=[]
        for px,py in [(0,h-1),(w-1,h-1),(0,0),(w-1,0),(w//2,h-1)]:
            edge(px,py);edges.append(cursor())
        after_edges=geometry()
        assert (after_edges['generation'],after_edges['width'],after_edges['height'],after_edges['scale'])==(committed['generation'],w,h,s),after_edges
        record=dict(committed=committed,edges=edges,after_edges=after_edges);records.append(record)
        # Confirm the real UI transaction after edge observations.
        win=window();factor=s/100
        wait(lambda:window()['w']==round(640*factor),'Display Settings resized for committed scale',120,interval=.01)
        win=window()
        layout=json.loads((Path(__file__).resolve().parents[1]/f'.bsf-test/display-layout-{s}-pending.json').read_text())
        if (old['width'],old['height'])!=(w,h):
            keep=layout['keep']
            click(win['x']+keep[0]+keep[2]//2,win['y']+keep[1]+keep[3]//2)
        wait(lambda:geometry()['desktop_generation']==committed['generation'],'desktop generation acknowledgement',120,interval=.01)
        current=desktop();record['desktop']=current
        assert [a['canonical'] for a in current['anchors']]==[a['canonical'] for a in baseline['anchors']],('canonical drift',current,baseline)
        if (w,h,s) in seen:assert current==seen[(w,h,s)],('rendered anchor drift',w,h,s)
        else:seen[(w,h,s)]=current
        screenshot('confirmed-'+str(len(records)))
        (out/'progress.json').write_text(json.dumps(dict(baseline=baseline,transitions=records),indent=2))
    health()
    return dict(result='PASS',input_kind=input_kind,baseline=baseline,transitions=records,kernel_sha256=kernel_sha,iso_sha256=iso_sha)
