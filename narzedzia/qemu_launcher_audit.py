#!/usr/bin/env python3
"""Print the final launcher argv/topology; help/version queries never start a VM."""
import argparse,json,os,platform,shutil,subprocess
from pathlib import Path

def topology(argv):
    devices=[argv[i+1] for i,a in enumerate(argv[:-1]) if a=='-device']
    pci=[d for d in devices if d.split(',')[0] in ('intel-hda','virtio-blk-pci','virtio-net-pci','virtio-rng-pci','virtio-vga','virtio-keyboard-pci','virtio-mouse-pci','virtio-tablet-pci')]
    classes={'intel-hda':'audio','virtio-blk-pci':'storage','virtio-net-pci':'network',
             'virtio-rng-pci':'rng','virtio-vga':'gpu','virtio-keyboard-pci':'keyboard',
             'virtio-mouse-pci':'relative mouse','virtio-tablet-pci':'absolute tablet'}
    return [{'order':i,'device':d,'expected_class':classes[d.split(',')[0]],
             'expected_bdf':f'00:{i+2:02x}.0'} for i,d in enumerate(pci)]

def report(argv,probe=False):
    result={'argv':argv,'host_os':platform.platform(),'qemu_binary':shutil.which(argv[0]) or argv[0],
            'topology':topology(argv),'bdf_assumption':'pc/i440fx default host bridge/PIIX at 00:00.0/00:01.*, -vga none, no extra PCI devices'}
    if probe:
        for key,args in [('version',['--version']),('machines',['-machine','help']),('virtio_mouse_properties',['-device','virtio-mouse-pci,help'])]:
            p=subprocess.run([result['qemu_binary'],*args],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=False,timeout=10)
            result[key]={'returncode':p.returncode,'output':p.stdout}
    return result

def normalize(argv):
    # Firmware mode is retained, including distinct pflash vs -bios. Paths alone
    # are host specific; no device/property/order normalization is permitted.
    out=[]
    for i,a in enumerate(argv):
        a=a.replace('\\','/')
        if i==0:a='<QEMU>'
        elif argv[i-1] in ('-bios','-cdrom'):a='<FIRMWARE>' if argv[i-1]=='-bios' else '<ISO>'
        elif 'file=' in a:
            import re
            a=re.sub(r'file=[^,]+','file=<FIRMWARE>' if 'if=pflash' in a else 'file=<DISK>',a)
        out.append(a)
    return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--probe',action='store_true');p.add_argument('argv',nargs=argparse.REMAINDER)
    a=p.parse_args();argv=a.argv[1:] if a.argv[:1]==['--'] else a.argv
    print(json.dumps(report(argv,a.probe),indent=2))
if __name__=='__main__':main()
