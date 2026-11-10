import json,struct
from pathlib import Path
from bsf_process_observer import ProcessObserver

def run_live(c):
    root=Path(__file__).resolve().parents[1];out=c['out'];click,key,wait=c['click'],c['key'],c['wait']
    o=ProcessObserver(c['hmp'],c['symbol'],root/'ustawienia.elf','Ustawienia')
    c['launch_start']('ustawienia');wait(o.pid,'Settings for native font installation',180)
    wait(lambda:c['screenshot']('live-font-opening-probe').getpixel((1033,114))[0]>150,'Settings complete frame',180)
    config_address=c['symbol']('bsf_config_request(proces*, unsigned long, unsigned long, unsigned long)::active')
    def cfg():return struct.unpack('<6I',o.physical(config_address,24))
    before_config=cfg();size=before_config[3]
    layout=json.loads((root/f'.bsf-test/live-font-layout-{size}.json').read_text())
    def hit(name):
        x,y,w,h=o.window();r=layout[name];click(x+r[0]+r[2]//2,y+r[1]+r[3]//2)
    x,y,w,h=o.window();click(x+w//2,y+12)
    target=o.object('SettingsUI::FontPage');assert target
    for _ in range(3):
        hit('nav')
        if o.visible(target):break
    wait(lambda:o.visible(target),'native Fonts page visible',90)
    before=o.catalog();assert not any(r['family']=='Bursztyn Test' for r in before)
    hit('source');key('ctrl','a')
    for ch in '/live.ttf':key({'/':'slash','.':'dot'}.get(ch,ch))
    hit('install')
    wait(lambda:len(o.catalog())==len(before)+1,'Settings installs TTF using ordinary BWS',120)
    after=o.catalog();installed=next(r for r in after if r['family']=='Bursztyn Test');assert installed['path']=='/czcionki/ttf/live.ttf'
    assert cfg()==before_config,'Installation unexpectedly changed selected system font'
    c['screenshot']('live-font-installed');hit('refresh');assert len(o.catalog())==len(after)
    hit('families');key('home');key('ret');hit('apply')
    wait(lambda:cfg()[2]==installed['id'],'new family selected and persisted',120)
    hit('source');key('ctrl','a')
    for _ in range(4):key('shift','a')
    def imported_mask():
        cache=o.value('BSF::(anonymous namespace)::cache','<Q');slots=struct.unpack('<Q',o.user(cache,8))[0]
        data=o.user(slots,128*56)
        return any(struct.unpack_from('<I',data,i*56)[0]==installed['id'] for i in range(128))
    wait(imported_mask,'new font renders a supported A mask',120)
    c['screenshot']('live-font-selected-fallback')
    # Restore a bundled family before deleting the disposable imported font.
    hit('families');key('home');key('down');key('ret');hit('apply')
    wait(lambda:cfg()[2]!=installed['id'],'bundled family restored before removal',120)
    c['launch_start']('shell')
    shell=ProcessObserver(c['hmp'],c['symbol'],root/'shell.elf','Powłoka')
    wait(shell.pid,'Shell for authorized disposable font removal',180)
    def input_text():
        p=shell.value('(anonymous namespace)::g_obecny_bufor_wejscia','<Q')
        return shell.user(p,2048).split(b'\0',1)[0] if p else None
    wait(lambda:input_text()==b'','Shell ready',180)
    x,y,w,h=shell.window();click(x+300,y+h-20)
    for ch in 'usun /czcionki/ttf/live.ttf':key({'/':'slash','.':'dot',' ':'spc'}.get(ch,ch))
    key('ret');wait(lambda:input_text()==b'','font removal command complete',120)
    x,y,w,h=o.window();click(x+200,y+10);hit('refresh')
    wait(lambda:len(o.catalog())==len(before),'catalog removes font and clears caches',120)
    assert all(r['id']!=installed['id'] for r in o.catalog())
    c['screenshot']('live-font-removed');c['health']()
    (out/'result.json').write_text(json.dumps(dict(result='PASS',mode='native-live-font-install-refresh-remove',before=before,installed=installed,final=o.catalog(),selected_font_unchanged_on_install=True,ordinary_bws=True,kernel_sha256=c['kernel_sha'],iso_sha256=c['iso_sha']),ensure_ascii=False,indent=2)+'\n')
