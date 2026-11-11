"""Read-only release Start geometry and PNG cache observer."""
import json,re,struct,subprocess
from functools import lru_cache
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
@lru_cache(maxsize=32)
def bilinear_icon(path,side):
    # Independent fixed-point RGBA oracle for the integrated bilinear icon
    # contract. Horizontal and vertical interpolation truncate separately.
    source=Image.open(path).convert('RGBA');w,h=source.size
    dw,dh=side,side
    if w>h:dh=max(1,side*h//w)
    else:dw=max(1,side*w//h)
    ox,oy=(side-dw)//2,(side-dh)//2
    xs=((w-1)<<16)//(dw-1) if dw>1 else 0
    ys=((h-1)<<16)//(dh-1) if dh>1 else 0
    out=bytearray(side*side*4)
    for y in range(dh):
        fy=ys*y;y0=fy>>16;y1=min(h-1,y0+1);wy=fy&65535
        for x in range(dw):
            fx=xs*x;x0=fx>>16;x1=min(w-1,x0+1);wx=fx&65535
            samples=[source.getpixel((xx,yy)) for xx,yy in [(x0,y0),(x1,y0),(x0,y1),(x1,y1)]]
            channels=[]
            for c in range(4):
                top=(samples[0][c]*(65536-wx)+samples[1][c]*wx)>>16
                bottom=(samples[2][c]*(65536-wx)+samples[3][c]*wx)>>16
                channels.append((top*(65536-wy)+bottom*wy)>>16)
            r,g,b,a=channels;offset=((y+oy)*side+x+ox)*4
            out[offset:offset+4]=bytes((b,g,r,a))
    return bytes(out)

class PowerObserver:
    def __init__(self,hmp,symbol):
        self.hmp=hmp;self.symbol=symbol
        self.layout=json.loads((ROOT/'.bsf-test/power-observer.json').read_text())
        symbols=subprocess.check_output(['nm','-C',str(ROOT/'menedzer_okien.elf')],text=True)
        self.slot=int(re.search(r'^([0-9a-f]+) [bBdD] \(anonymous namespace\)::start_menu$',symbols,re.M)[1],16)
        binary=ROOT/'.bsf-test/power-process-layout'
        subprocess.run(['g++','-std=c++17',str(ROOT/'narzedzia/qemu_process_layout.cpp'),'-o',str(binary)],check=True)
        self.process_layout=json.loads(subprocess.check_output([str(binary)],text=True))
        layout=self.process_layout
        metadata=self.read(self.symbol('uklady_procesow')+layout['layout_size'],layout['layout_size'])
        self.image_base=struct.unpack_from('<Q',metadata)[0] if struct.unpack_from('<I',metadata,layout['version_offset'])[0]==2 else 0
        self.slot=self.relocate(self.slot)
    def relocate(self,address):return address+self.image_base

    def read(self,a,n):
        data=[]
        for line in self.hmp(f'xp /{n}bx 0x{a:x}').splitlines():
            if ':' in line:data.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',line.split(':',1)[1]))
        assert len(data)==n,(hex(a),n,len(data));return bytes(data)
    def user(self,a,n):
        cr3=struct.unpack('<Q',self.read(self.symbol('tablica_procesow')+self.process_layout['process_size']+self.process_layout['cr3_offset'],8))[0];cache={}
        def physical(v):
            table=cr3&0x000ffffffffff000
            for shift in (39,30,21,12):
                slot=table+((v>>shift)&511)*8
                if slot not in cache:cache[slot]=struct.unpack('<Q',self.read(slot,8))[0]
                entry=cache[slot];assert entry&1,('unmapped',hex(v))
                if shift==12 or shift in (30,21) and entry&128:return (entry&0x000ffffffffff000&~((1<<shift)-1))+(v&((1<<shift)-1))
                table=entry&0x000ffffffffff000
        data=b''
        while n:
            count=min(n,4096-(a&4095));data+=self.read(physical(a),count);a+=count;n-=count
        return data
    def pointer(self,a):return struct.unpack('<Q',self.user(a,8))[0]
    def geometry(self):
        base=self.pointer(self.slot);l=self.layout
        rect=lambda a:list(struct.unpack('<4i',self.user(a+l['rect_offset'],16)))
        return dict(start=rect(base),button=rect(self.pointer(base+l['button_slot'])),popup=rect(base+l['popup_offset']))
    def opened(self):
        base=self.pointer(self.slot);l=self.layout
        return bool(self.pointer(base+l['popup_offset']+l['manager_offset']))
    def verify(self,image,scale=100):
        assert self.opened(),'power popup must actually be open'
        cursor=tuple(struct.unpack('<i',self.read(self.symbol(n),4))[0] for n in ('mysz_x','mysz_y'))
        g=self.geometry();start,button,popup=(g[k] for k in ('start','button','popup'))
        scaled=lambda v:(v*scale+50)//100
        assert start[0]+start[2]-button[0]-button[2]==scaled(8),g
        assert start[1]+start[3]-button[1]-button[3]==scaled(8),g
        assert button[2]==button[3]==scaled(36),g
        assert button[1]-popup[1]-popup[3]==scaled(8)+scaled(2),g
        assert popup[0]+popup[2]==button[0]+button[2],g
        base=self.pointer(self.slot);l=self.layout;icons=[]
        for i,name in enumerate(('zasilania','zablokuj','uspij','zamknij','uruchomponownie')):
            cache=base+l['icons_offset']+i*l['icon_size'];ptr=self.pointer(cache+l['pixel_offset']);side=scaled(22 if i==0 else 18)
            assert ptr,('missing PNG',name)
            data=self.user(ptr,side*side*4)
            expected=bilinear_icon(str(ROOT/'ikonki'/('przycisk'+name+'.png')),side)
            assert data==expected,('bilinear PNG cache differs',name,scale)
            # Verify alpha edges on screen against the actual surrounding row.
            if i==0:
                x=button[0]+(button[2]-side)//2;y=button[1]+(button[3]-side)//2
            else:
                row=(popup[3]-scaled(2))//4
                x=popup[0]+scaled(1)+scaled(8);y=popup[1]+scaled(1)+(i-1)*row+(row-side)//2
            bg=image.getpixel((x-2,y+side//2));matched=0
            for yy in range(side):
                for xx in range(side):
                    b,gc,r,a=data[(yy*side+xx)*4:(yy*side+xx+1)*4]
                    if i in (1,2):a=(a*96+127)//255
                    expected_rgb=tuple((v*a+background*(255-a)+127)//255 for v,background in zip((r,gc,b),bg))
                    if cursor[0]-2<=x+xx<cursor[0]+34 and cursor[1]-2<=y+yy<cursor[1]+34:continue
                    assert image.getpixel((x+xx,y+yy))==expected_rgb,('alpha pixels',name,xx,yy,image.getpixel((x+xx,y+yy)),expected_rgb,bg,g)
                    matched+=1
            icons.append(dict(name=name,side=side,pixels=matched))
        g['icons']=icons;return g
