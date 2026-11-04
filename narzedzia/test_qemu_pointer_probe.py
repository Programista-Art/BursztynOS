#!/usr/bin/env python3
"""Observer ABI and QMP protocol tests; no claim about Windows coordinates."""
import json
import socket
import struct
import threading
import unittest
from qemu_pointer_probe import Qmp, guest_snapshot, windows_snapshot, serial_decoder

class ProbeTests(unittest.TestCase):
    def test_powershell_log_encodings_and_partial_writes(self):
        import tempfile
        from pathlib import Path
        text='[ABS MAP] mode=ABS raw=0,0 mapped=0,0\r\n'
        for encoding in ('utf-8-sig','utf-16'):
            with tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/'serial.log';data=text.encode(encoding);path.write_bytes(data)
                decoder=serial_decoder(path)
                actual=''.join(decoder.decode(data[i:i+3]) for i in range(0,len(data),3))
                self.assertEqual(actual.lstrip('\ufeff'),text)
    def test_physical_geometry_abi(self):
        class Memory:
            def read(self, address, length):
                if address==1:return struct.pack('<i',0)
                if address==2:return struct.pack('<i',1079)
                return struct.pack('<IIiiI10iI',1,42,1920,1080,200,0,0,1920,1000,0,1000,1920,80,1919,1079,42)
        g=guest_snapshot(Memory(),dict(mysz_x=1,mysz_y=2,committed_geometry=3))
        self.assertEqual(g,dict(x=0,y=1079,version=1,generation=42,width=1920,height=1080,scale=200,cursor_max_x=1919,cursor_max_y=1079))

    def test_actual_tcp_protocol_async_events_and_physical_read(self):
        server=socket.socket();server.bind(('127.0.0.1',0));server.listen(1)
        requests=[]
        def serve():
            connection,_=server.accept()
            with connection,connection.makefile('rb') as stream:
                connection.sendall(b'{"QMP":{"version":{"qemu":{"major":8}}}}\n')
                for response in ({},'0000000000000000: 0x00 0xff 0x01 0x80\r\n'):
                    requests.append(json.loads(stream.readline()))
                    connection.sendall(b'{"event":"MOUSE_MODE_CHANGED"}\n')
                    connection.sendall((json.dumps({'return':response})+'\n').encode())
        thread=threading.Thread(target=serve);thread.start()
        q=Qmp(server.getsockname()[1])
        try:self.assertEqual(q.read(0,4),b'\x00\xff\x01\x80')
        finally:q.close();thread.join(5);server.close()
        self.assertFalse(thread.is_alive())
        self.assertEqual(requests,[{'execute':'qmp_capabilities','arguments':{}},{'execute':'human-monitor-command','arguments':{'command-line':'xp /4bx 0x0'}}])

    def test_does_not_fabricate_windows_measurements_on_linux(self):
        import platform
        if platform.system()!='Windows':self.assertFalse(windows_snapshot(None)['available'])

if __name__=='__main__':unittest.main()
