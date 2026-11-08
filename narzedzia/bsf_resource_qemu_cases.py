import json
from bsf_process_observer import ProcessObserver
from pathlib import Path

def run_resource(c):
    root=Path(__file__).resolve().parents[1];out=c['out'];args=c['args']
    o=ProcessObserver(c['hmp'],c['symbol'],root/'ustawienia.elf','Ustawienia')
    c['launch_start']('ustawienia');c['wait'](o.pid,'native Settings process',180)
    c['wait'](lambda:o.value('BSF::(anonymous namespace)::initialized','<?'),'Settings BSF initialized',180)
    # A complete first frame establishes that font loading/rasterization finished.
    c['wait'](lambda:c['screenshot']('resource-opening-probe').getpixel((1033,114))[0]>150,'Settings initial frame',240)
    body=(280,150,1010,610)
    before=c['screenshot']('resource-before-fonts').crop(body).tobytes()
    c['click'](800,113) # Explicit window activation before a content action.
    c['click'](190,342)
    c['wait'](lambda:c['screenshot']('resource-font-page-probe').crop(body).tobytes()!=before,'Fonts page pixels replace System page',120)
    rows=o.catalog();assert len(rows)==args.expected_font_count,(len(rows),args.expected_font_count)
    assert len({r['id'] for r in rows})==len(rows)
    assert rows==sorted(rows,key=lambda r:(r['family'],r['path']))
    info=o.user(o.address('BSF::(anonymous namespace)::resource_info'),56)
    import struct
    examined,rejected,resident,peak=struct.unpack_from('<4I',info)
    assert resident<=16*1024*1024 and peak<=16*1024*1024
    if args.expected_resident_bytes:assert resident==args.expected_resident_bytes,(resident,args.expected_resident_bytes)
    if len(rows)==128:
        c['click'](600,220)
        popup=c['symbol']('(anonymous namespace)::application_popup')
        c['wait'](lambda:o.physical(popup,1)[0]!=0,'expanded font-family popup',60)
        x,y,w,h=struct.unpack('<4i',o.physical(popup+12,16))
        c['move'](x+w//2,y+h//2)
        initial=c['screenshot']('resource-popup-before-scroll').crop((x,y,x+w,y+h)).tobytes()
        for _ in range(8):
            c['q'].call('input-send-event',{'events':[{'type':'btn','data':{'button':'wheel-down','down':down}} for _ in range(8) for down in (True,False)]})
            started=c['guest_ticks']();c['wait'](lambda:c['guest_ticks']()-started>=80,'font list wheel consumed',60)
        c['wait'](lambda:c['screenshot']('resource-popup-scroll-probe').crop((x,y,x+w,y+h)).tobytes()!=initial,'font-family list scroll pixels',90)
        c['screenshot']('resource-catalog-last');c['key']('esc')
    c['screenshot']('resource-settings-fonts');c['health']()
    result=dict(result='PASS',mode='native-font-resources',catalog_count=len(rows),examined=examined,rejected=rejected,resident=resident,peak_resident=peak,catalog=rows,kernel_sha256=c['kernel_sha'],iso_sha256=c['iso_sha'])
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
