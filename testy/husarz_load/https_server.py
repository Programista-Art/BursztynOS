#!/usr/bin/env python3
"""Threaded HTTPS static server for the local TLS fixture (TLS 1.2 max, like the guest client).

usage: https_server.py PAGES_DIR  -> prints the chosen port on stdout, serves forever.
Requests are recorded in <PAGES_DIR>/../https-requests.log is NOT written; callers import `serve()`.
"""
import http.server, ssl, sys, threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def serve(pages, requests=None):
    class H(http.server.SimpleHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, '.png': 'image/png'}

        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(pages), **kw)

        def do_GET(self):
            if requests is not None:
                requests.append(self.path)
            super().do_GET()

        def end_headers(self):
            self.send_header('Connection', 'close')
            super().end_headers()

        def log_message(self, *_):
            pass

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(ROOT / '.tls-fixture/server.pem', ROOT / '.tls-fixture/server.key')
    ctx.set_ciphers('ECDHE-ECDSA-AES128-GCM-SHA256')
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


if __name__ == '__main__':
    s = serve(Path(sys.argv[1]))
    print(s.server_port, flush=True)
    threading.Event().wait()
