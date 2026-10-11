#!/usr/bin/env python3
"""Save complete compiler-command and preprocessed diagnostic diffs. No build/VM."""
import argparse
import difflib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ('BURSZTYNDIAGXHCIPHYSICALMOUSE', 'BURSZTYNDIAGXHCIPOINTING',
         'BURSZTYNDIAGPS2', 'BURSZTYNDIAGUSBHID', 'BURSZTYNDIAGINPUTCOEXISTENCE',
         'BURSZTYN_DEBUG_XHCI', 'BURSZTYN_DEBUG_GUI_PERF')


def commands(flag=None):
    args = ['make', '-n', '-B', 'all'] + [f'{f}={int(f == flag)}' for f in FLAGS]
    output = subprocess.check_output(args, cwd=ROOT, text=True)
    result = {}
    for line in output.splitlines():
        try:
            words = shlex.split(line)
        except ValueError:
            continue
        if words and '-c' in words and '-o' in words:
            result[words[words.index('-o') + 1]] = words
    if flag and flag.startswith('BURSZTYN_DEBUG_'):
        # These older flags have no Makefile option; audit their direct compiler define.
        for obj, words in result.items():
            if obj == 'xhci.o':
                words.append('-D' + flag + '=1')
    return result


def preprocess(words, overlay=None):
    args = []
    skip = False
    for word in words:
        if skip:
            skip = False
            continue
        if word == '-o':
            skip = True
        elif word not in ('-c', '-MMD', '-MP'):
            args.append(word)
    if overlay:
        args = [str(overlay / word) if word.endswith(('.cpp', '.c')) and (overlay / word).exists() else word for word in args]
        args[1:1] = ['-I' + str(overlay)]
    return subprocess.check_output(args + ['-E', '-P'], cwd=ROOT, text=True).splitlines(True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--flag', action='append', choices=FLAGS)
    parser.add_argument('--source-overlay', type=Path)
    opts = parser.parse_args()
    if opts.source_overlay:
        opts.source_overlay = opts.source_overlay.resolve()
    opts.output.mkdir(parents=True, exist_ok=True)
    base = commands()
    cache = {}
    summary = {}
    for flag in opts.flag or FLAGS:
        variant = commands(flag)
        assert base.keys() == variant.keys()
        command_diff, source_diff, changed = [], [], []
        for obj, normal in base.items():
            diag = variant[obj]
            if normal == diag:
                continue
            command_diff.extend(difflib.unified_diff([shlex.join(normal) + '\n'],
                [shlex.join(diag) + '\n'], fromfile=f'production/{obj}', tofile=f'{flag}/{obj}'))
            if obj not in cache:
                cache[obj] = preprocess(normal, opts.source_overlay)
            diff = list(difflib.unified_diff(cache[obj], preprocess(diag, opts.source_overlay),
                fromfile=f'production/{obj}', tofile=f'{flag}/{obj}'))
            if diff:
                changed.append(obj)
                source_diff.extend(diff)
        (opts.output / f'{flag}-commands.diff').write_text(''.join(command_diff))
        (opts.output / f'{flag}-preprocessed.diff').write_text(''.join(source_diff))
        summary[flag] = {'changed_translation_units': changed,
                         'preprocessed_diff_lines': len(source_diff)}
    (opts.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
