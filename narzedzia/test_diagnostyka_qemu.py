#!/usr/bin/env python3
"""Native diagnostics GUI regression. Private VM, read-only ISO, optional COPY of an
existing installed test image for preference persistence. Never attaches host disks.
QMP supplies real input; memory observations are read-only test evidence.
"""
import argparse,hashlib,json,re,shutil,struct,subprocess,tempfile,time
from pathlib import Path
from test_hda_regression_qemu import Qmp
ROOT=Path(__file__).resolve().parents[1]
def read_psf_file(image,path):
 """Read-only test observer for bounded direct-block diagnostic files."""
 with image.open('rb') as f:
  def read_at(offset,length):
   if offset<0 or length<0 or offset+length>image.stat().st_size:raise RuntimeError('Image bounds')
   f.seek(offset);data=f.read(length)
   if len(data)!=length:raise RuntimeError('Short image read')
   return data
  header=read_at(512,512)
  if header[:8]!=b'EFI PART':raise RuntimeError('Fixture has no GPT')
  entries,count,stride=struct.unpack_from('<QII',header,72)
  if count>128 or stride!=128:raise RuntimeError('Unsupported fixture GPT')
  base=None
  for i in range(count):
   entry=read_at(entries*512+i*stride,stride)
   if entry[:16]==bytes.fromhex('3ce71fb214048545bd078ab4980f9c88'):base=struct.unpack_from('<Q',entry,32)[0]*512;break
  if base is None:raise RuntimeError('Fixture has no BSP2 partition')
  magic,total,blocks,root,nodes,data_start=struct.unpack('<4sQQQQQ',read_at(base,44))
  if magic!=b'BSP2':raise RuntimeError('Fixture has no BSP2 superblock')
  def inode(ident):
   if ident<1 or ident>data_start-nodes:raise RuntimeError('Inode bounds')
   b=read_at(base+(nodes+ident-1)*4096,4096)
   kind=b[0];size=struct.unpack_from('<Q',b,1)[0];pointers=struct.unpack_from('<490Q',b,25)
   length=size*64 if kind==2 else size
   if length>1024*1024:raise RuntimeError('Diagnostic observer file bound')
   out=bytearray()
   for ptr in pointers[:(length+4095)//4096]:
    if ptr>=blocks-data_start:raise RuntimeError('Data block bounds')
    out.extend(read_at(base+(data_start+ptr)*4096,4096))
   return kind,bytes(out[:length])
  ident=root
  for part in path.strip('/').split('/'):
   kind,data=inode(ident)
   if kind!=2:raise RuntimeError('Expected directory')
   found=None
   for offset in range(0,len(data),64):
    child=struct.unpack_from('<Q',data,offset)[0];name=data[offset+8:offset+64].split(b'\0',1)[0].decode('utf-8')
    if child and name==part:found=child;break
   if found is None:raise RuntimeError('Missing persisted file '+path)
   ident=found
  kind,data=inode(ident)
  if kind!=1:raise RuntimeError('Expected diagnostic file')
  return data

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--timeout',type=float,default=240)
 p.add_argument('--machine',choices=['q35','pc'],default='q35');p.add_argument('--no-wallpapers',action='store_true');p.add_argument('--kernel',type=Path,default=ROOT/'system_operacyjny.bin');p.add_argument('--boot-only',action='store_true');p.add_argument('--output',type=Path,default=ROOT/'.input-test/diagnostyka/qemu');p.add_argument('--installed-fixture',type=Path);p.add_argument('--ps2',action='store_true');p.add_argument('--hold',type=float,default=0)
 args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
 (out/'result.json').unlink(missing_ok=True)
 kernel=args.kernel.resolve();kernel_sha=hashlib.sha256(kernel.read_bytes()).hexdigest();symbols=subprocess.check_output(['nm','-C',str(kernel)],text=True)
 def symbol(name):
  match=re.search(r'^([0-9a-f]+) [bBdD] '+re.escape(name)+r'$',symbols,re.M)
  if not match:raise RuntimeError('Missing observer symbol '+name)
  return int(match[1],16)
 xaddr,yaddr=symbol('mysz_x'),symbol('mysz_y')
 with tempfile.TemporaryDirectory(prefix='bursztyn-diag-') as temp:
  temp=Path(temp);boot=temp/'staging/boot';(boot/'grub').mkdir(parents=True)
  shutil.copy2(kernel,boot/'system_operacyjny.bin')
  if not args.no_wallpapers:shutil.copy2(ROOT/'tapety.pak',boot/'tapety.pak')
  extra=' bursztyn.installed=1 bursztyn.slot=A' if args.installed_fixture else ''
  (boot/'grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Diagnostics regression" {\ninsmod all_video\nset gfxmode=1280x720x32,auto\nset gfxpayload=keep\nmultiboot2 /boot/system_operacyjny.bin'+extra+'\n'+('' if args.no_wallpapers else 'module2 /boot/tapety.pak tapety.pak\n')+'boot\n}\n')
  iso=temp/'diag.iso'
  with (out/'iso-build.log').open('w') as log:subprocess.run(['grub-mkrescue','-o',str(iso),str(boot.parent)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
  disk=None
  if args.installed_fixture:
   source=args.installed_fixture.resolve()
   if not source.is_file() or source.suffix!='.img':raise RuntimeError('Only an existing regular test .img is accepted')
   disk=out/'private-persistent.img'
   subprocess.run(['cp','--reflink=auto','--sparse=always',str(source),str(disk)],check=True)
  sock=Path('/tmp')/f'bursztyn-diag-{time.time_ns()}.sock';serial=out/'serial.log';serial.unlink(missing_ok=True)
  (out/'qmp-path.txt').write_text(str(sock))
  machine=args.machine+(',hpet=on' if args.machine=='pc' else '')+('' if args.ps2 else ',i8042=off')
  cmd=['qemu-system-x86_64','-machine',machine,'-accel','tcg','-cpu','max','-smp','1','-m','2048',
   '-bios','/usr/share/ovmf/OVMF.fd','-display','none','-serial','file:'+str(serial),'-no-reboot',
   '-qmp',f'unix:{sock},server=on,wait=off','-vga','none','-device','virtio-vga,disable-legacy=on,xres=1280,yres=720',
   '-drive',f'file={iso},id=boot,if=none,format=raw,readonly=on','-device','virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1',
   '-device','qemu-xhci,id=usb0','-device','qemu-xhci,id=usb1','-device','usb-kbd,bus=usb0.0','-device','usb-mouse,bus=usb1.0',
   '-audiodev','none,id=snd','-device','intel-hda','-device','hda-output,audiodev=snd',
   '-netdev','user,id=n1','-device','virtio-net-pci,netdev=n1,disable-legacy=on']
  if disk:cmd+=['-drive',f'file={disk},id=persist,if=none,format=raw','-device','virtio-blk-pci,drive=persist,disable-legacy=on']
  q=None
  with (out/'qemu.log').open('w') as log:
   vm=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT);deadline=time.monotonic()+args.timeout
   def text():return serial.read_text(errors='replace') if serial.exists() else ''
   def wait(predicate,label):
    while time.monotonic()<deadline:
     value=text()
     if predicate(value):return value
     if vm.poll() is not None:raise RuntimeError('QEMU exited: '+(out/'qemu.log').read_text())
     if '[PF]' in value or '[KERNEL-PANIC]' in value:raise RuntimeError('Guest fault: '+value[-2000:])
     time.sleep(.2)
    raise TimeoutError(label+'\n'+text()[-3000:])
   try:
    if args.boot_only:
     wait(lambda s:'[SYSTEM] Pulpit gotowy.' in s,'baseline desktop')
     q=Qmp(sock);q.call('screendump',{'filename':str(out/'baseline.png'),'format':'png'})
     (out/'result.json').write_text(json.dumps({'result':'PASS','kernel_sha256':kernel_sha,'baseline_desktop':True},indent=2)+'\n')
     print('Baseline installed desktop boot: PASS',flush=True);return
    wait(lambda s:'[DIAG APP] Ring 3 diagnostic window initialized' in s,'Ring 3 window')
    wait(lambda s:s.count('result=interrupt IN armed')>=2,'both USB controllers')
    wait(lambda s:'[DIAG APP] First frame submitted' in s,'first visible GUI frame')
    q=Qmp(sock)
    mice=q.call('query-mice')['return'];mouse=next(m for m in mice if 'USB' in m['name'] or 'HID' in m['name'])
    q.call('human-monitor-command',{'command-line':f'mouse_set {mouse["index"]}'})
    def hmp(cmd):return q.call('human-monitor-command',{'command-line':cmd}).get('return','')
    def position():
     values=[]
     for a in (xaddr,yaddr):
      m=re.search(r':\s*0x([0-9a-fA-F]+)',hmp(f'xp /1wx 0x{a:x}'))
      if not m:raise RuntimeError('Cannot observe cursor')
      values.append(int(m[1],16))
     return values
    def click(x,y):
     for _ in range(40):
      cx,cy=position();dx=max(-80,min(80,x-cx));dy=max(-80,min(80,y-cy))
      if not dx and not dy:break
      hmp(f'mouse_move {dx} {dy}');time.sleep(.12)
     else:raise RuntimeError('Cursor did not reach '+str((x,y,position())))
     hmp('mouse_button 1');time.sleep(.3);hmp('mouse_button 0');time.sleep(.7)
    time.sleep(2);q.call('screendump',{'filename':str(out/'system.png'),'format':'png'})
    print('Diagnostics visible; '+str(sock),flush=True)
    if args.hold:time.sleep(min(args.hold,50))
    # Four native tab pages, standard default window at (100,30), 1040x650.
    for name,x,view in [('devices',260,'devices'),('drivers',350,'drivers'),('boot-log',450,'log')]:
     click(x,132);wait(lambda s:'[DIAG APP] View='+view in s,'tab '+view);time.sleep(1);q.call('screendump',{'filename':str(out/(name+'.png')),'format':'png'})
    click(630,644)
    wait(lambda s:'[DIAG APP] Report saved and size verified' in s,'report save via GUI')
    q.call('screendump',{'filename':str(out/'saved.png'),'format':'png'})
    result={'result':'PASS','kernel_sha256':kernel_sha,
     'ring3_launched':True,'usb_controllers':2,'report_saved':True,'boot_log_saved':'[DIAG] Boot report saved:' in text(),
     'persistent_fixture':bool(disk)}
    result['tabs_verified']=True
    if not args.ps2:
     if '[PS/2] controller absent' not in text():raise RuntimeError('PS/2 was not disabled')
     result['usb_with_missing_ps2']=True
    # Clipboard export and ordinary WM minimize/restore/maximize actions.
    click(950,654);time.sleep(1)
    click(1075,44);time.sleep(1);q.call('screendump',{'filename':str(out/'minimized.png'),'format':'png'})
    click(120,700);time.sleep(1)
    click(1099,44);time.sleep(1);q.call('screendump',{'filename':str(out/'maximized.png'),'format':'png'})
    # Maximized title buttons move to the screen edge. Restore previous geometry.
    click(1237,14);time.sleep(1)
    result['wm_actions_exercised']=True
    if disk:
     report_data=read_psf_file(disk,'/logi/DIAG.TXT')
     if b'INPUT-' not in report_data and b'HID' not in report_data:raise RuntimeError('Missing report hardware details')
     (out/'DIAG.TXT').write_bytes(report_data)
     (out/'BOOTLOG.TXT').write_bytes(read_psf_file(disk,'/logi/BOOTLOG.TXT'))
     click(120,618);wait(lambda s:'[DIAG APP] Saved autostart=0' in s,'disable normal autostart')
     click(120,588);wait(lambda s:'[DIAG APP] Saved problems=0' in s,'disable problem autostart')
     if read_psf_file(disk,'/uzytkownicy/DIAG.CFG')!=b'autostart=0\nproblems=0\n':raise RuntimeError('Disabled preferences not persisted')
     def reboot(label):
      nonlocal q,vm,deadline
      shutil.copyfile(serial,out/(label+'-previous-serial.log'));q.close();q=None
      vm.terminate();vm.wait(timeout=10);sock.unlink(missing_ok=True);serial.unlink(missing_ok=True)
      vm=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT);deadline=time.monotonic()+args.timeout
      wait(lambda s:'[SYSTEM] Pulpit gotowy.' in s,'reboot desktop')
      q=Qmp(sock);hmp(f'mouse_set {mouse["index"]}')
     reboot('disabled')
     wait(lambda s:'[DIAG] Startup preference: autostart=0' in s and '[DIAG] Startup preference: problems=0' in s,'disabled persisted preferences')
     time.sleep(17)
     if '[DIAG] Diagnostyka systemu launched' in text() or '[DIAG APP]' in text():raise RuntimeError('Disabled autostart launched')
     q.call('screendump',{'filename':str(out/'autostart-disabled.png'),'format':'png'})
     # Manual Start menu entry remains usable when both startup modes are off.
     click(32,700);q.call('screendump',{'filename':str(out/'start-menu.png'),'format':'png'})
     click(130,580);wait(lambda s:'[DIAG APP] First frame submitted' in s,'manual Start menu launch')
     click(120,618);wait(lambda s:'[DIAG APP] Saved autostart=1' in s,'enable normal autostart')
     click(120,588);wait(lambda s:'[DIAG APP] Saved problems=1' in s,'enable problem autostart')
     if read_psf_file(disk,'/uzytkownicy/DIAG.CFG')!=b'autostart=1\nproblems=1\n':raise RuntimeError('Enabled preferences not persisted')
     reboot('enabled');wait(lambda s:'[DIAG APP] First frame submitted' in s,'restored autostart')
     q.call('screendump',{'filename':str(out/'autostart-enabled.png'),'format':'png'})
     # Close and manually launch the protected desktop shortcut with Enter.
     click(1124,44);time.sleep(1);click(560,128)
     hmp('sendkey ret');wait(lambda s:s.count('[DIAG APP] First frame submitted')>=2,'desktop shortcut launch')
     result['preferences_survived_reboots']=True;result['start_menu_launch']=True;result['desktop_launch']=True
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
   except Exception:
    if q:q.call('screendump',{'filename':str(out/'failure.png'),'format':'png'})
    raise
   finally:
    if q:q.close()
    vm.terminate()
    try:vm.wait(timeout=5)
    except subprocess.TimeoutExpired:vm.kill();vm.wait(timeout=5)
    sock.unlink(missing_ok=True)
if __name__=='__main__':main()
