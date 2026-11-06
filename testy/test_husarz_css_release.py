#!/usr/bin/env python3
"""Run native Husarz regression groups against one immutable release image."""
import argparse
import concurrent.futures
import hashlib
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
GROUPS={
 'css':(['--css-only'],1500),
 'full':([],2100),
 'heavy':(['--performance'],2100),
 'layout':(['--layout-only'],1200),
 'editing':(['--editing-only'],1800),
 'javascript':(['--javascript-only'],2700),
 'modern':(['--modern-javascript-only'],3000),
 'storage':(['--javascript-storage-only'],1800),
 'cancellation':(['--javascript-cancellation-only'],1500),
 'real-sites':(['--real-css'],1800),
}
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--iso',type=Path,required=True);p.add_argument('--kernel',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True);p.add_argument('--jobs',type=int,default=3)
 p.add_argument('--groups',nargs='+',choices=GROUPS,default=[g for g in GROUPS if g!='real-sites'])
 args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
 hashes={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in [('iso',args.iso),('kernel',args.kernel)]}
 results={}
 def run(name):
  flags,timeout=GROUPS[name];destination=out/name;destination.mkdir(exist_ok=True)
  command=[sys.executable,'-u',str(ROOT/'testy/test_husarz_qemu.py'),'--iso',str(args.iso.resolve()),'--kernel',str(args.kernel.resolve()),'--output',str(destination),'--timeout',str(timeout),*flags]
  print('START',name,flush=True)
  with (destination/'driver.log').open('w') as log:
   status=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
  path=destination/'result.json';data=json.loads(path.read_text()) if path.exists() else {}
  expected='OBSERVED' if name=='real-sites' else 'PASS'
  ok=status==0 and data.get('result')==expected and data.get('iso_sha256')==hashes['iso'] and data.get('kernel_sha256')==hashes['kernel']
  print('FINISH',name,expected if ok else 'FAIL',flush=True)
  return name,{'ok':ok,'returncode':status,'result':data}
 with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.jobs,3))) as pool:
  for future in concurrent.futures.as_completed([pool.submit(run,name) for name in args.groups]):
   name,result=future.result();results[name]=result
   (out/'summary.json').write_text(json.dumps({'hashes':hashes,'groups':results},indent=2)+'\n')
 if not all(v['ok'] for v in results.values()):return 1
 return 0
if __name__=='__main__':sys.exit(main())
