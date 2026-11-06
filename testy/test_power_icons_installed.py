#!/usr/bin/env python3
"""Install release files into a fresh private disk and compare exact PNG bytes."""
import json,tempfile
from pathlib import Path
from instalator.qemu_install import Guest,install,installer_iso,sha,sparse,target_fingerprint
from test_diagnostyka_qemu import read_psf_file
from package_power_icons import NAMES
ROOT=Path(__file__).resolve().parents[1]
def main():
    out=Path(tempfile.mkdtemp(prefix='power-installed-',dir=ROOT/'.bsf-test'))
    print('Artifacts:',out,flush=True)
    disk=out/'target.img';sentinel=out/'sentinel.img'
    sparse(disk,4*1024**3);sparse(sentinel,64*1024**2);unchanged=sha(sentinel)
    iso=installer_iso(out);g=Guest(out/'installer',disk,sentinel,iso)
    try:install(g,disk,sentinel,target_fingerprint(disk),unchanged)
    finally:g.close()
    assets={}
    for name in NAMES:
        data=read_psf_file(disk,'/ikonki/'+name)
        assert data==(ROOT/'ikonki'/name).read_bytes(),name
        import hashlib
        assets[name]=hashlib.sha256(data).hexdigest()
    assert sha(sentinel)==unchanged
    result=dict(result='PASS',assets=assets,disk=str(disk),kernel_sha256=sha(ROOT/'system_operacyjny.bin'),iso_sha256=sha(ROOT/'BursztynOS.iso'))
    (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
if __name__=='__main__':main()
