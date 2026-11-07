"""Audited release asset selection, independent of native runtime discovery."""
import pathlib,re

def assets(root):
    source=root/'czcionki/ttf'
    result=[]
    for line in (root/'czcionki/release-fonts.txt').read_text().splitlines():
        name=line.split('#')[0].strip()
        if not name:continue
        if not re.fullmatch(r'[A-Za-z0-9_.-]+',name):raise ValueError('Invalid release asset name')
        path=source/name
        if path.is_symlink() or not path.is_file() or path.suffix.lower() not in ('.ttf','.txt'):raise ValueError('Invalid release asset')
        if path in result:raise ValueError('Duplicate release asset')
        result.append(path)
    return sorted(result)
