"""Audited release asset selection, independent of native runtime discovery.

Entries in czcionki/release-fonts.txt are file names in czcionki/ttf, or
'<dir>/<name>' for the BSF 2.2 font directories (otf, ttc, otc). The font
format is validated at runtime from the data, never from the name.
"""
import pathlib,re

DIRS=('ttf','otf','ttc','otc')
FONT_SUFFIX={'ttf':'.ttf','otf':'.otf','ttc':'.ttc','otc':'.otc'}

def assets(root):
    result=[]
    for line in (root/'czcionki/release-fonts.txt').read_text().splitlines():
        name=line.split('#')[0].strip()
        if not name:continue
        directory,_,base=name.rpartition('/')
        directory=directory or 'ttf'
        if directory not in DIRS or not re.fullmatch(r'[A-Za-z0-9_.-]+',base):raise ValueError('Invalid release asset name')
        path=root/'czcionki'/directory/base
        allowed=(FONT_SUFFIX[directory],'.txt')
        if path.is_symlink() or not path.is_file() or path.suffix.lower() not in allowed:raise ValueError('Invalid release asset')
        if path in result:raise ValueError('Duplicate release asset')
        result.append(path)
    return sorted(result)
