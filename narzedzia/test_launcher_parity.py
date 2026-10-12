#!/usr/bin/env python3
"""Execute both print-only launchers; compare all canonical options and PCI order."""
import difflib,json,os,subprocess,sys
from pathlib import Path
from qemu_launcher_audit import normalize,topology
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.pointingtest/launcher-audit';OUT.mkdir(parents=True,exist_ok=True)
pwsh=os.environ.get('PWSH','pwsh')
def capture(command):return json.loads(subprocess.check_output(command,cwd=ROOT,text=True))
linux=capture(['make','-s','runuefi-argv'])
windows=capture([pwsh,'-NoProfile','-File','./runwindows.ps1','-PrintArgv','-QemuPath','qemu-system-x86_64.exe','-FirmwarePath','C:/QEMU/OVMF.fd'])
for name,data in [('linux',linux),('windows',windows)]:
 (OUT/(name+'.json')).write_text(json.dumps(data,indent=2)+'\n')
 assert len(topology(data['argv']))==7
 assert data['topology']==topology(data['argv'])

def canonical(argv):
 a=normalize(argv);result=[];host={}
 # Preserve every canonical option/value/order, permitting ONLY display/audio
 # backends, console position, VM label and firmware path. Audio PCI stays put.
 for i in range(1,len(a),2):
  option,value=a[i:i+2]
  if option in ('-display','-audiodev','-serial','-name'):
   host[option]=value
   if option=='-audiodev':assert value in ('alsa,id=snd','none,id=snd')
   continue
  result += [option,value]
 return result,host
l,lh=canonical(linux['argv']);w,wh=canonical(windows['argv'])
assert l==w, '\n'.join(difflib.unified_diff(l,w,fromfile='Linux',tofile='Windows'))
assert '-device' in l and l[l.index('-accel')+1]=='tcg,thread=single'
assert topology(linux['argv'])==topology(windows['argv'])
assert all('virtio-tablet' not in a for a in l)
# Dry-run of the real runuefi target must use precisely the audited arguments.
text=subprocess.check_output(['make','-n','runuefi'],cwd=ROOT,text=True)
import shlex
commands=[shlex.split(line) for line in text.splitlines() if line.startswith(linux['argv'][0]+' ')]
assert commands and commands[-1]==linux['argv']
(OUT/'normalized-comparison.json').write_text(json.dumps({'linux':normalize(linux['argv']),'windows':normalize(windows['argv']),'canonical':l,'intentional_host_differences':{'linux':lh,'windows':wh}},indent=2)+'\n')
print('Launcher parity: 8 checks PASS (actual Make argv, actual PowerShell argv, dry-run runtime argv, 7 PCI devices/order, all canonical properties, single TCG, relative mouse, explicit host differences)')
