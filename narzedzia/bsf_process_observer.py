"""Read-only QMP observation of a Ring 3 process; no guest-state writes."""
import json,re,struct,subprocess
from functools import lru_cache
from pathlib import Path

@lru_cache(maxsize=1)
def process_layout():
    root=Path(__file__).resolve().parents[1]
    binary=root/'.bsf-test/bsf-process-layout'
    subprocess.run(['g++','-std=c++17',str(root/'narzedzia/qemu_process_layout.cpp'),'-o',str(binary)],check=True)
    return json.loads(subprocess.check_output([str(binary)],text=True))
class ProcessObserver:
    def __init__(self,hmp,symbol,elf,title):
        self.hmp,self.symbol,self.title=hmp,symbol,title.encode()
        self.symbols=subprocess.check_output(['nm','-C',str(elf)],text=True)
        try:self.layouts=symbol('uklady_procesow')
        except RuntimeError:self.layouts=None
        self.layout=process_layout() if self.layouts is not None else dict(process_size=144,cr3_offset=0x38)
    def address(self,name):
        m=re.search(r'^([0-9a-f]+) [bBdDvVrRtT] '+re.escape(name)+r'$',self.symbols,re.M)
        if not m:raise AssertionError('Missing observer symbol '+name)
        address=int(m[1],16)
        if self.layouts is not None:
            pid=self.pid();assert pid,'Process has not published a window: '+self.title.decode()
            metadata=self.physical(self.layouts+pid*self.layout['layout_size'],self.layout['layout_size'])
            if struct.unpack_from('<I',metadata,self.layout['version_offset'])[0]==2:
                address+=struct.unpack_from('<Q',metadata)[0]
        return address
    def physical(self,a,n):
        result=[]
        for row in self.hmp(f'xp /{n}bx 0x{a:x}').splitlines():
            if ':' in row:result.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',row.split(':',1)[1]))
        assert len(result)==n
        return bytes(result)
    def pid(self):
        names=self.physical(self.symbol('(anonymous namespace)::tytul_okna'),16*48)
        return next((i for i in range(1,16) if names[i*48:(i+1)*48].startswith(self.title)),None)
    def user(self,a,n):
        p=self.pid();assert p
        root=struct.unpack('<Q',self.physical(self.symbol('tablica_procesow')+p*self.layout['process_size']+self.layout['cr3_offset'],8))[0]
        result=b''
        while n:
            table=root&0x000ffffffffff000
            for shift in (39,30,21,12):
                entry=struct.unpack('<Q',self.physical(table+((a>>shift)&511)*8,8))[0];assert entry&1
                if shift==12 or shift in (30,21) and entry&128:
                    phys=(entry&0x000ffffffffff000&~((1<<shift)-1))+(a&((1<<shift)-1));break
                table=entry&0x000ffffffffff000
            count=min(n,4096-(a&4095));result+=self.physical(phys,count);a+=count;n-=count
        return result
    def value(self,name,fmt='<I'):
        return struct.unpack(fmt,self.user(self.address(name),struct.calcsize(fmt)))[0]
    def catalog(self):
        n=self.value('BSF::(anonymous namespace)::count');assert n<=128
        pointer=self.value('BSF::(anonymous namespace)::catalog','<Q')
        data=self.user(pointer,n*348) if n else b''
        return [dict(id=struct.unpack_from('<I',data,i*348)[0],path=data[i*348+4:i*348+164].split(b'\0',1)[0].decode(),family=data[i*348+164:i*348+260].split(b'\0',1)[0].decode()) for i in range(n)]
    def window(self):
        p=self.pid();assert p
        return struct.unpack_from('<4i',self.physical(self.symbol('tablica_warstw')+p*40,40),8)
    def object(self,typename):
        table=self.address('vtable for '+typename)+16
        block=self.value('(anonymous namespace)::poczatek_sterty','<Q')
        for _ in range(4096):
            if not block:return None
            data=self.user(block,40)
            if not data[8] and struct.unpack_from('<Q',data,32)[0]==table:return block+32
            block=struct.unpack_from('<Q',data,16)[0]
        raise AssertionError('Observer exceeded bounded GUI heap list')
    def visible(self,address):
        import json
        from pathlib import Path
        root=Path(__file__).resolve().parents[1];layout=root/'.bsf-test/control-layout.json'
        if not layout.is_file():
            binary=root/'.bsf-test/control-layout'
            subprocess.run(['g++','-std=c++17','-fcoroutines','-O0','-ffunction-sections','-fdata-sections','-I'+str(root),str(root/'narzedzia/bsf_control_observer.cpp'),'-Wl,--gc-sections','-o',str(binary)],check=True)
            layout.write_text(subprocess.check_output([str(binary)],text=True))
        return bool(self.user(address+json.loads(layout.read_text())['visible'],1)[0])
