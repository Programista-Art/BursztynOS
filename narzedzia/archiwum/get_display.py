import socket, json
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("/tmp/my_qmp.sock")
s.recv(1024)
s.sendall(b'{"execute": "qmp_capabilities"}\n')
s.recv(1024)
s.sendall(b'{"execute": "query-display-options"}\n')
print(s.recv(1024).decode())
