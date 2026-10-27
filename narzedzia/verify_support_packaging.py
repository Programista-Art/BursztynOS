#!/usr/bin/env python3
"""Verify exact Support assets in ISO, embedded kernel and optional installed PSF."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
from package_support import NAMES,ROOT

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso',type=Path,default=ROOT/'BursztynOS.iso')
    p.add_argument('--kernel',type=Path,default=ROOT/'system_operacyjny.bin')
    p.add_argument('--disk',type=Path)
    args=p.parse_args()
    elf=args.kernel.read_bytes();phoff=struct.unpack_from('<Q',elf,32)[0];size,count=struct.unpack_from('<HH',elf,54)
    loads=[]
    for i in range(count):
        kind,flags,offset,vaddr,paddr,filesz,memsz,align=struct.unpack_from('<IIQQQQQQ',elf,phoff+i*size)
        if kind==1:loads.append((vaddr,offset,filesz))
    symbols={}
    for line in subprocess.check_output(['nm',str(args.kernel)],text=True).splitlines():
        parts=line.split()
        if len(parts)==3:symbols[parts[2]]=int(parts[0],16)
    with tempfile.TemporaryDirectory(prefix='support-iso-') as tmp:
        target=Path(tmp)/'support'
        subprocess.run(['xorriso','-osirrox','on','-indev',str(args.iso),'-extract','/dane/sponsorze',str(target)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        for name in NAMES:
            source=(ROOT/'dane/sponsorze'/name).read_bytes()
            assert (target/name).read_bytes()==source,name+' ISO'
            symbol='_binary_dane_sponsorze_'+name.replace('.','_')
            start,end=symbols[symbol+'_start'],symbols[symbol+'_end']
            match=[(v,o,n) for v,o,n in loads if v<=start and end<=v+n]
            assert len(match)==1,name+' load segment'
            v,o,n=match[0];assert elf[o+start-v:o+end-v]==source,name+' embedded'
            if args.disk:
                from test_diagnostyka_qemu import read_psf_file
                assert read_psf_file(args.disk,'/dane/sponsorze/'+name)==source,name+' installed'
            app=(ROOT/'ustawienia.bin').read_bytes()
            for line in source.splitlines():
                if line.strip():assert line.strip() not in app,name+' embedded in Settings'
    print('Support assets: ISO/kernel'+('/installed' if args.disk else '')+' exact bytes PASS; Settings reads names at runtime')
if __name__=='__main__':main()
