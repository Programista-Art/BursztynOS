#!/usr/bin/env python3
"""Compare every actual 16 px / 100% desktop icon anchor across native frames.

Images are ordinary release QEMU screenshots. Top image strips omit embedded
font glyphs; template matching searches the entire desktop for displacement.
"""
import argparse
import json
from pathlib import Path
from PIL import Image


def locate(im, template):
    w, h = template.size
    row = max(range(h), key=lambda y: len(set(template.crop((0,y,w,y+1)).getdata())))
    needle = template.crop((0,row,w,row+1)).tobytes()
    data, start, matches = im.tobytes(), 0, []
    while True:
        offset = data.find(needle, start)
        if offset < 0:
            return matches
        start = offset + 3
        if offset % 3:
            continue
        y, x = divmod(offset // 3, im.width)
        y -= row
        if y >= 0 and x+w <= im.width and y+h < 680:
            if im.crop((x,y,x+w,y+h)).tobytes() == template.tobytes():
                matches.append([x,y])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--restart',type=Path,help='Disk-only reboot screenshots to compare as well')
    args = parser.parse_args()
    directory = args.directory
    baseline = Image.open(directory/'grid-sans-before.png').convert('RGB')
    icons = []
    for row in range(4):
        for col in range(10):
            x, y = 56+120*col, 3+124*row
            # Catalog launchers occupy their known initial cells. Discover
            # native user files/folders by their existing solid silhouettes.
            body = baseline.getpixel((x+5,y+5))
            user = body in ((255,191,0),(112,128,144)) and baseline.getpixel((x+40,y+40)) == body
            if row*10+col < 16 or user:
                template = baseline.crop((x,y,x+48,y+10))
                matches = locate(baseline, template)
                if [x,y] not in matches:
                    raise AssertionError('Missing baseline image anchor')
                icons.append((x,y,user,template,matches))
    if len(icons) <= 16 or not any(user and y >= 251 for x,y,user,t,m in icons):
        raise AssertionError('Missing real user icons in a third row')
    frames = sorted(directory.glob('grid-[0-4]-*.png'))
    if len(frames) != 5:
        raise AssertionError('Expected five live family transitions')
    if args.restart:frames.append(args.restart/'grid-mono-after-reboot.png')
    results = []
    for frame in frames:
        im = Image.open(frame).convert('RGB')
        for x,y,user,template,matches in icons:
            if locate(im,template) != matches:
                raise AssertionError(f'{frame.name}: icon at {(x,y)} moved or changed image outline')
        results.append(frame.name)
    result = dict(result='PASS',font_size=16,ui_scale=100,icon_count=len(icons),
                  anchors=[dict(x=x,y=y,user_file=user) for x,y,user,t,m in icons],frames=results,reboot_verified=bool(args.restart))
    (directory/'all-icon-anchors.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'All {len(icons)} native icon anchors identical across {len(frames)} compared frames: PASS')


if __name__ == '__main__':
    main()
