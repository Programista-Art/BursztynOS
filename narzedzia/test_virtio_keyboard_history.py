"""Compile the actual f6541df accumulator and the current one; no emulator."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
out = root / '.virtiotest/forensic-validation'
out.mkdir(exist_ok=True)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
rows = []
with tempfile.TemporaryDirectory(prefix='bursztyn-keyboard-history-') as directory:
    old = Path(directory) / 'input_device.cpp'
    old.write_bytes(subprocess.check_output(['git', 'show', 'f6541df:input_device.cpp'], cwd=root))
    for before, source in [(1, old), (0, root / 'input_device.cpp')]:
        binary = out / f'keyboard-claim-{before}'
        subprocess.run(['g++', '-std=c++17', '-O1', '-g', '-I.', '-DBURSZTYN_VIRTIO_HOST_TEST',
                        f'-DFORENSIC_BEFORE_MIMO={before}', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', str(source), 'narzedzia/test_virtio_keyboard_isolation.cpp',
                        '-o', str(binary)], cwd=root, check=True)
        rows.append(subprocess.check_output([binary], cwd=root, env=env, text=True).strip())
(out / 'previous-claim-history.log').write_text('\n'.join(rows) + '\n')
print('\n'.join(rows))
