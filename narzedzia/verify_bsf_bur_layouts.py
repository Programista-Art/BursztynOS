#!/usr/bin/env python3
"""Verify actual BUR v1 and BUR v2 segment contracts after shared-font code growth."""
import pathlib,struct,json,hashlib,sys
rows=[]
for p in sorted(pathlib.Path('.').glob('*.bin')):
    data=p.read_bytes()
    if data[:4]==b'BUR\x02':
        if len(data)<160:raise ValueError('Truncated BUR v2: '+p.name)
        entry=struct.unpack_from('<Q',data,24)[0]
        off,size,va=struct.unpack_from('<3Q',data,32)
        roff,rsize,rva=struct.unpack_from('<3Q',data,56)
        doff,dfile,dsize,dva=struct.unpack_from('<4Q',data,80)
        reloff,relcnt,relentsz,stack_pages=struct.unpack_from('<QIHH',data,112)
        ro_ok=(rsize==0 and roff==0 and rva==0 and off+size==doff) or (rsize>0 and off+size==roff and roff+rsize==doff and rva==va+size)
        rel_ok=(relcnt==0 and reloff==0 and doff+dfile==len(data)) or (relcnt>0 and relentsz==24 and reloff==doff+dfile and reloff+relcnt*relentsz==len(data))
        if not (off==4096 and size>0 and dsize>0 and ro_ok and rel_ok and va==0x1000 and va+size+rsize+0x1000==dva and va<=entry<va+size and dfile<=dsize):
            raise ValueError('Invalid BUR v2 segment bounds: '+p.name)
        if any(v%4096 for v in (off,size,va,roff,rsize,rva,doff,dsize,dva)):
            raise ValueError('Unaligned BUR v2 segment: '+p.name)
        rows.append(dict(path=p.name,version=2,bytes=len(data),text_bytes=size,rodata_bytes=rsize,data_capacity=dsize,data_file_bytes=dfile,relocations=relcnt,sha256=hashlib.sha256(data).hexdigest()))
    elif data[:4]==b'BUR\0':
        if len(data)<60:raise ValueError('Truncated BUR v1: '+p.name)
        entry,off,size,va,doff,dsize,dva=struct.unpack_from('<7Q',data,4)
        if not (off==4096 and size>0 and dsize>0 and off+size==doff and va+size==dva and va<=entry<va+size and doff<=len(data) and len(data)-doff<=dsize):raise ValueError('Invalid segment bounds: '+p.name)
        if any(v%4096 for v in (off,size,va,doff,dsize,dva)):raise ValueError('Unaligned segment: '+p.name)
        rows.append(dict(path=p.name,version=1,bytes=len(data),text_bytes=size,data_capacity=dsize,data_file_bytes=len(data)-doff,sha256=hashlib.sha256(data).hexdigest()))
if not rows:raise ValueError('No BUR files inspected')
if len(sys.argv)>1:pathlib.Path(sys.argv[1]).write_text(json.dumps(rows,indent=2)+'\n')
print('BUR file/VA/entry/segment bounds: PASS,',len(rows),'binaries')
