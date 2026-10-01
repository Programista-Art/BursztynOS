import re

with open("shell.cpp", "r") as f:
    code = f.read()

match = re.search(r"/\* =+ \*\n \* 8\. OBSLUGA POLECEN\n \* =+ \*/", code)
if not match:
    print("Could not find OBSLUGA POLECEN")
else:
    backend_code = code[match.start():]
    with open("backend.cpp", "w") as f:
        f.write(backend_code)
    print("Backend saved.")
