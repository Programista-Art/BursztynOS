#!/usr/bin/env python3
"""Send one command to guest_session.py: guest_ctl.py [--port N] "click 10 20"."""
import argparse, socket, sys

ap = argparse.ArgumentParser()
ap.add_argument('--port', type=int, default=5555)
ap.add_argument('--timeout', type=float, default=900)
ap.add_argument('command', nargs='+')
a = ap.parse_args()
s = socket.create_connection(('127.0.0.1', a.port), timeout=a.timeout)
s.sendall((' '.join(a.command) + '\n').encode())
print(s.makefile().readline().strip())
