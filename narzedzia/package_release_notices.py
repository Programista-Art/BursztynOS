#!/usr/bin/env python3
"""Retain original attribution and third-party terms in release media."""
import pathlib,shutil,sys
root=pathlib.Path(__file__).resolve().parents[1]
dest=pathlib.Path(sys.argv[1])/'licenses';dest.mkdir(parents=True,exist_ok=True)
for src,name in [('narzedzia/unicode/LICENSE.txt','Unicode.txt'),('License.md','BursztynOS.md'),('biblioteki/mbedtls/LICENSE','mbedtls.txt'),('bsf/vendor/LICENSE','stb-truetype.txt'),('czcionki/ttf/LICENSE-DejaVu.txt','DejaVu.txt')]:
 shutil.copy2(root/src,dest/name)
text=(root/'obrazy/vendor/stb_image.h').read_text()
start=text.rfind('ALTERNATIVE A - MIT License')
if start<0:raise SystemExit('stb_image license not found')
(dest/'stb-image.txt').write_text(text[start:])
for src,name in [('/usr/share/doc/grub-common/copyright','GRUB-copyright.txt'),('/usr/share/common-licenses/GPL-3','GPL-3.txt')]:
 shutil.copy2(src,dest/name)
(dest/'README.txt').write_text('Bursztyn OS attribution and component notices. Third-party terms apply to their respective components. GRUB is separately distributed boot software. Distributors must provide corresponding GRUB source using an applicable GPL distribution method; these notices alone are not a source offer. Preserve the exact distribution GRUB source package and patches alongside published binaries.\n')
