#!/usr/bin/env python3
"""Bounded, flat trusted-font packaging. Preserve font bytes and notices."""
import pathlib,sys,shutil,re
from bsf_release_assets import assets
root=pathlib.Path(__file__).resolve().parents[1]
stage=pathlib.Path(sys.argv[1]);dest=stage/'czcionki/ttf';dest.mkdir(parents=True,exist_ok=True)
source=root/'czcionki/ttf'
if dest.resolve()==source.resolve():raise SystemExit('Refusing source directory as staging')
for old in dest.iterdir():
 if old.is_file() and old.suffix.lower() in ('.ttf','.txt'):old.unlink()
release=assets(root)
fonts=[p for p in release if p.suffix.lower()=='.ttf']
if len(fonts)>32:raise SystemExit('BSF: maximum 32 font files')
notices=[p for p in release if p.suffix.lower()=='.txt']
if len(notices)>4 or any(p.stat().st_size>128*1024 for p in notices):raise SystemExit('BSF: notice limit exceeded')
if sum(p.stat().st_size for p in fonts+notices)>16*1024*1024:raise SystemExit('BSF: asset budget exceeded')
for p in fonts+notices:
 if not re.fullmatch(r'[A-Za-z0-9_.-]+',p.name):raise SystemExit('Invalid asset filename')
 shutil.copy2(p,dest/p.name)
for p in fonts:
 if not re.fullmatch(r'[A-Za-z0-9_.-]+\.ttf',p.name,re.I) or not 12<=p.stat().st_size<=2*1024*1024:raise SystemExit('Invalid trusted font asset: '+p.name)
shutil.copy2(root/'ustawienia.bin',stage/'boot/ustawienia.bur')
cfg=stage/'boot/grub/grub.cfg';s=cfg.read_text();s=re.sub(r'^.*module2 /boot/ustawienia.bur.*\n','',s,flags=re.M);s=re.sub(r'^.*module2 /czcionki/ttf/.*\n','',s,flags=re.M)
modules='    module2 /boot/ustawienia.bur bsf-settings\n'+''.join('    module2 /czcionki/ttf/'+p.name+' bsf/'+p.name+'\n' for p in sorted(dest.iterdir()) if p.is_file())
s=re.sub(r'(?m)^([ \t]*)boot\s*$',lambda m:modules+m.group(1)+'boot',s);cfg.write_text(s)
print('BSF: packaged',len(fonts),'TTF files and license notices')
