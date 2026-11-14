#!/usr/bin/env python3
"""Install the current ISO into a NEW private disk image (.bsf-test/installed-sec-*).

Reuses the installer GUI automation of testy/test_bsf_installed_qemu.py; never attaches
an existing disk. Prints the artifact directory; boot it with
narzedzia/guest_session.py --disk <dir>/target.img.
"""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
from instalator.qemu_install import Guest, install, installer_iso, sha, sparse, target_fingerprint  # noqa: E402

parent = ROOT / '.bsf-test'
out = Path(tempfile.mkdtemp(prefix='installed-sec-', dir=parent))
print('Artifacts:', out, flush=True)
disk, sentinel = out / 'target.img', out / 'sentinel.img'
sparse(disk, 4 * 1024**3)
sparse(sentinel, 64 * 1024**2)
sentinel_sha = sha(sentinel)
iso = installer_iso(out)
guest = Guest(out / 'installer', disk, sentinel, iso)
try:
    install(guest, disk, sentinel, target_fingerprint(disk), sentinel_sha)
finally:
    guest.close()
(out / 'installed.json').write_text(json.dumps(dict(sentinel_sha256=sentinel_sha)))
print('INSTALL-OK', out, flush=True)
