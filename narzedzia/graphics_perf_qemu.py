#!/usr/bin/env python3
"""Native production graphics benchmark. Read-only QMP observations, real input.

No data disks attach. High resolution fixtures replace only GRUB configuration;
use --production-iso to test the exact release ISO with its normal boot mode.
"""
import argparse, hashlib, json, os, re, shutil, socket, statistics, struct, subprocess, sys, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'testy'))
from test_hda_regression_qemu import Qmp
NAMES='frames compose_ns present_ns rects pixels full_frames copy_ns copy_bytes gpu_wait_ns gpu_commands transfers transfer_pixels flushes flush_pixels framebuffer_attempts framebuffer_creates scanouts host_polls host_hint_changes mode_requests mode_commits inputs input_samples input_ns input_max_ns cursor_presents'.split()
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--iso',type=Path,default=ROOT/'BursztynOS.iso');p.add_argument('--kernel',type=Path,default=ROOT/'system_operacyjny.bin')
 p.add_argument('--output',type=Path,required=True);p.add_argument('--mode',default='1280x720');p.add_argument('--timeout',type=int,default=900)
 p.add_argument('--capture-port',type=int,default=0);p.add_argument('--host-resize',action='store_true');p.add_argument('--lifecycle',action='store_true');p.add_argument('--production-iso',action='store_true');p.add_argument('--display',default='none');p.add_argument('--drags',type=int,default=24)
 a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);w,h=map(int,a.mode.split('x'))
 symbols=subprocess.check_output(['nm','-C',str(a.kernel)],text=True)
 def symbol(s):
  m=re.search(r'^([0-9a-f]+) [bBdDu] '+re.escape(s)+r'$',symbols,re.M)
  if not m:raise RuntimeError('Missing observer '+s)
  return int(m[1],16)
 perf=symbol('GraphicsPerf::graphics_perf');geometry=symbol('committed_geometry');layers=symbol('tablica_warstw');titles=symbol('(anonymous namespace)::tytul_okna')
 cursor=symbol('mysz_x');cy=symbol('mysz_y');buttons=symbol('zaktualizuj_mysze_wspolne(long, long, unsigned char, char const*)::poprzednie_przyciski_gui')
 iso=a.iso.resolve()
 if not a.production_iso:
  cfg=out/'grub.cfg';cfg.write_text(f'set timeout=0\nset default=0\nmenuentry "Graphics measurement {w}x{h}" {{\ninsmod all_video\nset gfxmode={w}x{h}x32\nset gfxpayload=keep\nmultiboot2 /boot/system_operacyjny.bin\nmodule2 /boot/tapety.pak tapety.pak\nboot\n}}\n')
  iso=out/'mode.iso'
  with tempfile.TemporaryDirectory(prefix='graphics-iso-') as tmp:
   staging=Path(tmp)/'staging'
   with (out/'iso-build.log').open('w') as log:
    subprocess.run(['xorriso','-osirrox','on','-indev',str(a.iso.resolve()),'-extract','/',str(staging)],stdout=log,stderr=subprocess.STDOUT,check=True)
    packaged=staging/'boot/grub/grub.cfg'
    original=packaged.read_text()
    original=re.sub(r'set gfxmode=[^\n]+',f'set gfxmode={w}x{h}x32',original)
    original=re.sub(r'set timeout=\d+','set timeout=0',original)
    original=re.sub(r' bursztyn.video=[0-9]+x[0-9]+','',original)
    original=original.replace('multiboot2 /boot/system_operacyjny.bin',f'multiboot2 /boot/system_operacyjny.bin bursztyn.video={w}x{h}')
    cfg.write_text(original);packaged.chmod(0o644)
    shutil.copy2(cfg,packaged)
    subprocess.run(['grub-mkrescue','-o',str(iso),str(staging)],stdout=log,stderr=subprocess.STDOUT,check=True)
 # AF_UNIX socket paths are limited independently of output-directory paths.
 socket_dir=tempfile.TemporaryDirectory(prefix='graphics-qmp-')
 serial=out/'serial.log';sock=Path(socket_dir.name)/'qmp.sock'
 # Same canonical PCI order as both launchers. No writable media.
 cmd=['qemu-system-x86_64','-machine','pc,hpet=on','-cpu','max','-accel','tcg,thread=single','-smp','1','-m','2G','-bios','/usr/share/ovmf/OVMF.fd','-display',a.display,'-serial',f'file:{serial}','-qmp',f'unix:{sock},server=on,wait=off','-no-reboot','-boot','d','-audiodev','none,id=snd','-device','intel-hda','-device','hda-output,audiodev=snd','-drive',f'file={iso},if=none,id=boot,format=raw,readonly=on','-device','virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1','-netdev','user,id=n1','-device','virtio-net-pci,netdev=n1,disable-legacy=on','-object','rng-random,id=rng0,filename=/dev/urandom','-device','virtio-rng-pci,rng=rng0,disable-legacy=on','-vga','none','-device',f'virtio-vga,id=gpu0,disable-legacy=on,xres={w},yres={h}','-device','virtio-keyboard-pci','-device','virtio-mouse-pci']
 if a.capture_port:cmd+=['-qmp',f'tcp:127.0.0.1:{a.capture_port},server=on,wait=off']
 (out/'command.json').write_text(json.dumps(cmd,indent=2));deadline=time.monotonic()+a.timeout;q=None;result=dict(result='FAIL',iso_sha256=digest(iso),source_iso_sha256=digest(a.iso),kernel_sha256=digest(a.kernel),mode=[w,h],display=a.display)
 with (out/'qemu.log').open('w') as log:
  proc=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT)
  try:
   def health():
    text=serial.read_text(errors='replace') if serial.exists() else ''
    if proc.poll() is not None or any(x in text for x in ['[KERNEL-PANIC]','[PF]','[USER FAULT]','BLAD KRYTYCZNY: Timeout']):raise RuntimeError(text[-2000:])
    if time.monotonic()>deadline:raise TimeoutError(text[-2000:])
    return text
   def wait(fn,label,seconds=90):
    until=min(deadline,time.monotonic()+seconds)
    while time.monotonic()<until:
     health()
     if fn():return
     time.sleep(.025)
    raise TimeoutError(label)
   wait(lambda:'[DIAG APP] First frame submitted' in health(),'desktop',a.timeout)
   q=Qmp(sock);q.socket.settimeout(20)
   def call(c,args=None):
    r=q.call(c,args)
    if 'error' in r:raise RuntimeError(r)
    return r['return']
   def hmp(c):return call('human-monitor-command',{'command-line':c})
   def read(addr,n):
    # Small QMP reads do not block the GPU backend with large text dumps.
    data=[]
    for line in hmp(f'xp /{n}bx 0x{addr:x}').splitlines():
     if ':' in line:data.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',line.split(':',1)[1]))
    if len(data)!=n:raise RuntimeError(('read',len(data),n))
    return bytes(data)
   def pos():return [struct.unpack('<i',read(addr,4))[0] for addr in (cursor,cy)]
   def geom():return struct.unpack('<16I',read(geometry,64))
   def process_cpu():
    stat=Path(f'/proc/{proc.pid}/stat').read_text().split()
    return (int(stat[13])+int(stat[14]))/os.sysconf('SC_CLK_TCK')
   def snapshot(label):
    call('stop')
    try:
     file=out/(label+'.bin');call('pmemsave',{'val':perf,'size':8*(1+len(NAMES)+1+128*6),'filename':str(file)})
     g=geom();mem=read(layers,16*40)
     extra={}
     for name,count,key in [('GraphicsPerf::graphics_resources',4,'resources'),('GraphicsPerf::graphics_modes',161,'modes')]:
      found=re.search(r'^([0-9a-f]+) [bBdDu] '+re.escape(name)+r'$',symbols,re.M)
      if found:extra[key]=list(struct.unpack('<'+str(count)+'Q',read(int(found[1],16),count*8)))
    finally:call('cont')
    data=file.read_bytes();v=struct.unpack_from('<'+str(2+len(NAMES))+'Q',data);seq=v[-1]
    frames=[struct.unpack_from('<6Q',data,8*(2+len(NAMES))+i*48) for i in range(128)]
    file.unlink()
    return dict(counters=dict(zip(NAMES,v[1:-1])),sequence=seq,frames=frames,geometry=list(g),layers=mem.hex(),host_time=time.monotonic(),qemu_cpu_s=process_cpu(),**extra)
   def shot(label):
    call('stop')
    try:call('screendump',{'filename':str(out/(label+'.ppm'))})
    finally:call('cont')
   def move(x,y):
    old=pos();hmp(f'mouse_move {x-old[0]} {y-old[1]}');wait(lambda:pos()==[x,y],'pointer',45)
   def press(v):
    hmp(f'mouse_button {v}');wait(lambda:read(buttons,1)[0]==v,'buttons')
   def key(c):
    call('send-key',{'keys':[{'type':'qcode','data':c}],'hold-time':80});time.sleep(.3)
   mice=call('query-mice');mouse=next(m for m in mice if 'virtio' in m['name'].lower());hmp(f'mouse_set {mouse["index"]}')
   time.sleep(3);initial=snapshot('initial');shot('desktop')
   assert initial['geometry'][2:4]==[w,h],initial['geometry']
   # Drag the ordinary autostart diagnostics window; no test-only guest path.
   def window():
    names=read(titles,16*48);meta=read(layers,16*40)
    for pid in range(1,16):
     title=names[pid*48:(pid+1)*48].split(b'\0')[0].decode('utf-8','replace')
     if 'Diagnostyka' in title:
      _,z,x,y,ww,hh=struct.unpack_from('<6i',meta,pid*40)
      return dict(pid=pid,z=z,x=x,y=y,w=ww,h=hh)
   wait(lambda:window() is not None,'diagnostics opens',180);time.sleep(3);shot('window')
   win=window();move(win['x']+100,win['y']+14);press(1);time.sleep(.5)
   before=snapshot('before');positions=[];consumption_ms=[];host_start=time.monotonic()
   for i in range(a.drags):
    x=win['x']+100+(i%8)*12;y=win['y']+14+((i//8)%2)*60
    injected=time.monotonic();move(x,y);consumption_ms.append((time.monotonic()-injected)*1000);positions.append(pos());time.sleep(.025)
   press(0);time.sleep(2);after=snapshot('after');shot('dragged')
   time.sleep(3);idle=snapshot('idle');shot('idle')
   assert after['geometry']==before['geometry']==idle['geometry'],'unstable geometry during drag/idle'
   delta={k:after['counters'][k]-before['counters'][k] for k in NAMES if k!='input_max_ns'}
   elapsed=after['host_time']-before['host_time'];frames=after['sequence']-before['sequence'];sample=[after['frames'][i%128] for i in range(max(before['sequence'],after['sequence']-128),after['sequence'])]
   def summary(values):
    if not values:return {}
    v=sorted(values);return dict(count=len(v),mean=statistics.mean(v),p50=statistics.median(v),p95=v[min(len(v)-1,int(.95*len(v)))],maximum=max(v))
   result.update(result='PASS',initial=initial,before=before,after=after,idle=idle,delta=delta,host_elapsed_s=elapsed,qemu_cpu_s=after.get('qemu_cpu_s',0)-before.get('qemu_cpu_s',0),host_fps=frames/elapsed,frame_compose_ns=summary([f[1] for f in sample]),frame_present_ns=summary([f[2] for f in sample]),frame_total_ns=summary([f[1]+f[2] for f in sample]),input_to_submit_ns=summary([f[5] for f in sample if f[5]]),positions=positions,input_consumption_ms=summary(consumption_ms),window_before=win,window_after=window())
   assert result['window_after']['x']!=win['x'] or result['window_after']['y']!=win['y'],'drag did not move window'
   if a.capture_port:
    observer=ROOT/'narzedzia/graphics_perf_capture.py'
    subprocess.run([sys.executable,str(observer),'--export-symbols',str(out/'symbols.json'),'--kernel',str(a.kernel)],check=True)
    subprocess.run([sys.executable,str(observer),'--symbols',str(out/'symbols.json'),'--port',str(a.capture_port),'--seconds','1','--output',str(out/'tcp-metrics.json')],check=True)
   if a.lifecycle:
    from PIL import Image
    old_cursor=pos();new_cursor=[min(w-20,old_cursor[0]+60),min(h-20,old_cursor[1]+40)]
    submitted=time.monotonic();move(*new_cursor)
    scene_pointer=symbol('backbuffer')
    def cursor_visible():
     shot('cursor-after')
     with Image.open(out/'cursor-after.ppm') as im:
      x,y=new_cursor
      if im.getpixel((x,y))!=(0,0,0) or im.getpixel((x+1,y+1))!=(255,255,255):return False
      pointer=struct.unpack('<Q',read(scene_pointer,8))[0]
      # Supervisor scene lives at a virtual address: HMP x reads virtual
      # memory, whereas xp used for low kernel symbols reads physical memory.
      raw=hmp(f'x /1wx 0x{pointer+(old_cursor[1]*w+old_cursor[0])*4:x}')
      match=re.search(r':\s*0x([0-9a-fA-F]+)',raw)
      if not match:raise RuntimeError('Cannot read clean scene pixel')
      pixel=int(match[1],16)
      return im.getpixel(tuple(old_cursor))==((pixel>>16)&255,(pixel>>8)&255,pixel&255)
    wait(cursor_visible,'cursor visible and previous background restored',30)
    result['input_to_visible_capture_ms']=(time.monotonic()-submitted)*1000
    result['cursor_evidence']=dict(old=old_cursor,new=new_cursor,restored=True,visible=True)
    history=[]
    states=symbol('(anonymous namespace)::stan_okna')
    def click(x,y):move(x,y);press(1);press(0)
    win=window();click(win['x']+win['w']-64,win['y']+14)
    wait(lambda:struct.unpack('<I',read(states+win['pid']*4,4))[0]==2,'guest window minimizes')
    history.append(snapshot('minimized'));shot('minimized')
    click(120,h-20)
    wait(lambda:struct.unpack('<I',read(states+win['pid']*4,4))[0]!=2,'guest window restores')
    history.append(snapshot('restored'));shot('restored')
    # Opening Start exercises the actual BWS57 popup copy with SMAP enabled.
    click(32,h-20);time.sleep(1);shot('start-popup');key('esc');time.sleep(1)
    history.append(snapshot('popup-closed'))
    assert all(record['geometry']==before['geometry'] for record in history),'geometry changed during lifecycle'
    result['lifecycle']=history
   if a.host_resize:
    if a.display=='none':raise RuntimeError('--host-resize requires a window frontend')
    native_windows=subprocess.check_output(['wmctrl','-lp'],text=True).splitlines()
    wid=next(line.split()[0] for line in native_windows if line.split()[2]==str(proc.pid))
    host_history=[]
    for size in ['1000,700','1460,900','900,600']:
     subprocess.run(['wmctrl','-ir',wid,'-e','0,-1,-1,'+size],check=True)
     time.sleep(2);record=snapshot('host-'+size.replace(',','x'));host_history.append(record)
     assert record['geometry']==before['geometry'],'host viewport changed guest mode'
    for action in ['add','remove']:
     subprocess.run(['wmctrl','-ir',wid,'-b',action+',maximized_vert,maximized_horz'],check=True)
     time.sleep(2);record=snapshot('host-maximize-'+action);host_history.append(record)
     assert record['geometry']==before['geometry'],'host maximize/restore changed guest mode'
    result['host_resize']=host_history

   call('stop')
   try:registers=hmp('info registers')
   finally:call('cont')
   cr4=int(re.search(r'CR4=([0-9a-fA-F]+)',registers)[1],16)
   efer=int(re.search(r'EFER=([0-9a-fA-F]+)',registers)[1],16)
   assert cr4&(1<<20) and cr4&(1<<21) and efer&(1<<11),'SMEP/SMAP/NX missing'
   result['security_registers']=dict(cr4=hex(cr4),efer=hex(efer),smep=True,smap=True,nx=True)
   (out/'registers.txt').write_text(registers)
   assert idle['counters']['framebuffer_creates']==before['counters']['framebuffer_creates'],'framebuffer recreated during drag/idle'
   if 'resources' in idle:assert idle['resources'][0]==before['resources'][0],'graphics memory grew during drag/idle'
  except Exception as e:result['result']='FAIL';result['error']=str(e);raise
  finally:
   (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
   if q:
    try:q.call('quit');q.close()
    except Exception:pass
   try:proc.wait(timeout=5)
   except subprocess.TimeoutExpired:proc.kill();proc.wait()
   socket_dir.cleanup()
 print(json.dumps({k:result[k] for k in ['result','mode','delta','host_fps','frame_total_ns','input_to_submit_ns']},indent=2))
if __name__=='__main__':main()
