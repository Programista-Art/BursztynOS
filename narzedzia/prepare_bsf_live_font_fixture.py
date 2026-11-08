#!/usr/bin/env python3
"""Put a tiny derived DejaVu fixture in an OFFLINE disposable installed disk.
Reuses cytaty.txt's existing block/inode, renamed /live.ttf. No allocation or
real disk access. Native Settings performs subsequent installation and refresh.
"""
from pathlib import Path
import struct, mmap, json, argparse
ROOT=Path(__file__).resolve().parents[1]
def font(family="Bursztyn Test"):
    data=(ROOT/'czcionki/ttf/DejaVuSans.ttf').read_bytes()
    tables={data[12+i*16:16+i*16]:data[o:o+n] for i in range(struct.unpack_from('>H',data,4)[0]) for o,n in [struct.unpack_from('>II',data,20+i*16)]}
    head=bytearray(tables[b'head']);hhea=bytearray(tables[b'hhea'])
    # DejaVu Sans uses a simple A glyph; its ID is resolved by production BSF
    # in the resource tests. Resolve format12 here independently for the fixture.
    cmap=tables[b'cmap'];gid=None
    for i in range(struct.unpack_from('>H',cmap,2)[0]):
        off=struct.unpack_from('>I',cmap,8+i*8)[0]
        if struct.unpack_from('>H',cmap,off)[0]!=12:continue
        for j in range(struct.unpack_from('>I',cmap,off+12)[0]):
            a,b,g=struct.unpack_from('>III',cmap,off+16+j*12)
            if a<=65<=b:gid=g+65-a;break
        if gid is not None:break
    assert gid is not None
    loca=tables[b'loca'];long=struct.unpack_from('>H',head,50)[0]
    loc=lambda g:struct.unpack_from('>I' if long else '>H',loca,g*(4 if long else 2))[0]*(1 if long else 2)
    outline=tables[b'glyf'][loc(gid):loc(gid+1)];assert struct.unpack_from('>h',outline)[0]>0
    struct.pack_into('>I',head,8,0);struct.pack_into('>H',head,50,1);struct.pack_into('>H',hhea,34,2)
    names=[family,'Regular'];strings=b''.join(s.encode('utf-16-be') for s in names)
    records=b'';offset=0
    for i,s in enumerate(names):
        n=len(s.encode('utf-16-be'));records+=struct.pack('>6H',3,1,0x409,i+1,n,offset);offset+=n
    t={b'head':bytes(head),b'hhea':bytes(hhea),b'maxp':struct.pack('>IH',0x10000,2),b'hmtx':tables[b'hmtx'][:4]+tables[b'hmtx'][gid*4:gid*4+4],b'loca':struct.pack('>III',0,0,len(outline)),b'glyf':outline,
       b'cmap':struct.pack('>HHHHI',0,1,3,10,12)+struct.pack('>HHIIIIII',12,0,28,0,1,65,65,1),
       b'name':struct.pack('>HHH',0,2,30)+records+strings,b'post':tables[b'post'][:32]}
    def checksum(b):
        b+=bytes((-len(b))%4);return sum(struct.unpack('>'+str(len(b)//4)+'I',b))&0xffffffff
    n=len(t);power=1<<(n.bit_length()-1)
    result=bytearray(struct.pack('>IHHHH',0x10000,n,power*16,power.bit_length()-1,n*16-power*16)+bytes(n*16))
    head_offset=0
    for i,(tag,b) in enumerate(sorted(t.items())):
        result+=bytes((-len(result))%4);off=len(result)
        struct.pack_into('>4sIII',result,12+i*16,tag,checksum(b),off,len(b));result+=b
        if tag==b'head':head_offset=off
    result+=bytes((-len(result))%4);struct.pack_into('>I',result,head_offset+8,(0xb1b0afba-checksum(bytes(result)))&0xffffffff)
    assert len(result)<=4096
    return bytes(result)
def prepare(directory):
    directory=directory.resolve();disk=directory/'target.img'
    if not directory.is_relative_to((ROOT/'.bsf-test').resolve()) or not disk.is_file() or disk.is_symlink():raise RuntimeError('Only private disposable regular images')
    if '[INSTALL] COMPLETE verified' not in (directory/'installer/serial.log').read_text():raise RuntimeError('Installation incomplete')
    pidfile=directory/'installer/pid'
    if pidfile.exists() and Path('/proc/'+pidfile.read_text().strip()).exists():raise RuntimeError('Installer VM still running')
    payload=font()
    with disk.open('r+b') as f,mmap.mmap(f.fileno(),0) as image:
        assert image[512:520]==b'EFI PART'
        entries,count,stride=struct.unpack_from('<QII',image,584);assert count<=128 and stride==128
        base=next(struct.unpack_from('<Q',image,entries*512+i*128+32)[0]*512 for i in range(count) if image[entries*512+i*128:entries*512+i*128+16]==bytes.fromhex('3ce71fb214048545bd078ab4980f9c88'))
        magic,total,blocks,root,nodes,data=struct.unpack_from('<4sQQQQQ',image,base);assert magic==b'BSP2'
        def inode(ident):
            assert 1<=ident<=data-nodes
            off=base+(nodes+ident-1)*4096;kind=image[off];size=struct.unpack_from('<Q',image,off+1)[0];length=size*64 if kind==2 else size
            assert length<=1024*1024
            ptrs=struct.unpack_from('<490Q',image,off+25)[:(length+4095)//4096]
            assert all(p<blocks-data for p in ptrs)
            return off,kind,length,[base+(data+p)*4096 for p in ptrs]
        _,kind,length,locations=inode(root);assert kind==2
        match=None
        for p in locations:
            for j in range(0,min(4096,length),64):
                if image[p+j+8:p+j+64].split(b'\0',1)[0]==b'cytaty.txt':match=(p+j,struct.unpack_from('<Q',image,p+j)[0])
            length-=4096
        if match is None:raise RuntimeError('Missing disposable cytaty.txt source')
        entry,ident=match;off,kind,length,locations=inode(ident);assert kind==1 and len(locations)>=1
        image[locations[0]:locations[0]+4096]=payload+bytes(4096-len(payload))
        struct.pack_into('<Q',image,off+1,len(payload));image[entry+8:entry+64]=b'live.ttf'+bytes(48)
        image.flush()
    (directory/'live-font.ttf').write_bytes(payload)
    (directory/'live-font-prepared.json').write_text(json.dumps(dict(path='/live.ttf',family='Bursztyn Test',bytes=len(payload)))+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args();prepare(a.directory)
