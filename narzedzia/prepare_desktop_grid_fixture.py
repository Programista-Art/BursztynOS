#!/usr/bin/env python3
"""Refresh final UI payloads in an OFFLINE private installed QEMU fixture.

No allocation: replacement programs must fit their existing direct blocks.
EFI copying uses mtools. Never host devices.
"""
import hashlib
import json
import mmap
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def refresh(directory, reset_preferences=False):
    directory = directory.resolve()
    disk = directory/'target.img'
    if not directory.is_relative_to((ROOT/'.bsf-test').resolve()) or not disk.is_file() or disk.is_symlink():
        raise RuntimeError('Only an offline private .bsf-test installation is accepted')
    if '[INSTALL] COMPLETE verified' not in (directory/'installer/serial.log').read_text():
        raise RuntimeError('Installation is incomplete')
    payload = (ROOT/'menedzer_okien.bin').read_bytes()
    kernel = ROOT/'system_operacyjny.bin'
    if payload[:4] != b'BUR\0' or kernel.read_bytes()[:4] != b'\x7fELF':
        raise RuntimeError('Invalid final payloads')
    with disk.open('r+b') as file, mmap.mmap(file.fileno(),0) as image:
        if image[512:520] != b'EFI PART':
            raise RuntimeError('Missing GPT')
        entries,count,stride = struct.unpack_from('<QII',image,512+72)
        if count>128 or stride!=128:
            raise RuntimeError('Unsupported GPT')
        base=esp=None
        for i in range(count):
            entry=image[entries*512+i*stride:entries*512+(i+1)*stride]
            kind=entry[:16];start=struct.unpack_from('<Q',entry,32)[0]*512
            if kind==bytes.fromhex('3ce71fb214048545bd078ab4980f9c88'):base=start
            if kind==bytes.fromhex('28732ac11ff8d211ba4b00a0c93ec93b'):esp=start
        if base is None or esp is None:
            raise RuntimeError('Missing BSP2/EFI partition')
        magic,total,blocks,root,nodes,data_start=struct.unpack_from('<4sQQQQQ',image,base)
        if magic!=b'BSP2':raise RuntimeError('Invalid BSP2')
        def inode(ident):
            if not 1<=ident<=data_start-nodes:raise RuntimeError('Inode bounds')
            offset=base+(nodes+ident-1)*4096
            kind=image[offset];size=struct.unpack_from('<Q',image,offset+1)[0]
            pointers=struct.unpack_from('<490Q',image,offset+25)
            length=size*64 if kind==2 else size
            if length>1024*1024:raise RuntimeError('Direct-file fixture bound')
            chunks=[];locations=[]
            for ptr in pointers[:(length+4095)//4096]:
                if ptr>=blocks-data_start:raise RuntimeError('Data block bounds')
                at=base+(data_start+ptr)*4096
                if at+4096>len(image):raise RuntimeError('Image bounds')
                locations.append(at);chunks.append(image[at:at+4096])
            return kind,b''.join(chunks)[:length],locations
        def lookup(path):
            ident=root
            for part in path.strip('/').split('/'):
                kind,listing,_=inode(ident)
                if kind!=2:raise RuntimeError('Expected directory')
                found=None
                for offset in range(0,len(listing),64):
                    child=struct.unpack_from('<Q',listing,offset)[0]
                    name=listing[offset+8:offset+64].split(b'\0',1)[0]
                    if child and name==part.encode():found=child;break
                if found is None:raise FileNotFoundError(path)
                ident=found
            return ident
        def replace(path,content):
            ident=lookup(path);kind,previous,locations=inode(ident)
            if kind!=1 or not content or len(content)>len(locations)*4096:
                raise RuntimeError('Refusing allocation in fixture replacement: '+path)
            for block,at in enumerate(locations):
                chunk=content[block*4096:(block+1)*4096]
                if chunk:image[at:at+len(chunk)]=chunk
            # Preserve direct-block allocation, timestamps and PZB permissions.
            struct.pack_into('<Q',image,base+(nodes+ident-1)*4096+1,len(content))
            if inode(ident)[1]!=content:raise RuntimeError('Payload readback mismatch: '+path)
        replace('/menedzer_okien.bur',payload)
        settings=(ROOT/'ustawienia.bin').read_bytes()
        replace('/sloty/a/programy/ustawienia.cebula/ustawienia.bur',settings)
        if reset_preferences:
            magic=0x31465342;version=1;family=0;size=16;generation=1
            config=struct.pack('<6I',magic,version,family,size,generation,magic^version^family^size^0xb5f01937)
            replace('/sloty/a/system/bsf.cfg',config)
            try:
                ident=lookup('/ustawienia/uklad-pulpitu.cfg')
                _,previous,_=inode(ident)
                if previous:replace('/ustawienia/uklad-pulpitu.cfg',bytes(len(previous)))
            except FileNotFoundError:pass
        image.flush();os.fsync(file.fileno())
    subprocess.run(['mcopy','-o','-i',f'{disk}@@{esp}',str(kernel),'::/boot/system_operacyjny.bin'],check=True)
    check=directory/'kernel-readback.bin'
    subprocess.run(['mcopy','-o','-i',f'{disk}@@{esp}','::/boot/system_operacyjny.bin',str(check)],check=True)
    if check.read_bytes()!=kernel.read_bytes():raise RuntimeError('EFI kernel readback mismatch')
    check.unlink()
    shutil.copy2(kernel,directory/'iso/boot/system_operacyjny.bin')
    shutil.copy2(ROOT/'ustawienia.bin',directory/'iso/boot/ustawienia.bur')
    manifest=dict(kernel_sha256=hashlib.sha256(kernel.read_bytes()).hexdigest(),
                  wm_sha256=hashlib.sha256(payload).hexdigest(),preserved_direct_block_allocation=True,preferences_reset=reset_preferences)
    (directory/'final-payloads.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest
