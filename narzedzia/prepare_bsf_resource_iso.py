#!/usr/bin/env python3
"""Private native resource fixtures; audited production assets stay untouched."""
from pathlib import Path
import argparse,shutil,struct,subprocess
from prepare_bsf_live_font_fixture import font
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('mode',choices=['catalog128','large16']);p.add_argument('directory',type=Path);a=p.parse_args()
out=a.directory.resolve()
if not out.is_relative_to((ROOT/'.bsf-test').resolve()) or out.exists():raise RuntimeError('New private .bsf-test directory required')
out.mkdir();stage=out/'staging';shutil.copytree(ROOT/'isodir',stage)
fontdir=stage/'czcionki/ttf'
if a.mode=='catalog128':
    for i in range(124):(fontdir/f'test-{i:03}.ttf').write_bytes(font(f'Bursztyn Test {i:03}'))
else:
    data=bytearray((fontdir/'DejaVuSans.ttf').read_bytes());end=12+struct.unpack_from('>H',data,4)[0]*16
    padding=16*1024*1024-32-len(data);data[end:end]=bytes(padding)
    head=None
    for i in range(struct.unpack_from('>H',data,4)[0]):
        at=12+i*16;off=struct.unpack_from('>I',data,at+8)[0]+padding;struct.pack_into('>I',data,at+8,off)
        if data[at:at+4]==b'head':head=off
    def checksum(b):
        b=bytes(b)+bytes((-len(b))%4);return sum(struct.unpack('>'+str(len(b)//4)+'I',b))&0xffffffff
    struct.pack_into('>I',data,head+8,0)
    for i in range(struct.unpack_from('>H',data,4)[0]):
        at=12+i*16;off,n=struct.unpack_from('>II',data,at+8);struct.pack_into('>I',data,at+4,checksum(data[off:off+n]))
    struct.pack_into('>I',data,head+8,(0xb1b0afba-checksum(data))&0xffffffff)
    for f in fontdir.glob('*.ttf'):f.unlink()
    (fontdir/'DejaVuSans.ttf').write_bytes(data)
fonts=sorted(fontdir.glob('*.ttf'));assert len(fonts)==(128 if a.mode=='catalog128' else 1)
modules='\n'.join(f' module2 /czcionki/ttf/{f.name} bsf/{f.name}' for f in fonts)
(stage/'boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Native BSF resources" {\n insmod all_video\n set gfxmode=1280x720x32,auto\n set gfxpayload=keep\n multiboot2 /boot/system_operacyjny.bin\n module2 /boot/tapety.pak tapety.pak\n module2 /boot/ustawienia.bur bsf-settings\n'+modules+'\n boot\n}\n')
with (out/'iso-build.log').open('w') as log:subprocess.run(['grub-mkrescue','-o',str(out/'fixture.iso'),str(stage)],stdout=log,stderr=subprocess.STDOUT,check=True)
print(out/'fixture.iso')
