"""Native font Apply with fresh Settings windows and exact popup row input."""
import json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def run(out,screenshot,click,move,key,wait,ticks,launch,health,hmp,symbol,kernel_sha,iso_sha):
    from power_png_qemu_observer import PowerObserver
    observer=PowerObserver(hmp,symbol);read=observer.read
    titles=symbol('(anonymous namespace)::tytul_okna');layers=symbol('tablica_warstw');popup_address=symbol('(anonymous namespace)::application_popup')
    config=symbol('bsf_config_request(proces*, unsigned long, unsigned long, unsigned long)::active')
    def size():return struct.unpack('<I',read(config+12,4))[0]
    def window():
        names=read(titles,16*48);meta=read(layers,16*40)
        for pid in range(1,16):
            if names[pid*48:(pid+1)*48].split(b'\0')[0]==b'Ustawienia':
                _,z,x,y,w,h=struct.unpack_from('<6i',meta,pid*40);return dict(pid=pid,x=x,y=y,w=w,h=h)
    def settle(n=200):
        start=ticks();wait(lambda:ticks()-start>=n,'font UI repaint',90)
    def hit(r):
        w=window();click(w['x']+r[0]+r[2]//2,w['y']+28+r[1]+r[3]//2);settle()
    records=[]
    for target,index in ((12,0),(20,4),(24,5),(16,2)):
        current=size();print('Font acceptance:',current,'->',target,flush=True)
        launch('ustawienia');wait(lambda:window() is not None,'fresh font Settings',180);settle(300)
        layout=json.loads((ROOT/f'.bsf-test/support-layout-{current}.json').read_text())
        hit(layout['nav'][3]);hit(layout['size'])
        wait(lambda:bool(read(popup_address,1)[0]),'size combo open',90)
        bounds=struct.unpack('<4i',read(popup_address+12,16));row=max(22,layout['themes_heading'][3]+6)
        screenshot('size-popup-'+str(target));click(bounds[0]+bounds[2]//2,bounds[1]+6+index*row+row//2)
        wait(lambda:not read(popup_address,1)[0],'font combo committed',90);settle()
        screenshot('size-selected-'+str(target));hit(layout['apply'])
        screenshot('size-applied-'+str(target));print('Active font:',size(),flush=True)
        wait(lambda:size()==target,'font Apply '+str(target),90);settle(300)
        w=window();click(w['x']+w['w']-16,w['y']+14);wait(lambda:window() is None,'font Settings dismissed',90)
        click(28,700);settle();key('tab');key('spc');wait(observer.opened,'font power popup open',90);settle()
        im=screenshot('power-font-'+str(target));sample=observer.verify(im)
        expected=next(r for r in json.loads((ROOT/'.bsf-test/power-png-metrics.json').read_text()) if r['font']==target and r['scale']==100 and r['screen']==[1280,720])
        assert sample['popup'][2]==expected['width'],(target,sample,expected)
        sample['font']=target;records.append(sample)
        (out/'font-progress.json').write_text(json.dumps(records,indent=2))
        key('esc');settle();key('esc');wait(lambda:not read(popup_address,1)[0],'Start dismissed after font',90);health()
    return dict(result='PASS',font_samples=records,kernel_sha256=kernel_sha,iso_sha256=iso_sha)
