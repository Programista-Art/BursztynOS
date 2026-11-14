#!/usr/bin/env python3
"""DIAG: .tls-fixture/ca.pem -> jadro/siec/tls_test_ca.generated.h (patrz tls.cpp, BURSZTYN_DIAG_TEST_CA)."""
import sys
from pathlib import Path
pem = Path(sys.argv[1]).read_text().strip().split('\n')
body = '\n'.join('"%s\\n"' % l for l in pem)
Path(sys.argv[2]).write_text('static const unsigned char TEST_CA_PEM[] =\n' + body + ';\n')
