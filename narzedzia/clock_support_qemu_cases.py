"""Ordinary-release clock frame sampling and native Settings Support input."""
import hashlib
import json
import time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def run(out,screenshot,click,move,key,wait,ticks,launch,health,config,kernel_sha,iso_sha):
    def settle(n=250):
        start=ticks();wait(lambda:ticks()-start>=n,'native repaint consumed',120)
    move(1100,600);settle(500)
    frames=out/'clock-frames';frames.mkdir(exist_ok=True)
    samples=[];hashes=set();neighbor=None;padding=None
    for index in range(240):
        im=screenshot('clock-live-probe');bar=im.crop((0,680,1280,720));clock=bar.crop((1130,0,1280,40))
        gold=sum(1 for r,g,b in clock.getdata() if r>130 and g>60 and b<90)
        if gold<20:raise AssertionError('Clock has a blank intermediate frame')
        adjacent=hashlib.sha256(bar.crop((0,0,1130,40)).tobytes()).hexdigest()
        pad=tuple(clock.getpixel((x,y)) for x in (0,3,146) for y in (0,3,35,39))
        if neighbor is not None and adjacent!=neighbor:raise AssertionError('Neighboring taskbar pixels changed during clock-only updates')
        if padding is not None and pad!=padding:raise AssertionError('Clock background/border changed between ticks')
        neighbor,padding=adjacent,pad
        digest=hashlib.sha256(clock.tobytes()).hexdigest();hashes.add(digest)
        bar.save(frames/f'{index:03}.png');samples.append(dict(frame=index,clock_sha256=digest,neighbor_sha256=adjacent,text_pixels=gold))
        if index>=99 and len(hashes)>=6:break
        time.sleep(.04)
    if len(hashes)<6:raise AssertionError('Fewer than six distinct displayed seconds')
    (out/'clock-frames.json').write_text(json.dumps(dict(result='PASS',samples=samples,distinct_times=len(hashes),no_blank=True,stable_neighbors=True),indent=2)+'\n')
    print('Clock frames PASS:',len(samples),'frames,',len(hashes),'distinct times',flush=True)
    def geo(px):return json.loads((ROOT/f'.bsf-test/support-layout-{px}.json').read_text())
    def hit(rect):click(105+rect[0]+rect[2]//2,128+rect[1]+rect[3]//2)
    def open_settings():
        launch('ustawienia')
        wait(lambda:(lambda p:p[0]>150 and p[1]<130 and p[2]<130)(screenshot('support-opening-probe').getpixel((1033,114))),'Settings ready',240,interval=1)
        settle(250)
    def capture(px,label):
        geometry=geo(px);hit(geometry['nav'][3]);settle()
        result=[]
        for i,rect in enumerate(geometry['tabs']):
            hit(rect);move(1100,660);settle()
            im=screenshot(f'support-{label}-{px}-{i}')
            x,y,w,h=geometry['table'];header=26 if px==16 else 32;row=22 if px==16 else 30
            area=im.crop((105+x+10,128+y+header+1,105+x+min(w-20,500),128+y+header+row-1))
            if len(set(area.getdata()))<4:raise AssertionError('Support list has no rendered UTF-8 text')
            result.append(hashlib.sha256(area.tobytes()).hexdigest())
        if len(set(result))!=3:raise AssertionError('Support categories display duplicate data')
        return result
    open_settings();before=capture(16,'initial')
    hit(geo(16)['refresh']);settle(500)
    if capture(16,'refresh')!=before:raise AssertionError('Refresh changed category mapping')
    # Keep the same Support model while applying through the native Font page.
    hit(geo(16)['nav'][1]);settle();hit(geo(16)['size']);key('end');key('ret');hit(geo(16)['apply'])
    wait(lambda:config()[3]==24,'Apply 24 px with Support model retained',180,interval=1);settle(750)
    large=capture(24,'live-font')
    hit(geo(24)['refresh']);settle(500)
    if capture(24,'large-refresh')!=large:raise AssertionError('Large-font Refresh changed data')
    click(1033,114);settle(250);open_settings()
    if capture(24,'reopen')!=large:raise AssertionError('Settings restart did not reload the same support files')
    health()
    return dict(result='PASS',mode='clock-support',clock_frames=len(samples),clock_distinct_times=len(hashes),clock_no_blank=True,
                clock_stable_neighbors=True,support_categories=3,support_refresh=True,support_reopen=True,font_sizes=[16,24],
                category_sources=['/dane/sponsorze/mecenasi.txt','/dane/sponsorze/sponsorzy.txt','/dane/sponsorze/wkladtworczy.txt'],
                kernel_sha256=kernel_sha,iso_sha256=iso_sha,no_guest_fault=True)
