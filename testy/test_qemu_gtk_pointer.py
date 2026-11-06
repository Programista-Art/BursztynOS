#!/usr/bin/env python3
"""Linux GTK frontend + XTest acceptance; explicitly NOT Windows/DPI evidence.
Requires Xephyr, metacity, python-xlib. QMP only reads guest state; pointer and
fullscreen inputs pass through the real GTK frontend. Disposable disk/desktop.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path
from Xlib import X, XK, display
from Xlib.ext import xtest
from qemu_pointer_probe import export_symbols, guest_snapshot
from test_hda_regression_qemu import Qmp

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', type=Path, required=True)
    p.add_argument('--kernel', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--transition-only',action='store_true',help='Focused large/fullscreen/returned capture comparison')
    p.add_argument('--regrab-after-return', action='store_true', help='Diagnostic intervention only; not a launcher/guest fix')
    a = p.parse_args()
    out = a.output.resolve();out.mkdir(parents=True, exist_ok=True)
    number = next(i for i in range(101, 120) if not Path(f'/tmp/.X11-unix/X{i}').exists())
    env = dict(os.environ, DISPLAY=f':{number}')
    processes = [];q = None;d = None
    result = dict(platform='Linux/X11 nested Xephyr, not Windows',frontend='gtk',input='virtio-mouse-pci relative',
                  qemu_version=subprocess.check_output(['qemu-system-x86_64','--version'],text=True),
                  regrab_intervention=a.regrab_after_return, center_tolerance_pixels=1,
                  kernel_sha256=hashlib.sha256(a.kernel.read_bytes()).hexdigest(), stages=[])
    try:
        with (out/'frontend.log').open('w') as log:
            processes.append(subprocess.Popen(['Xephyr',f':{number}','-ac','-noreset','-screen','1920x1200'],stdout=log,stderr=log))
            deadline=time.monotonic()+10
            while not Path(f'/tmp/.X11-unix/X{number}').exists():
                assert time.monotonic()<deadline,'Xephyr startup';time.sleep(.05)
            d=display.Display(f':{number}');root=d.screen().root
            processes.append(subprocess.Popen(['metacity','--sm-disable'],env=env,stdout=log,stderr=log))
            argv=json.loads(subprocess.check_output(['make','-s','runuefi-argv'],cwd=ROOT,text=True))['argv']
            sock=Path(f'/tmp/bursztyn-gtk-pointer-{os.getpid()}.sock')
            disk=out/'empty.img'
            with disk.open('wb') as f:f.truncate(256*1024*1024)
            for i,item in enumerate(argv):
                if i and argv[i-1]=='-display':argv[i]='gtk'
                if i and argv[i-1]=='-serial':argv[i]='file:'+str(out/'serial.log')
                if i and argv[i-1]=='-audiodev':argv[i]='none,id=snd'
                if i and argv[i-1]=='-cdrom':argv[i]=str(a.iso.resolve())
                if i and argv[i-1]=='-name':argv[i]='Bursztyn pointer frontend probe'
                if 'file=' in item and 'if=none' in item:argv[i]=re.sub(r'file=[^,]+',f'file={disk}',item)
            argv += ['-name','Bursztyn pointer frontend probe','-qmp',f'unix:{sock},server=on,wait=off','-no-reboot']
            result['argv']=argv;processes.append(subprocess.Popen(argv,cwd=ROOT,env=env,stdout=log,stderr=log))
            deadline=time.monotonic()+120
            while not sock.exists():
                assert time.monotonic()<deadline,'QMP startup';time.sleep(.05)
            q=Qmp(sock);symbols=export_symbols(a.kernel)['symbols']
            class Reader:
                def read(self,address,length):
                    text=q.call('human-monitor-command',{'command-line':f'xp /{length}bx 0x{address:x}'})['return']
                    data=[]
                    for line in text.splitlines():
                        if ':' in line:data.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b',line.split(':',1)[1]))
                    assert len(data)==length;return bytes(data)
            reader=Reader()
            def guest():return guest_snapshot(reader,symbols)
            def wait(condition,label,timeout=15):
                end=time.monotonic()+timeout
                while not condition():
                    assert time.monotonic()<end,label;time.sleep(.02)
            wait(lambda:guest()['generation']>0,'committed display',120)
            # Wait for the normal desktop to be entered, not a fixed boot delay.
            wait(lambda:'Wielozadaniowosc Round-Robin aktywna' in (out/'serial.log').read_text(errors='replace') or
                 'HUSARZ' in (out/'serial.log').read_text(errors='replace') or
                 'GUI: menedzer' in (out/'serial.log').read_text(errors='replace'), 'desktop serial marker',30)
            def find(window):
                try:
                    if 'Bursztyn pointer frontend probe' in (window.get_wm_name() or ''):return window
                    for child in window.query_tree().children:
                        found=find(child)
                        if found:return found
                except Exception:pass
            window=find(root);assert window,'GTK window'
            def wm(*args):subprocess.run(['wmctrl','-i','-r',hex(window.id),*args],env=env,check=True)
            def click_center():
                g=window.get_geometry();pos=root.translate_coords(window,0,0)
                xtest.fake_input(d,X.MotionNotify,x=pos.x+g.width//2,y=pos.y+g.height//2)
                xtest.fake_input(d,X.ButtonPress,1);xtest.fake_input(d,X.ButtonRelease,1);d.sync()
            def hotkey(letter='f'):
                codes=[d.keysym_to_keycode(XK.string_to_keysym(k)) for k in ('Control_L','Alt_L',letter)]
                for k in codes:xtest.fake_input(d,X.KeyPress,k)
                for k in reversed(codes):xtest.fake_input(d,X.KeyRelease,k)
                d.sync()
            baseline=guest();result['baseline']=baseline
            stages=('large','fullscreen','returned') if a.transition_only else ('small','large','maximized','fullscreen','returned','small-again')
            for stage in stages:
                if stage in ('small','small-again','large'):
                    wm('-b','remove,maximized_vert,maximized_horz');wm('-e','0,100,100,'+('800,600' if stage!='large' else '1500,950'))
                if stage=='maximized':wm('-b','add,maximized_vert,maximized_horz')
                if stage in ('fullscreen','returned'):
                    hotkey()
                    def is_full():
                        value=window.get_full_property(d.intern_atom('_NET_WM_STATE'),X.AnyPropertyType)
                        return value is not None and d.intern_atom('_NET_WM_STATE_FULLSCREEN') in value.value
                    wait(lambda:is_full()==(stage=='fullscreen'),'frontend fullscreen state')
                if stage=='returned' and a.regrab_after_return:
                    hotkey('g')
                    wait(lambda:'release grab' not in (window.get_wm_name() or ''),'release frontend grab')
                click_center()
                wait(lambda:'release grab' in (window.get_wm_name() or ''),'GTK pointer grab')
                samples=[]
                for target in ((0,0),(baseline['width']-1,0),(0,baseline['height']-1),(baseline['width']-1,baseline['height']-1),(baseline['width']//2,baseline['height']//2)):
                    trace=[]
                    tolerance=1 if target==(baseline['width']//2,baseline['height']//2) else 0
                    for attempt in range(160):
                        g=guest();diff=(target[0]-g['x'],target[1]-g['y'])
                        if max(abs(diff[0]),abs(diff[1]))<=tolerance:break
                        pointer=root.query_pointer()
                        trace.append(dict(host=[pointer.root_x,pointer.root_y],guest=[g['x'],g['y']],title=window.get_wm_name()))
                        step=lambda n:max(-60,min(60,n))
                        xtest.fake_input(d,X.MotionNotify,x=max(0,min(1919,pointer.root_x+step(diff[0]))),y=max(0,min(1199,pointer.root_y+step(diff[1]))));d.sync()
                        # Observe asynchronous delivery; this never changes a guest timeout.
                        end=time.monotonic()+.15
                        while time.monotonic()<end and (guest()['x'],guest()['y'])==(g['x'],g['y']):time.sleep(.005)
                    actual=guest();samples.append(dict(target=target,actual=actual))
                    if max(abs(actual['x']-target[0]),abs(actual['y']-target[1]))>tolerance:
                        result['failed_stage']=dict(stage=stage,target=target,actual=actual,trace=trace[-20:])
                        raise AssertionError((stage,target,actual))
                    assert (actual['width'],actual['height'],actual['generation'])==(baseline['width'],baseline['height'],baseline['generation'])
                geometry=window.get_geometry()
                result['stages'].append(dict(stage=stage,window_size=[geometry.width,geometry.height],samples=samples,mice=q.call('query-mice')))
                print('Frontend pointer PASS:',stage,flush=True)
            text=(out/'serial.log').read_text(errors='replace')
            assert not any(x in text for x in ('PANIC','Ring 3 fault','TRANSFER_TO_HOST_2D timeout'))
            result['status']='PASS'
    except Exception as error:
        result['status']='FAIL';result['error']=repr(error);raise
    finally:
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        if q:q.close()
        if d:d.close();d=None
        for proc in reversed(processes):
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=5)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()
        if d:d.close()

if __name__=='__main__':main()
