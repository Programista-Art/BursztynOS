#!/usr/bin/env python3
"""Deterministic page fixtures for desktop responsiveness measurements.

Usage: generate.py OUTDIR. Produces ten categories (small, css, js,
wordpress-like, images, long, webfont-free text, rtl, table, many-nodes)
plus index.html. All content is synthetic and licence-free.
"""
import sys, zlib, struct, random
from pathlib import Path

def png(path, w, h, seed):
    rnd = random.Random(seed)
    raw = b''.join(b'\0' + bytes(rnd.randrange(256) for _ in range(w * 3)) for _ in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

LOREM = ("Bursztyn OS to niezalezny polski system operacyjny z wlasnym jadrem, "
         "systemem plikow PSF oraz przegladarka Husarz. ")

def page(title, body, head=''):
    return f'<!doctype html><html lang="pl"><head><meta charset="utf-8"><title>{title}</title>{head}</head><body>{body}</body></html>'

def main():
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    pages = {}
    pages['small.html'] = page('Mala', '<h1>Mala strona</h1><p>' + LOREM + '</p>')
    rules = ''.join(f'.c{i}{{color:#{(i*37)%256:02x}{(i*91)%256:02x}{(i*53)%256:02x};margin:{i%7}px;padding:{i%5}px}}' for i in range(600))
    items = ''.join(f'<div class="c{i%600}"><span class="c{(i*7)%600}">{LOREM}</span></div>' for i in range(400))
    pages['css-heavy.html'] = page('CSS', '<h1>CSS</h1>' + items, f'<style>{rules}</style>')
    js = ("var s=0;for(var i=0;i<20000;i++){s+=i%7;}"
          "var d=document.getElementById('o');for(var k=0;k<200;k++){var e=document.createElement('p');"
          "e.textContent='Linia '+k+' suma '+s;d.appendChild(e);}")
    pages['js-heavy.html'] = page('JS', '<h1>JS</h1><div id="o"></div><script>' + js + '</script>')
    wp = ''.join(f'<article class="post"><header class="entry-header"><h2 class="entry-title"><a href="#p{i}">Wpis {i}</a></h2>'
                 f'<div class="entry-meta">2026-11-{1+i%28} autor admin</div></header><div class="entry-content"><p>{LOREM*4}</p>'
                 f'<ul><li>punkt a</li><li>punkt b</li><li>punkt c</li></ul></div><footer class="entry-footer">kategorie: a, b</footer></article>'
                 for i in range(60))
    wcss = ('.post{border:1px solid #ccc;margin:12px;padding:10px;background:#fafafa}.entry-title a{color:#036;text-decoration:none}'
            '.entry-meta{color:#777;font-size:12px}.entry-content p{line-height:1.5}')
    pages['wordpress.html'] = page('WP', '<div id="page"><header id="masthead"><h1>Blog</h1></header><main>' + wp + '</main></div>', f'<style>{wcss}</style>')
    for i in range(8):
        png(out / f'img{i}.png', 160, 120, i)
    pages['images.html'] = page('Obrazy', '<h1>Obrazy</h1>' + ''.join(f'<img src="img{i%8}.png" width="160" height="120"> ' for i in range(24)))
    pages['long.html'] = page('Dluga', ''.join(f'<h3>Sekcja {i}</h3><p>{LOREM*6}</p>' for i in range(300)))
    pages['rtl.html'] = page('RTL', '<h1 dir="rtl">مرحبا بالعالم</h1>' + ''.join(
        f'<p dir="rtl">هذا نص عربي رقم {i} للاختبار مع ‏<span dir="ltr">Bursztyn OS</span> و שלום עולם {i}</p>' for i in range(120)))
    rows = ''.join('<tr>' + ''.join(f'<td>r{r}c{c} {LOREM[:20]}</td>' for c in range(8)) + '</tr>' for r in range(150))
    pages['table.html'] = page('Tabela', f'<table border="1" cellpadding="3">{rows}</table>')
    pages['many-nodes.html'] = page('Wezly', ''.join(f'<div><p><b>a{i}</b><i>b{i}</i><u>c{i}</u></p></div>' for i in range(2500)))
    pages['index.html'] = page('Indeks', ''.join(f'<p><a href="{n}">{n}</a></p>' for n in pages))
    for n, t in pages.items():
        (out / n).write_text(t, encoding='utf-8')
    print(f'{len(pages)} pages, {sum((out / n).stat().st_size for n in pages)} bytes')

main()
