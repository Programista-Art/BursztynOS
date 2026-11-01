#!/usr/bin/env python3
"""Verify release embedding and create isolated malformed-asset media fixtures."""
import argparse,hashlib,json,re,shutil,struct,subprocess,zlib
from pathlib import Path
from package_power_icons import NAMES
ROOT=Path(__file__).resolve().parents[1]
def symbols(kernel):
    result={}
    for line in subprocess.check_output(['nm',str(kernel)],text=True).splitlines():
        bits=line.split()
        if len(bits)==3:result[bits[2]]=int(bits[0],16)
    return result

def offset(data,va):
    phoff=struct.unpack_from('<Q',data,32)[0];stride,count=struct.unpack_from('<HH',data,54)
    for i in range(count):
        typ,flags,off,address,physical,length,mem,align=struct.unpack_from('<II6Q',data,phoff+i*stride)
        if typ==1 and address<=va<address+length:return off+va-address
    raise RuntimeError('ELF address outside loadable file')

def verify(kernel,iso,out):
    out.mkdir(parents=True,exist_ok=True)
    data=kernel.read_bytes();syms=symbols(kernel);assets={}
    extraction=out/'iso-icons'
    if extraction.exists():shutil.rmtree(extraction)
    subprocess.run(['xorriso','-osirrox','on','-indev',str(iso),'-extract','/ikonki',str(extraction)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
    assert sorted(p.name for p in extraction.iterdir())==sorted(NAMES)
    for name in NAMES:
        key='_binary_ikonki_'+name.replace('.','_');size=syms[key+'_end']-syms[key+'_start']
        start=offset(data,syms[key+'_start']);source=(ROOT/'ikonki'/name).read_bytes()
        assert data[start:start+size]==source== (extraction/name).read_bytes(),name
        assets[name]=dict(bytes=size,sha256=hashlib.sha256(source).hexdigest(),kernel_file_offset=start)
    result=dict(result='PASS',kernel_sha256=hashlib.sha256(data).hexdigest(),iso_sha256=hashlib.sha256(iso.read_bytes()).hexdigest(),assets=assets)
    (out/'packaging.json').write_text(json.dumps(result,indent=2));return result

def failures(kernel,out):
    out.mkdir(parents=True,exist_ok=True);tree=out/'iso';shutil.copytree(ROOT/'isodir',tree)
    data=bytearray(kernel.read_bytes());syms=symbols(kernel)
    manager=(offset(data,syms['_binary_menedzer_okien_bin_start']),offset(data,syms['_binary_menedzer_okien_bin_end']-1)+1)
    path=b'/ikonki/przyciskuspij.png';matches=[m.start() for m in re.finditer(re.escape(path+b'\0'),data)]
    matches=[p for p in matches if not manager[0]<=p<manager[1]];assert len(matches)==1,matches
    p=matches[0];data[p:p+len(path)]=path[:-3]+b'bad' # expected runtime pathname is absent
    p=offset(data,syms['_binary_ikonki_przyciskzamknij_png_start']);data[p:p+8]=b'BAD-PNG!'
    p=offset(data,syms['_binary_ikonki_przyciskzablokuj_png_start']);struct.pack_into('>I',data,p+16,129);struct.pack_into('>I',data,p+29,zlib.crc32(data[p+12:p+29]))
    assert data[manager[0]:manager[1]]==kernel.read_bytes()[manager[0]:manager[1]]
    patched=out/'kernel.bin';patched.write_bytes(data);(tree/'boot/system_operacyjny.bin').write_bytes(data)
    iso=out/'failures.iso'
    with (out/'mkrescue.log').open('w') as log:subprocess.run(['grub-mkrescue','-o',str(iso),str(tree)],stdout=log,stderr=log,check=True)
    (out/'fixture.json').write_text(json.dumps(dict(missing='przyciskuspij.png',corrupt='przyciskzamknij.png',oversized='przyciskzablokuj.png',kernel_sha256=hashlib.sha256(data).hexdigest()),indent=2))
    print(patched,iso)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--failures',action='store_true');parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.failures:failures(ROOT/'system_operacyjny.bin',args.output)
    else:print(json.dumps(verify(ROOT/'system_operacyjny.bin',ROOT/'BursztynOS.iso',args.output)))
