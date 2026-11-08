"""Read-only observation of production Terminal, driven by ordinary GUI events."""
import json, re, struct, subprocess, time

def run_terminal(c):
    out, root=c['out'],c['ROOT'] if 'ROOT' in c else __import__('pathlib').Path(__file__).resolve().parents[1]
    key,click,wait,screenshot=c['key'],c['click'],c['wait'],c['screenshot']
    hmp,symbol=c['hmp'],c['symbol']
    symbols=subprocess.check_output(['nm','-C',str(root/'shell.elf')],text=True)
    def addr(name):
        m=re.search(r'^([0-9a-f]+) [bBdD] '+re.escape('(anonymous namespace)::'+name)+r'$',symbols,re.M)
        if not m:raise AssertionError('Missing Shell observer: '+name)
        return int(m[1],16)
    def physical(a,n):
        vals=[]
        for line in hmp(f'xp /{n}bx 0x{a:x}').splitlines():
            if ':' in line:vals.extend(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',line.split(':',1)[1]))
        assert len(vals)==n
        return bytes(vals)
    def pid():
        names=physical(symbol('(anonymous namespace)::tytul_okna'),16*48)
        return next((i for i in range(1,16) if names[i*48:(i+1)*48].startswith('Powłoka'.encode())),None)
    def user(a,n):
        p=pid();assert p
        table0=struct.unpack('<Q',physical(symbol('tablica_procesow')+p*144+0x38,8))[0]
        result=b''
        while n:
            table=table0&0x000ffffffffff000
            for shift in (39,30,21,12):
                entry=struct.unpack('<Q',physical(table+((a>>shift)&511)*8,8))[0];assert entry&1
                if shift==12 or shift in (30,21) and entry&128:
                    phys=(entry&0x000ffffffffff000&~((1<<shift)-1))+(a&((1<<shift)-1));break
                table=entry&0x000ffffffffff000
            size=min(n,4096-(a&4095));result+=physical(phys,size);a+=size;n-=size
        return result
    def value(name,fmt='<i'):return struct.unpack(fmt,user(addr(name),struct.calcsize(fmt)))[0]
    def text():
        p=value('g_obecny_bufor_wejscia','<Q');return user(p,2048).split(b'\0',1)[0] if p else None
    def command(command):
        assert text()==b''
        for ch in command:key({' ':'spc','"':'quotedbl'}.get(ch,ch))
        key('ret');wait(lambda:text()==b'','Shell command completion',120)
    c['navigate']('terminal-unicode.html')
    click(c['frame']['x']+45,c['frame']['y']+140);key('ctrl','a');key('ctrl','c')
    c['launch_start']('shell');wait(pid,'Terminal window',180)
    wait(lambda:value('g_obecny_bufor_wejscia','<Q')!=0,'Terminal input loop',180)
    assert value('g_terminal_face','<I')!=0
    w,h=value('g_terminal_cell'),value('g_terminal_row');assert w>0 and h>0
    x,y=value('shell_x'),value('shell_y');height=value('shell_h')
    click(x+300,y+height-20);key('ctrl','v')
    original='pisz "Polski Zażółć gęślą jaźń. Русский Привет мир. Українська Привіт світ. Ελληνικά Καλημέρα κόσμε. ἀᾀῷ 😀 | ä́"'.encode()
    wait(lambda:text()==original,'Terminal clipboard exact UTF-8',120)
    key('left');assert value('g_caret','<Q')==len(original)-1
    key('left');assert value('g_caret','<Q')==len(original)-6
    key('delete');key('z')
    expected=original[:-6]+b'z"';assert text()==expected
    screenshot('terminal-ttf-edited');key('ret');wait(lambda:text()==b'','Terminal output submitted',120)
    lines=value('g_liczba_linii');first=value('g_pierwsza_linia');base=value('g_linie','<Q')
    # sizeof ShellLine: 4096 + 160 + three int32 fields; checked by host harness.
    observed=[user(base+((first+i)%512)*4268,4096).split(b'\0',1)[0] for i in range(lines)]
    assert expected[6:-1] in observed,observed
    screenshot('terminal-ttf-output')
    command('czcionka bitmap');assert value('g_terminal_face','<I')==0;screenshot('terminal-bitmap-recovery')
    command('czcionka ttf');assert value('g_terminal_face','<I')!=0
    key('up');assert text()==b'czcionka ttf';key('ret')
    c['health']()
    (out/'terminal-observed.json').write_text(json.dumps(dict(cell_px=w,row_px=h,input_hex=expected.hex(),output=[x.decode('utf-8') for x in observed]),ensure_ascii=False,indent=2)+'\n')
