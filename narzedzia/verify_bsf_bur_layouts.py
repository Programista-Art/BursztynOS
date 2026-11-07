#!/usr/bin/env python3
"""Verify actual BUR v1 segment contracts after shared-font code growth."""
import pathlib,struct,json,hashlib,sys
rows=[]
for p in sorted(pathlib.Path('.').glob('*.bin')):
    data=p.read_bytes()
    if data[:4]!=b'BUR\0':continue
    if len(data)<60:raise ValueError('Truncated BUR: '+p.name)
    entry,off,size,va,doff,dsize,dva=struct.unpack_from('<7Q',data,4)
    if not (off==4096 and size>0 and dsize>0 and off+size==doff and va+size==dva and va<=entry<va+size and doff<=len(data) and len(data)-doff<=dsize):raise ValueError('Invalid segment bounds: '+p.name)
    if any(v%4096 for v in (off,size,va,doff,dsize,dva)):raise ValueError('Unaligned segment: '+p.name)
    rows.append(dict(path=p.name,bytes=len(data),text_bytes=size,data_capacity=dsize,data_file_bytes=len(data)-doff,sha256=hashlib.sha256(data).hexdigest()))
if not rows:raise ValueError('No BUR v1 files inspected')
if len(sys.argv)>1:pathlib.Path(sys.argv[1]).write_text(json.dumps(rows,indent=2)+'\n')
print('BUR v1 file/VA/entry/segment bounds: PASS,',len(rows),'binaries')
