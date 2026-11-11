import json,struct,time
from pathlib import Path
from bsf_process_observer import ProcessObserver

def run_live(c):
    root=Path(__file__).resolve().parents[1];out=c['out'];click,key,wait=c['click'],c['key'],c['wait']
    elfs=c['args'].app_elf_directory or root
    layout_info=json.loads((root/'.bsf-test/font-observer.json').read_text())
    timings={}
    o=ProcessObserver(c['hmp'],c['symbol'],elfs/'ustawienia.elf','Ustawienia')
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
    started=time.perf_counter_ns();hit('install')
    wait(lambda:len(o.catalog())==len(before)+1,'Settings installs TTF using ordinary BWS',120)
    after=o.catalog();installed=next(r for r in after if r['family']=='Bursztyn Test');assert installed['path']=='/czcionki/ttf/live.ttf'
    timings['install_and_rescan_host_ns']=time.perf_counter_ns()-started
    assert cfg()==before_config,'Installation unexpectedly changed selected system font'
    c['screenshot']('live-font-installed')
    epoch=o.value('BSF::(anonymous namespace)::catalog_epoch');started=time.perf_counter_ns();hit('refresh')
    wait(lambda:o.value('BSF::(anonymous namespace)::catalog_epoch')!=epoch and len(o.catalog())==len(after),'explicit font rescan complete',120)
    timings['refresh_host_ns']=time.perf_counter_ns()-started
    hit('families');key('home');key('ret');hit('apply')
    wait(lambda:cfg()[2]==installed['id'],'new family selected and persisted',120)
    hit('source');key('ctrl','a')
    for _ in range(4):key('shift','a')
    def mask_faces(observer):
        cache=observer.value('BSF::(anonymous namespace)::cache','<Q')
        slots=struct.unpack('<Q',observer.user(cache+layout_info['cache_slots'],8))[0]
        if not slots:return set()
        stride=layout_info['glyph_stride'];data=observer.user(slots,128*stride)
        return {struct.unpack_from('<I',data,i*stride+layout_info['glyph_face'])[0] for i in range(128)}-{0}
    wait(lambda:installed['id'] in mask_faces(o),'new font renders a supported A mask',120)
    c['q'].call('stop')
    try:
        expected=(c['args'].installed_disk.parent/'live-font.ttf').read_bytes()
        stride=layout_info['loaded_stride'];loaded=o.user(o.address('BSF::(anonymous namespace)::loaded'),4*stride)
        actual=None
        for i in range(4):
            at=i*stride
            if struct.unpack_from('<I',loaded,at+layout_info['loaded_id'])[0]!=installed['id']:continue
            pointer=struct.unpack_from('<Q',loaded,at+layout_info['loaded_bytes'])[0]
            size=struct.unpack_from('<I',loaded,at+layout_info['loaded_size'])[0]
            if pointer and size==len(expected):actual=o.user(pointer,size)
        assert actual==expected,'Installed font bytes differ from the incoming file'
    finally:c['q'].call('cont')
    c['screenshot']('live-font-selected-fallback')
    c['launch_start']('notatnik')
    note=ProcessObserver(c['hmp'],c['symbol'],elfs/'notatnik.elf','Notatnik')
    wait(lambda:note.pid() and note.window()[2]>0,'Notepad window for installed font',180)
    editor=json.loads((root/'.bsf-test/editor-observer.json').read_text())
    def editor_text():
        memo=struct.unpack('<Q',note.user(note.address('(anonymous namespace)::g_notatnik')+editor['memo'],8))[0]
        if not memo:return None
        pointer=struct.unpack('<Q',note.user(memo+editor['buffer'],8))[0]
        size=struct.unpack('<Q',note.user(memo+editor['length'],8))[0]
        return note.user(pointer,size) if pointer and size else b''
    wait(lambda:editor_text()==b'','Notepad empty buffer ready',120)
    x,y,w,h=note.window();click(x+200,y+200)
    for _ in range(4):key('shift','a')
    key('z');wait(lambda:editor_text()==b'AAAAz','Notepad primary and fallback text bytes',120)
    wait(lambda:installed['id'] in mask_faces(note) and bool(mask_faces(note)-{installed['id']}),'Notepad imported and bundled fallback masks',120)
    c['screenshot']('live-font-notepad-primary-fallback')
    click(236,700) # Settings is the second taskbar window, after Husarz.
    # Restore a bundled family before deleting the disposable imported font.
    hit('families');key('home');key('down');key('ret');hit('apply')
    wait(lambda:cfg()[2]!=installed['id'],'bundled family restored before removal',120)
    c['launch_start']('shell')
    shell=ProcessObserver(c['hmp'],c['symbol'],elfs/'shell.elf','Powłoka')
    wait(shell.pid,'Shell for authorized disposable font removal',180)
    def input_text():
        p=shell.value('(anonymous namespace)::g_obecny_bufor_wejscia','<Q')
        return shell.user(p,2048).split(b'\0',1)[0] if p else None
    wait(lambda:input_text()==b'','Shell ready',180)
    x,y,w,h=shell.window();click(x+300,y+h-20)
    for ch in 'usun /czcionki/ttf/live.ttf':key({'/':'slash','.':'dot',' ':'spc'}.get(ch,ch))
    key('ret');wait(lambda:input_text()==b'','font removal command complete',120)
    click(236,700)
    epoch=o.value('BSF::(anonymous namespace)::catalog_epoch');started=time.perf_counter_ns();hit('refresh')
    wait(lambda:len(o.catalog())==len(before),'catalog removes font and clears caches',120)
    assert o.value('BSF::(anonymous namespace)::catalog_epoch')!=epoch
    timings['remove_refresh_host_ns']=time.perf_counter_ns()-started
    assert all(r['id']!=installed['id'] for r in o.catalog())
    c['screenshot']('live-font-removed');c['health']()
    (out/'result.json').write_text(json.dumps(dict(result='PASS',mode='native-live-font-install-refresh-remove',before=before,installed=installed,final=o.catalog(),selected_font_unchanged_on_install=True,ordinary_bws=True,installed_bytes_equal=True,notepad_primary_and_fallback=True,host_timings_ns=timings,kernel_sha256=c['kernel_sha'],iso_sha256=c['iso_sha']),ensure_ascii=False,indent=2)+'\n')
