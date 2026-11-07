#!/usr/bin/env python3
"""Inspect a final ISO (or extracted directory). Never emit secret values.
Requires xorriso/file and mtools for ISO/EFI extraction and file typing.
JSON is the audit baseline. EFI members use container.img!/member paths.
Third-party GRUB relocatable modules require symbol tables: explicit path exception.
"""
import argparse, hashlib, json, pathlib, re, struct, subprocess, tempfile

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
 return {'type':h[1], 'sections':result,'symbols':sum(s['entries'] for s in result if s['type'] in (2,11)), 'debug_bytes':sum(s['size'] for s in result if s['name'].startswith(('.debug','.zdebug','.stab')))}

def pe(data):
 """Read COFF symbols and the PE debug directory, including stripped EFI files."""
 if not data.startswith(b'MZ'): return None
 off=struct.unpack_from('<I',data,0x3c)[0]
 if data[off:off+4] != b'PE\0\0': raise ValueError('invalid PE signature')
 symoff, symbols=struct.unpack_from('<II',data,off+12)
 opt=off+24
 magic=struct.unpack_from('<H',data,opt)[0]
 if magic not in (0x10b,0x20b): raise ValueError('unsupported PE optional header')
 base=opt+(112 if magic==0x20b else 96)
 count=struct.unpack_from('<I',data,base-4)[0]
 debug_rva, debug_size=struct.unpack_from('<II',data,base+6*8) if count>6 else (0,0)
 return dict(symbols=symbols,symbol_offset=symoff,debug_rva=debug_rva,debug_bytes=debug_size)

def inspect(root, scan_efi=True):
 inventory=[]; findings=[]; baselines=[]
 for p in sorted(root.rglob('*')):
  rel=p.relative_to(root).as_posix()
  if p.is_symlink():
   findings.append(dict(path=rel,kind='symlink'));continue
  if not p.is_file():continue
  data=p.read_bytes(); typ=run('file','-b',str(p)).strip()
  try: e=elf(data); portable=pe(data)
  except (struct.error, IndexError, ValueError):
   findings.append(dict(path=rel,kind='malformed-executable'))
   e=None; portable=None
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
    parts=pathlib.PurePosixPath(rel).parts
    grub_module=(e['type']==1 and len(parts)==4 and parts[:2]==('boot','grub') and parts[2] in ('i386-pc','x86_64-efi') and (p.suffix=='.mod' or rel in GRUB_RUNTIME_OBJECTS))
    for s in e['sections']:
     if s['name'].startswith(('.debug','.zdebug','.stab')) or s['name'] in ('.line','.gnu_debuglink','.gnu_debugdata'):issue('debug-section',section=s['name'],size=s['size'])
     if s['type'] in (2,11) and not grub_module:issue('symbol-table',section=s['name'],count=s['entries'])
  if portable:
   if portable['symbols']:issue('symbol-table',section='COFF',count=portable['symbols'])
   if portable['debug_bytes']:issue('debug-section',section='PE debug directory',size=portable['debug_bytes'])
  for marker in DIAG:
   if marker in data:issue('diagnostic-marker',marker=marker.decode())
  ss=re.findall(rb'[\x20-\x7e]{4,}',data)
  src_ext=sum(bool(any(m.group() not in PUBLIC_SOURCE_ALLOWLIST for m in SOURCE.finditer(s))) for s in ss)
  baselines.append(dict(path=rel,strings=len(ss),source_extensions=src_ext,paths=len(paths),likely_identifiers=sum(bool(re.search(rb'_Z\w+|\b\w+::\w+|\b(?:scheduler|paging|syscall|virtio|BursztynUI|Husarz|PZB|BWS)\w*',s)) for s in ss),debug_test_assert=sum(bool(re.search(rb'\b(?:DEBUG|TEST|ASSERT)\b',s)) for s in ss),private_repo=sum(b'/Burstzyn/BursztynOS' in s for s in ss),elf=e,pe=portable))
 # GRUB's EFI boot filesystem is a second shipped filesystem. Scanning only
 # its raw bytes misses file names, symbol tables and other member metadata.
 if scan_efi:
  for p in sorted(root.rglob('efi.img')):
   if not p.is_file() or p.is_symlink(): continue
   prefix=p.relative_to(root).as_posix()+'!/'
   with tempfile.TemporaryDirectory(prefix='bursztyn-efi-') as td:
    try:
     subprocess.run(['mcopy','-s','-i',str(p.resolve()),'::*',td],
                    check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                    timeout=60)
     nested=inspect(pathlib.Path(td),scan_efi=False)
     if not nested['inventory']:
      findings.append(dict(path=prefix,kind='empty-efi-filesystem'))
     for key, target in [('inventory',inventory),('findings',findings),
                         ('string_baseline',baselines)]:
      for item in nested[key]:
       item['path']=prefix+item['path']; target.append(item)
    except (OSError,subprocess.SubprocessError):
     findings.append(dict(path=prefix,kind='efi-extraction-failed'))
 return dict(inventory=inventory,findings=findings,string_baseline=baselines)

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('artifact',type=pathlib.Path);ap.add_argument('--output',type=pathlib.Path,required=True);ap.add_argument('--report-only',action='store_true');a=ap.parse_args()
 if not a.artifact.exists(): ap.error('artifact does not exist')
 with tempfile.TemporaryDirectory(prefix='bursztyn-release-') as td:
  root=a.artifact
  if root.is_file():
   dest=pathlib.Path(td)/'iso'
   if root.suffix.lower()=='.iso':run('xorriso','-osirrox','on','-indev',str(root),'-extract','/',str(dest))
   else:dest.mkdir(parents=True,exist_ok=True);(dest/root.name).write_bytes(root.read_bytes())
   root=dest
  result=inspect(root)
  if not result['inventory']: result['findings'].append(dict(path='.',kind='empty-artifact'))
  result['artifact_sha256']=sha(a.artifact.read_bytes()) if a.artifact.is_file() else None
  a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
  print(f"Release audit: {len(result['inventory'])} files, {len(result['findings'])} findings; {a.output}")
  return 0 if a.report_only or not result['findings'] else 1
if __name__=='__main__':raise SystemExit(main())
