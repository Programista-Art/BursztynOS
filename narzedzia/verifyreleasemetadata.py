#!/usr/bin/env python3
"""Inspect a final ISO (or extracted directory). Never emit secret values.
Requires xorriso/file/readelf for ISO/type/ELF inspection. JSON is the audit baseline.
Third-party GRUB relocatable modules require symbol tables: explicit path exception.
"""
import argparse, hashlib, json, pathlib, re, subprocess, tempfile

def sha(data): return hashlib.sha256(data).hexdigest()
def run(*args): return subprocess.check_output(args, stderr=subprocess.DEVNULL).decode('utf-8','replace')
SECRET = {
 'private-key': rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----[\r\n]+[A-Za-z0-9+/=\r\n]{40,}-----END (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----',
 'github-token': rb'\b(?:gh[pousr]_[A-Za-z0-9]{36,255}|github_pat_[A-Za-z0-9_]{60,255})\b',
 'aws-access-key': rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b',
 'credential-url': rb'[a-zA-Z]+://[^\s/:]{1,80}:[^\s/@]{1,100}@',
}
PATH = re.compile(rb'(?:/(?:home|workspace|build|src|tmp|mnt)/[^\s\x00]{1,240}|[A-Za-z]:[\\/]Users[\\/][^\s\x00]{1,240})')
SOURCE = re.compile(rb'\b[A-Za-z_][A-Za-z0-9_./-]{2,}\.(?:cpp|hpp|asm|c|h|S)\b')
PUBLIC_SOURCE_ALLOWLIST = {b'extronic16B_unicode.h'}
DIAG = (b'[ABS MAP]', b'[VINPUT FORENSIC]', b'BURSZTYNDIAGABSMAP', b'BURSZTYNDIAGVIRTIOINPUTFORENSIC')
BAD_SUFFIX = {'.o','.obj','.a','.map','.sym','.pdb','.debug','.dSYM','.gcda','.gcno','.log','.bak','.tmp','.cpp','.hpp','.c','.h','.asm','.S','.py','.su','.d'}
BAD_DIR = {'tests','debug','tmp','logs','symbols','build','.bsf-test','.virtiotest','.wm-test','.husarz-test','.git','.release-private','.release-audit'}
GRUB_RUNTIME_OBJECTS = {'boot/grub/i386-pc/efiemu32.o','boot/grub/i386-pc/efiemu64.o'}

def elf(data):
 import struct
 if not data.startswith(b'\x7fELF'): return None
 if data[5] != 1 or data[4] not in (1,2): return {'unsupported': True}
 wide=data[4]==2
 h=struct.unpack_from('<16sHHIQQQIHHHHHH' if wide else '<16sHHIIIIIHHHHHH',data)
 off, entsz, count, names=h[6],h[11],h[12],h[13]
 sections=[struct.unpack_from('<IIQQQQIIQQ' if wide else '<IIIIIIIIII',data,off+i*entsz) for i in range(count)]
 st=sections[names]; strings=data[st[4]:st[4]+st[5]]
 result=[]
 for s in sections:
  name=strings[s[0]:].split(b'\0',1)[0].decode('ascii','replace')
  result.append(dict(name=name,type=s[1],flags=s[2],size=s[5],offset=s[4],entries=s[5]//s[9] if s[9] else 0))
 return {'sections':result,'symbols':sum(s['entries'] for s in result if s['type'] in (2,11)), 'debug_bytes':sum(s['size'] for s in result if s['name'].startswith(('.debug','.zdebug','.stab')))}

def inspect(root):
 inventory=[]; findings=[]; baselines=[]
 for p in sorted(root.rglob('*')):
  rel=p.relative_to(root).as_posix()
  if p.is_symlink():
   findings.append(dict(path=rel,kind='symlink'));continue
  if not p.is_file():continue
  data=p.read_bytes(); typ=run('file','-b',str(p)).strip(); e=elf(data)
  third=rel.startswith('boot/grub/') or rel=='boot/installer/BOOTX64.EFI' or rel=='efi.img'
  inventory.append(dict(path=rel,size=len(data),sha256=sha(data),type=typ,magic=data[:16].hex(),executable=bool(e or data.startswith((b'MZ',b'BUR\0'))),compression=next((x for x in ('gzip','XZ','bzip2','compressed') if x in typ),None)))
  def issue(kind,**kw):findings.append(dict(path=rel,kind=kind,**kw))
  if (p.suffix in BAD_SUFFIX and rel not in GRUB_RUNTIME_OBJECTS) or BAD_DIR.intersection(pathlib.PurePosixPath(rel).parts):issue('development-file')
  for category,pattern in SECRET.items():
   for m in re.finditer(pattern,data):issue('secret-pattern',category=category,offset=m.start(),fingerprint=sha(m.group())[:16])
  paths=[dict(offset=m.start(),value=m.group().decode('utf-8','replace')) for m in PATH.finditer(data)]
  # No raw suspect strings in reports; paths are represented by fingerprints.
  for m in paths:issue('source-path',offset=m['offset'],fingerprint=sha(m['value'].encode())[:16])
  if e:
   if e.get('unsupported'):issue('unsupported-elf')
   else:
    for s in e['sections']:
     if s['name'].startswith(('.debug','.zdebug','.stab')) or s['name'] in ('.line','.gnu_debuglink','.gnu_debugdata'):issue('debug-section',section=s['name'],size=s['size'])
     if s['type'] in (2,11) and not third:issue('symbol-table',section=s['name'],count=s['entries'])
  if not third:
   for marker in DIAG:
    if marker in data:issue('diagnostic-marker',marker=marker.decode())
  ss=re.findall(rb'[\x20-\x7e]{4,}',data)
  src_ext=sum(bool(any(m.group() not in PUBLIC_SOURCE_ALLOWLIST for m in SOURCE.finditer(s))) for s in ss)
  baselines.append(dict(path=rel,strings=len(ss),source_extensions=src_ext,paths=len(paths),likely_identifiers=sum(bool(re.search(rb'_Z\w+|\b\w+::\w+|\b(?:scheduler|paging|syscall|virtio|BursztynUI|Husarz|PZB|BWS)\w*',s)) for s in ss),debug_test_assert=sum(bool(re.search(rb'\b(?:DEBUG|TEST|ASSERT)\b',s)) for s in ss),private_repo=sum(b'/Burstzyn/BursztynOS' in s for s in ss),elf=e))
 return dict(inventory=inventory,findings=findings,string_baseline=baselines)

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('artifact',type=pathlib.Path);ap.add_argument('--output',type=pathlib.Path,required=True);ap.add_argument('--report-only',action='store_true');a=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bursztyn-release-') as td:
  root=a.artifact
  if root.is_file():
   dest=pathlib.Path(td)/'iso'
   if root.suffix.lower()=='.iso':run('xorriso','-osirrox','on','-indev',str(root),'-extract','/',str(dest))
   else:dest.mkdir(parents=True,exist_ok=True);(dest/root.name).write_bytes(root.read_bytes())
   root=dest
  result=inspect(root)
  result['artifact_sha256']=sha(a.artifact.read_bytes()) if a.artifact.is_file() else None
  a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
  print(f"Release audit: {len(result['inventory'])} files, {len(result['findings'])} findings; {a.output}")
  return 0 if a.report_only or not result['findings'] else 1
if __name__=='__main__':raise SystemExit(main())
