#!/usr/bin/env python3
"""Strip public kernel; archive matching developer ELF privately; prove PT_LOAD equality."""
import hashlib,json,pathlib,shutil,struct,subprocess,sys

def loads(data):
 h=struct.unpack_from('<16sHHIQQQIHHHHHH',data)
 result=[]
 for i in range(h[10]):
  p=struct.unpack_from('<IIQQQQQQ',data,h[5]+i*h[9])
  if p[0]==1: result.append((p[1],p[3],p[4],p[5],p[6],p[7],hashlib.sha256(data[p[2]:p[2]+p[5]]).hexdigest()))
 return (h[4],result)

def main():
 src,dst=map(pathlib.Path,sys.argv[1:3]);objcopy=sys.argv[3] if len(sys.argv)>3 else 'objcopy'
 dst.parent.mkdir(parents=True,exist_ok=True)
 subprocess.run([objcopy,'--strip-all',str(src),str(dst)],check=True)
 assert loads(src.read_bytes())==loads(dst.read_bytes()),'Strip changed entry point or load segments'
 digest=hashlib.sha256(dst.read_bytes()).hexdigest()
 private=pathlib.Path('.release-private')/digest;private.mkdir(parents=True,exist_ok=True)
 shutil.copy2(src,private/'kernel.elf')
 with (private/'kernel.nm').open('w') as f:subprocess.run(['nm','-n','-C',str(src)],stdout=f,check=True)
 (private/'manifest.json').write_text(json.dumps(dict(public_kernel_sha256=digest,private_elf_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),load_segments_identical=True),indent=2)+'\n')
 print('Public kernel:',digest,'private symbols:',private)
if __name__=='__main__':main()
