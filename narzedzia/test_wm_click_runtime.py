#!/usr/bin/env python3
"""One production SDL session; visual assertions plus read-only QMP guest state.

Never retries a failed action or starts another VM. No writes to guest memory.
"""
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import time
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.wm-test/runtime'
SOCKET = '/tmp/bursztyn-wm-click-once.sock'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    if Path(SOCKET).exists():
        raise RuntimeError('QMP socket already exists; refusing to replace a session')
    symbols = {}
    for line in subprocess.check_output(['nm', '-C', str(ROOT/'system_operacyjny.bin')], text=True).splitlines():
        fields = line.split(maxsplit=2)
        if len(fields)==3:
            symbols[fields[2]] = int(fields[0],16)
    result = {'session_count': 1, 'checks': {}, 'screenshots': []}
    logpath = OUT/'serial.log'
    log = logpath.open('w')
    proc = subprocess.Popen(['make','runuefi',
        'QEMUDISPLAYARGS=-display sdl -qmp unix:'+SOCKET+',server,nowait'],
        cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
        start_new_session=True)
    connection = None
    try:
        deadline = time.monotonic()+300
        while not Path(SOCKET).exists():
            if proc.poll() is not None or time.monotonic()>deadline:
                raise RuntimeError('Production SDL VM did not start')
            time.sleep(0.25)
        connection=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        connection.settimeout(8)
        connection.connect(SOCKET)
        stream=connection.makefile('rwb'); stream.readline()
        def command(name,args=None):
            request={'execute':name}
            if args is not None: request['arguments']=args
            stream.write((json.dumps(request)+'\n').encode());stream.flush()
            while True:
                line=stream.readline()
                if not line: raise RuntimeError('QMP disconnected')
                response=json.loads(line)
                if 'error' in response: raise RuntimeError(response['error'])
                if 'return' in response: return response['return']
        command('qmp_capabilities')
        def healthy():
            text=logpath.read_text(errors='replace')
            for marker in ('Timeout polecenia kontrolnego','[VIRTIO-GPU-DIAG]','KERNEL PANIC','[PANIC]','[EXCEPTION]'):
                if marker in text: raise RuntimeError('Unstable session: '+marker)
            if proc.poll() is not None: raise RuntimeError('VM exited')
        while '[WM] Initial Desktop ready' not in logpath.read_text(errors='replace'):
            healthy()
            if time.monotonic()>deadline: raise RuntimeError('Desktop readiness timed out')
            time.sleep(0.5)
        time.sleep(2); healthy()
        def memory(address,size):
            path=OUT/'memory.bin'
            command('pmemsave',{'val':address,'size':size,'filename':str(path)})
            return path.read_bytes()
        def layers():
            table=memory(symbols['tablica_warstw'],16*40)
            titles=memory(symbols['(anonymous namespace)::tytul_okna'],16*48)
            windows=[]
            for pid in range(1,16):
                owner,z,x,y,w,h,buffer,active=struct.unpack_from('<iiiiIIQB',table,pid*40)
                title=titles[pid*48:(pid+1)*48].split(b'\0',1)[0].decode('utf-8',errors='replace')
                if active:
                    windows.append(dict(pid=pid,z=z,x=x,y=y,w=w,h=h,title=title))
            return windows
        def focus(): return struct.unpack('<i',memory(symbols['aktywny_pid_gui'],4))[0]
        def theme(): return struct.unpack('<22I',memory(symbols['g_aktywny_motyw'],88))
        def shot(name):
            healthy();path=OUT/(name+'.png')
            command('screendump',{'filename':str(path),'format':'png'})
            im=Image.open(path).convert('RGB');im.load()
            result['screenshots'].append(str(path));return im
        desktop=shot('01-desktop');sw,sh=desktop.size
        if (sw,sh)!=(1280,720): raise RuntimeError('Expected production 1280x720 geometry, got '+str(desktop.size))
        def click(x,y):
            healthy()
            command('input-send-event',{'events':[
                {'type':'abs','data':{'axis':'x','value':round(x*32767/(sw-1))}},
                {'type':'abs','data':{'axis':'y','value':round(y*32767/(sh-1))}}]})
            time.sleep(0.15)
            command('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':True}}]})
            time.sleep(0.1)
            command('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':False}}]})
            time.sleep(1);healthy()
        def check(name,condition):
            result['checks'][name]=bool(condition)
            print(name+': '+('PASS' if condition else 'FAIL'),flush=True)
            if not condition: raise AssertionError(name)
        def window(prefix):
            return next((w for w in layers() if w['title'].startswith(prefix)),None)
        def visible(w,im):
            if not w: return False
            # Assert real compositor close-button pixels, not only process creation.
            color=theme()[2+11]&0xffffff
            rgb=((color>>16)&255,(color>>8)&255,color&255)
            box=(w['x']+w['w']-26,w['y']+4,w['x']+w['w']-6,w['y']+24)
            count=sum(pixel==rgb for pixel in im.crop(box).getdata())
            client=(w['x']+1,w['y']+28,w['x']+w['w']-1,w['y']+w['h']-1)
            changed=ImageChops.difference(im.crop(client),desktop.crop(client)).getbbox()
            return count>100 and changed is not None
        click(35,sh-20)
        menu=shot('02-start')
        overlay=memory(symbols['(anonymous namespace)::system_overlay'],24)
        check('Start menu visible',overlay[0]!=0 and ImageChops.difference(menu.crop((10,210,270,680)),desktop.crop((10,210,270,680))).getbbox() is not None)
        click(100,232)
        shell=window('Powłoka');shell_im=shot('03-shell')
        check('Shell visibly opened',visible(shell,shell_im))
        click(35,sh-20);shot('04-start-again');click(100,282)
        notebook=window('Notatnik');notebook_im=shot('05-notatnik')
        check('Notatnik visibly opened',visible(notebook,notebook_im))
        result['windows_after_launch']=layers()
        # Select an exposed Shell client pixel; no taskbar activation shortcut.
        exposed=None
        for x,y in [(shell['x']+5,shell['y']+60),(shell['x']+shell['w']-5,shell['y']+60),
                    (shell['x']+5,shell['y']+shell['h']-5),(shell['x']+shell['w']-5,shell['y']+shell['h']-5)]:
            if not (notebook['x']<=x<notebook['x']+notebook['w'] and notebook['y']<=y<notebook['y']+notebook['h']):
                exposed=(x,y);break
        check('Exposed client point available',exposed is not None)
        old_focus=focus();click(*exposed)
        check('Client click changes focus',old_focus!=shell['pid'] and focus()==shell['pid'])
        shot('06-shell-focused')
        # Themes are owned by native Settings; search avoids obsolete row indices.
        click(35,sh-20)
        for key in [*'ustawienia','ret']:
            command('human-monitor-command',{'command-line':'sendkey '+key+' 40'});time.sleep(0.2)
        time.sleep(1)
        themes=window('Ustawienia');check('Settings window opened',themes is not None)
        before_theme=theme()
        click(themes['x']+90,themes['y']+199) # Motywy
        click(themes['x']+210,themes['y']+190) # Bursztyn Jasny
        click(themes['x']+388,themes['y']+300) # Zastosuj
        time.sleep(1);healthy()
        check('Theme changed',theme()!=before_theme)
        shot('07-theme-changed')
        click(themes['x']+themes['w']-16,themes['y']+14)
        check('Settings close button works',window('Ustawienia') is None)
        click(*exposed);check('Client clicks work after theme',focus()==shell['pid'])
        changed=shot('08-shell-theme')
        check('Shell chrome visibly updated',visible(shell,changed) and
              shell_im.getpixel((shell['x']+shell['w']//2,shell['y']+26))!=changed.getpixel((shell['x']+shell['w']//2,shell['y']+26)))
        click(shell['x']+shell['w']-16,shell['y']+14)
        closed=shot('09-shell-closed')
        check('Shell close button removes window',window('Powłoka') is None and not visible(shell,closed))
        click(notebook['x']+notebook['w']//2,notebook['y']+60)
        check('Notatnik client focus after theme',focus()==notebook['pid'])
        shot('10-notatnik-theme')
        click(notebook['x']+notebook['w']-16,notebook['y']+14)
        closed_notebook=shot('11-notatnik-closed')
        check('Notatnik close button removes window',window('Notatnik') is None and not visible(notebook,closed_notebook))
        result['pass']=True
        command('quit')
    except Exception as exc:
        result['pass']=False;result['error']=str(exc)
        if connection:
            try:
                # These diagnostics also work before desktop helpers exist.
                path=OUT/'failure.png'
                command('screendump',{'filename':str(path),'format':'png'})
                result['screenshots'].append(str(path))
                result['registers_at_failure']=command('human-monitor-command',{'command-line':'info registers'})
            except Exception as diagnostic_error:
                result['diagnostic_error']=str(diagnostic_error)
            try:
                command('quit')
            except Exception:
                pass
        print('STOP (no retry): '+str(exc),flush=True)
    finally:
        if connection: connection.close()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid,signal.SIGTERM)
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid,signal.SIGKILL);proc.wait()
        log.close()
        if Path(SOCKET).exists(): Path(SOCKET).unlink()
        (OUT/'result.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
    return 0 if result.get('pass') else 1

if __name__=='__main__':
    raise SystemExit(main())
