#!/usr/bin/env python3
"""Generate the terminal's W/F ranges from Unicode 15.1.0 EAW (offline)."""
from pathlib import Path
import hashlib, json
root=Path(__file__).resolve().parents[1]
src=root/'narzedzia/unicode/15.1.0/EastAsianWidth.txt'
rows=[]
for line in src.read_text().splitlines():
    f=line.split('#')[0].split(';')
    if len(f)<2 or f[1].strip() not in ('W','F'):continue
    r=f[0].strip().split('..');a,b=int(r[0],16),int(r[-1],16)
    if rows and rows[-1][1]+1==a:rows[-1]=(rows[-1][0],b)
    else:rows.append((a,b))
out='// Generated Unicode 15.1.0 East_Asian_Width W/F; Unicode data license.\n'
out+='static constexpr uint32_t terminal_wide[][2] = {\n'
out+=''.join(f'{{0x{a:x},0x{b:x}}},\n' for a,b in rows)+'};\n'
(root/'bsf/terminal_width.inc').write_text(out)
p=src.parent/'SHA256.json';m=json.loads(p.read_text());m[src.name]=hashlib.sha256(src.read_bytes()).hexdigest();p.write_text(json.dumps(m,indent=2,sort_keys=True)+'\n')
print(len(rows),'ranges')
