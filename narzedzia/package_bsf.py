#!/usr/bin/env python3
"""Bounded, flat trusted-font packaging. Preserve font bytes and notices.

BSF 2.2: fonts may come from czcionki/{ttf,otf,ttc,otc}; each is staged in
the matching /czcionki/<dir> and announced as a Multiboot2 module labelled
bsf/<name> (the kernel routes it by extension, bsf/bootstrap.inc)."""
import pathlib,sys,shutil,re
from bsf_release_assets import assets
root=pathlib.Path(__file__).resolve().parents[1]
stage=pathlib.Path(sys.argv[1])
FONT={'.ttf':'ttf','.otf':'otf','.ttc':'ttc','.otc':'otc'}
for d in FONT.values():
    dest=stage/'czcionki'/d
    if dest.resolve()==(root/'czcionki'/d).resolve():raise SystemExit('Refusing source directory as staging')
    if dest.exists():
        for old in dest.iterdir():
            if old.is_file() and old.suffix.lower() in tuple(FONT)+('.txt',):old.unlink()
release=assets(root)
fonts=[p for p in release if p.suffix.lower() in FONT]
notices=[p for p in release if p.suffix.lower()=='.txt']
if len(fonts)>32:raise SystemExit('BSF: maximum 32 font files')
if len(notices)>8 or any(p.stat().st_size>128*1024 for p in notices):raise SystemExit('BSF: notice limit exceeded')
if sum(p.stat().st_size for p in fonts+notices)>16*1024*1024:raise SystemExit('BSF: asset budget exceeded')
names=set()
for p in fonts+notices:
    if not re.fullmatch(r'[A-Za-z0-9_.-]+',p.name):raise SystemExit('Invalid asset filename')
    if p.name.lower() in names:raise SystemExit('Duplicate asset filename (one flat module namespace)')
    names.add(p.name.lower())
    if p.suffix.lower() in FONT and not 12<=p.stat().st_size<=8*1024*1024:raise SystemExit('Invalid trusted font asset: '+p.name)
    target=stage/'czcionki'/(FONT.get(p.suffix.lower(),'ttf'))
    target.mkdir(parents=True,exist_ok=True)
    shutil.copy2(p,target/p.name)
shutil.copy2(root/'ustawienia.bin',stage/'boot/ustawienia.bur')
cfg=stage/'boot/grub/grub.cfg';s=cfg.read_text();s=re.sub(r'^.*module2 /boot/ustawienia.bur.*\n','',s,flags=re.M);s=re.sub(r'^.*module2 /czcionki/(ttf|otf|ttc|otc)/.*\n','',s,flags=re.M)
staged=sorted((p for d in FONT.values() if (stage/'czcionki'/d).exists() for p in (stage/'czcionki'/d).iterdir() if p.is_file()),key=lambda p:p.name)
modules='    module2 /boot/ustawienia.bur bsf-settings\n'+''.join('    module2 /czcionki/'+p.parent.name+'/'+p.name+' bsf/'+p.name+'\n' for p in staged)
s=re.sub(r'(?m)^([ \t]*)boot\s*$',lambda m:modules+m.group(1)+'boot',s);cfg.write_text(s)
print('BSF: packaged',len(fonts),'font files and license notices')
