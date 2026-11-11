#!/usr/bin/env python3
"""Export observer addresses, or capture bounded graphics metrics over QMP TCP.

On build host: --export-symbols graphics-symbols.json --kernel system_operacyjny.bin
On Windows: --symbols graphics-symbols.json --port 4444 --output win-gtk.json
QMP reads guest memory; it never writes guest memory or changes guest settings.
"""
import argparse, hashlib, json, re, socket, statistics, struct, subprocess, time
from pathlib import Path
NAMES='frames compose_ns present_ns rects pixels full_frames copy_ns copy_bytes gpu_wait_ns gpu_commands transfers transfer_pixels flushes flush_pixels framebuffer_attempts framebuffer_creates scanouts host_polls host_hint_changes mode_requests mode_commits inputs input_samples input_ns input_max_ns cursor_presents'.split()
SYMBOLS=['GraphicsPerf::graphics_perf','GraphicsPerf::graphics_modes','GraphicsPerf::graphics_resources','committed_geometry','GraphicsPerf::graphics_hints']
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--export-symbols',type=Path);p.add_argument('--kernel',type=Path,default=Path('system_operacyjny.bin'))
 p.add_argument('--symbols',type=Path);p.add_argument('--port',type=int,default=4444);p.add_argument('--seconds',type=int,default=60);p.add_argument('--output',type=Path)
 a=p.parse_args()
 if a.export_symbols:
  nm=subprocess.check_output(['nm','-C',str(a.kernel)],text=True);m={}
  for name in SYMBOLS:
   found=re.search(r'^([0-9a-f]+) [bBdDu] '+re.escape(name)+r'$',nm,re.M)
   if not found:raise RuntimeError('Missing '+name)
   m[name]=int(found[1],16)
  a.export_symbols.write_text(json.dumps(dict(kernel_sha256=hashlib.sha256(a.kernel.read_bytes()).hexdigest(),addresses=m),indent=2)+'\n');return
 if not a.symbols or not a.output:p.error('--symbols and --output required')
 mapping=json.loads(a.symbols.read_text());addresses=mapping['addresses']
 with socket.create_connection(('127.0.0.1',a.port),timeout=20) as s:
  f=s.makefile('rb');f.readline()
  def call(command,args=None):
   req={'execute':command}
   if args is not None:req['arguments']=args
   s.sendall((json.dumps(req)+'\n').encode())
   while True:
    r=json.loads(f.readline())
    if 'error' in r:raise RuntimeError(r)
    if 'return' in r:return r['return']
  call('qmp_capabilities')
  def read(name,n):
   raw=call('human-monitor-command',{'command-line':f'xp /{n}gx 0x{addresses[name]:x}'})
   values=[]
   for line in raw.splitlines():
    if ':' in line:values.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',line.split(':',1)[1]))
   if len(values)!=n:raise RuntimeError('Memory read failed')
   return values
  def sample():
   call('stop')
   try:
    perf=read(SYMBOLS[0],2+len(NAMES)+128*6)
    if perf[0]!=1:raise RuntimeError('Graphics telemetry is not initialized or the symbol map does not match; wait for the desktop and verify the ISO checksum')
    modes=read(SYMBOLS[1],161);resources=read(SYMBOLS[2],4);g=read('committed_geometry',8);hints=read('GraphicsPerf::graphics_hints',161)
   finally:call('cont')
   return dict(host_time=time.monotonic(),counters=dict(zip(NAMES,perf[1:1+len(NAMES)])),sequence=perf[1+len(NAMES)],frames=[perf[2+len(NAMES)+i*6:2+len(NAMES)+(i+1)*6] for i in range(128)],modes=modes,hints=hints,resources=resources,geometry=list(struct.unpack('<16I',struct.pack('<8Q',*g))))
  records=[];deadline=time.monotonic()+a.seconds
  print('Recording. Exercise idle, drag, resize, minimize and reopen now.',flush=True)
  while True:
   records.append(sample())
   if time.monotonic()>=deadline:break
   time.sleep(min(5,max(0,deadline-time.monotonic())))
  intervals=[]
  for old,new in zip(records,records[1:]):
   elapsed=new['host_time']-old['host_time'];frames=new['sequence']-old['sequence']
   delta={k:new['counters'][k]-old['counters'][k] for k in NAMES if k!='input_max_ns'}
   inputs=delta['input_samples']
   intervals.append(dict(host_seconds=elapsed,fps=frames/elapsed if elapsed else 0,
    compose_mean_ms=delta['compose_ns']/frames/1e6 if frames else 0,
    present_mean_ms=delta['present_ns']/frames/1e6 if frames else 0,
    input_mean_ms=delta['input_ns']/inputs/1e6 if inputs else 0,
    dirty_rectangles=delta['rects'],dirty_pixels=delta['pixels'],full_frames=delta['full_frames'],
    framebuffer_creates=delta['framebuffer_creates'],mode_commits=delta['mode_commits'],
    copy_bytes=delta['copy_bytes'],gpu_wait_ms=delta['gpu_wait_ns']/1e6,
    graphics_live_bytes=new['resources'][0],geometry=new['geometry']))
  result=dict(kernel_sha256=mapping['kernel_sha256'],samples=records,intervals=intervals,notes='Elapsed guest ns; latency starts at consumed input and ends at GPU submission, not host photons. Stop/cont brackets read-only coherent observations. Resource fields: live_bytes, peak_bytes, allocations, frees. Mode source: 1 boot, 2 config, 3 guest, 4 host, 5 rollback.')
  a.output.write_text(json.dumps(result,indent=2)+'\n')
 print(a.output)
if __name__=='__main__':main()
