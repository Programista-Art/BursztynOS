"""Read-only QMP observation of a Ring 3 process; no guest-state writes."""
import os,re,struct,subprocess
from pathlib import Path
def app_elf(root,name):
    return Path(os.environ.get('BURSZTYN_APP_SYMBOLS',root))/name
class ProcessObserver:
    def __init__(self,hmp,symbol,elf,title,image_file=None):
        self.hmp,self.symbol,self.title=hmp,symbol,title.encode()
        self.image_file=image_file.encode() if image_file else None
        elf=app_elf(Path(elf).parent,Path(elf).name)
        self.symbols=subprocess.check_output(['nm','-C',str(elf)],text=True)
    def address(self,name):
        m=re.search(r'^([0-9a-f]+) [bBdDvVrRtT] '+re.escape(name)+r'$',self.symbols,re.M)
        if not m:raise AssertionError('Missing observer symbol '+name)
        raw=int(m[1],16)
        p=self.pid();assert p
        base=struct.unpack('<Q',self.physical(self.symbol('uklady_procesow')+p*72,8))[0] or 0x1000000
        return base+(raw-0x1000000 if raw>=0x1000000 else raw)
    def physical(self,a,n):
        result=[]
        for row in self.hmp(f'xp /{n}bx 0x{a:x}').splitlines():
            if ':' in row:result.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',row.split(':',1)[1]))
        assert len(result)==n
        return bytes(result)
    def pid(self):
        names=self.physical(self.symbol('(anonymous namespace)::tytul_okna'),16*48)
        visible=next((i for i in range(1,16) if names[i*48:(i+1)*48].startswith(self.title)),None)
        if visible is not None or self.image_file is None:return visible
        records=self.physical(self.symbol('tablica_procesow'),16*144)
        for i in range(1,16):
            record=records[i*144:(i+1)*144]
            if struct.unpack_from('<i',record,136)[0] not in (0,6) and record[72:136].split(b'\0',1)[0].endswith(b'/'+self.image_file):return i
        return None
    def user(self,a,n):
        p=self.pid();assert p
        root=struct.unpack('<Q',self.physical(self.symbol('tablica_procesow')+p*144+0x38,8))[0]
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
    def mapped_user_bytes(self):
        """Present user mappings, including shared buffers; call while paused.

        This is a sampled mapping size, not private RSS or an allocation peak.
        """
        p=self.pid();assert p
        root=struct.unpack('<Q',self.physical(self.symbol('tablica_procesow')+p*144+0x38,8))[0]
        memo={}
        def walk(table,level):
            key=(table,level)
            if key in memo:return memo[key]
            assert len(memo)<1024,'Bounded memory observer exceeded page-table limit'
            memo[key]=0
            total=0
            for entry in struct.unpack('<512Q',self.physical(table,4096)):
                if entry&5!=5:continue
                if level==1:total+=4096
                elif level in (2,3) and entry&128:total+=1<<(12+9*(level-1))
                else:total+=walk(entry&0x000ffffffffff000,level-1)
            memo[key]=total
            return total
        return walk(root&0x000ffffffffff000,4)
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
