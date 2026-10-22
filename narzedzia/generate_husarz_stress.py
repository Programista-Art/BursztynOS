#!/usr/bin/env python3
"""Deterministic, bounded fixtures for the native Husarz performance suite."""
from pathlib import Path
root=Path(__file__).resolve().parent/'fixtures/husarz'
base='<style>body{margin:8px;color:#17212b;background:white} .card{padding:4px;border:1px solid #456;margin:3px} table{width:100%;border-spacing:2px} td{padding:2px} img{width:24px;height:24px}</style>'
def save(name,body,style=''):
    (root/name).write_text('<!doctype html><html><head><meta charset="utf-8">'+base+style+'</head><body>'+body+'</body></html>\n')
def table(k):
    return f'<table id="table{k}"><tbody><tr><td rowspan="2">span {k}</td><td colspan="2">wide</td></tr><tr><td>A</td><td>B</td></tr>'+''.join('<tr><td>A</td><td>B</td><td>C</td></tr>' for _ in range(8))+'</tbody></table>'
save('heavy-dom.html',''.join(f'<div class="card">Node {n}</div>' for n in range(1000)))
save('heavy-css.html',''.join(f'<div id="n{n}" class="card c{n%100} group{n%7}"><span>CSS {n}</span></div>' for n in range(350)), '<style>'+''.join(f'.card.c{n%100}.group{n%7}{{color:#{(n*731)%0xffffff:06x};padding-left:{n%4}px}}' for n in range(280))+'</style>')
save('heavy-table.html',''.join(table(n) for n in range(12)))
save('heavy-long.html',''.join(f'<div style="height:72px;border-bottom:1px solid #abc">Line {n}</div>' for n in range(700)))
save('heavy-images.html',''.join(f'<div class="card">Image {n}<img src="sample.png?slot={n%12}"><img src="sample.jpg?slot={n%12}"></div>' for n in range(16)))
save('heavy-mixed.html','<div style="height:12px;width:100%;background:#26a269"></div><h1>Heavy supported page</h1><form action="forms-native.html"><input name="q" value="edit"><input type="checkbox" checked><select name="s"><option>A</option><option>B</option></select></form>'+''.join(f'<section class="card group{n%7}"><div class="c{n%100}"><span>Nested content {n} ą ł</span><p>Text with margins and borders, repeated for bounded stress.</p></div></section>' for n in range(160))+''.join(table(n) for n in range(4))+''.join(f'<img src="sample.png?slot={n%8}">' for n in range(16)), '<style>'+''.join(f'.c{n} span{{color:#{(n*9331)%0xffffff:06x}}}' for n in range(100))+'</style>')
