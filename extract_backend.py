import re

with open("shell.cpp", "r") as f:
    code = f.read()

idx = code.find("struct Argumenty {")
if idx == -1:
    print("Could not find struct Argumenty")
else:
    backend_code = code[idx:]
    with open("backend.cpp", "w") as f:
        f.write(backend_code)
    print("Backend saved.")
