"""Small deterministic script staged with the future CPython guest image.

The script intentionally uses only the standard library pieces promised by
the runtime manifest.  The QEMU gate looks for its serial marker; this file
is not a host-side substitute for executing CPython in the guest.
"""

import encodings
import sys
import sysconfig


assert sys.version_info[:2] == (3, 12), sys.version
assert encodings.__name__ == "encodings"
assert sysconfig.get_path("stdlib"), "stdlib path is empty"

print("BURSZTYN-PYTHON-CONTRACT: PASS")
