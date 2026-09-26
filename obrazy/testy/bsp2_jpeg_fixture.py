#!/usr/bin/env python3
"""Copy a private QEMU disk and insert the unchanged JPEG as a BSP2 user file.

This is a test fixture helper, not an OS installer. It refuses to edit an
existing output image or replace a file already present in BSP2.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess

BLOCK = 4096
ABSENT = (1 << 64) - 1
JPEG_SHA256 = 'd7e181d92652dcf8ffa20155cb77776bd8ae89b47ba773713abfefe150f18438'


class Bsp2:
    def __init__(self, image):
        self.file = image.open('r+b')
        self.base = self._find_volume()
        magic, self.total_size, self.blocks, self.root, self.inodes, self.data = struct.unpack(
            '<4sQQQQQ', self.read(self.base, 44))
        if magic != b'BSP2' or not 1 < self.inodes < self.data < self.blocks or self.root == 0:
            raise RuntimeError('invalid BSP2 superblock')
        self.data_count = self.blocks - self.data
        self.bitmap = bytearray(self.read(self.base + BLOCK, (self.data_count + 7) // 8))

    def read(self, off, size):
        self.file.seek(off)
        got = self.file.read(size)
        if len(got) != size:
            raise RuntimeError('short disk read')
        return got

    def write(self, off, data):
        self.file.seek(off)
        if self.file.write(data) != len(data):
            raise RuntimeError('short disk write')

    def _find_volume(self):
        header = self.read(512, 92)
        if header[:8] != b'EFI PART':
            raise RuntimeError('source disk is not an installed GPT image')
        entries_lba = struct.unpack_from('<Q', header, 72)[0]
        count = min(struct.unpack_from('<I', header, 80)[0], 128)
        size = struct.unpack_from('<I', header, 84)[0]
        if size < 128 or size > 4096:
            raise RuntimeError('invalid GPT entry size')
        for idx in range(count):
            entry = self.read(entries_lba * 512 + idx * size, size)
            if entry[:16] == bytes(16):
                continue
            start = struct.unpack_from('<Q', entry, 32)[0]
            candidate = start * 512
            if self.read(candidate, 4) == b'BSP2':
                return candidate
        raise RuntimeError('BSP2 partition not found')

    def inode_offset(self, number):
        if not 1 <= number <= self.data - self.inodes:
            raise RuntimeError('invalid inode number')
        return self.base + (self.inodes + number - 1) * BLOCK

    def inode(self, number):
        return bytearray(self.read(self.inode_offset(number), BLOCK))

    def data_offset(self, number):
        if not 0 <= number < self.data_count:
            raise RuntimeError('invalid data block')
        return self.base + (self.data + number) * BLOCK

    def pointers(self, inode):
        return struct.unpack_from('<490Q', inode, 25)

    def find_child(self, parent, name):
        inode = self.inode(parent)
        if inode[0] != 2:
            raise RuntimeError('parent is not a directory')
        for block in self.pointers(inode):
            if block == ABSENT:
                continue
            entries = self.read(self.data_offset(block), BLOCK)
            for offset in range(0, BLOCK, 64):
                number = struct.unpack_from('<Q', entries, offset)[0]
                entry_name = entries[offset + 8:offset + 64].split(b'\0', 1)[0]
                if number and entry_name == name.encode('utf-8'):
                    return number
        return 0

    def alloc_block(self):
        for number in range(self.data_count):
            byte, bit = divmod(number, 8)
            mask = 1 << bit
            if not self.bitmap[byte] & mask:
                self.bitmap[byte] |= mask
                return number
        raise RuntimeError('BSP2 data full')

    def insert(self, parent, name, content):
        if len(name.encode()) > 55 or self.find_child(parent, name):
            raise RuntimeError('JPEG destination exists or name too long')
        inode_number = next((n for n in range(1, self.data - self.inodes + 1)
                             if self.read(self.inode_offset(n), 1) == b'\0'), None)
        if not inode_number:
            raise RuntimeError('BSP2 inode table full')
        parent_inode = self.inode(parent)
        parent_block = None
        parent_entry = None
        for index, block in enumerate(self.pointers(parent_inode)):
            if block == ABSENT:
                parent_block = self.alloc_block()
                struct.pack_into('<Q', parent_inode, 25 + 8 * index, parent_block)
                entries = bytearray(BLOCK)
                parent_entry = 0
                break
            entries = bytearray(self.read(self.data_offset(block), BLOCK))
            for offset in range(0, BLOCK, 64):
                if struct.unpack_from('<Q', entries, offset)[0] == 0:
                    parent_block, parent_entry = block, offset
                    break
            if parent_entry is not None:
                break
        if parent_entry is None:
            raise RuntimeError('directory full')
        blocks = [self.alloc_block() for _ in range((len(content) + BLOCK - 1) // BLOCK)]
        inode = bytearray(BLOCK)
        inode[0] = 1
        struct.pack_into('<Q', inode, 1, len(content))
        struct.pack_into('<Q', inode, 17, 0x5053464D02000204)  # metadata v2, PZB 4
        for index in range(490):
            struct.pack_into('<Q', inode, 25 + index * 8, blocks[index] if index < len(blocks) else ABSENT)
        for index in range(3):
            struct.pack_into('<Q', inode, 3945 + index * 8, ABSENT)
        if len(blocks) > 490:
            indirect = self.alloc_block()
            struct.pack_into('<Q', inode, 3945, indirect)
            pointers = bytearray(b'\xff' * BLOCK)
            for index, block in enumerate(blocks[490:]):
                struct.pack_into('<Q', pointers, index * 8, block)
            self.write(self.data_offset(indirect), pointers)
        for index, block in enumerate(blocks):
            chunk = content[index * BLOCK:(index + 1) * BLOCK]
            self.write(self.data_offset(block), chunk.ljust(BLOCK, b'\0'))
        self.write(self.inode_offset(inode_number), inode)
        name_bytes = name.encode('utf-8')
        entries[parent_entry + 8:parent_entry + 64] = name_bytes.ljust(56, b'\0')
        struct.pack_into('<Q', entries, parent_entry, inode_number)
        self.write(self.data_offset(parent_block), entries)
        count = struct.unpack_from('<Q', parent_inode, 1)[0]
        struct.pack_into('<Q', parent_inode, 1, count + 1)
        self.write(self.inode_offset(parent), parent_inode)
        self.write(self.base + BLOCK, self.bitmap)
        self.file.flush()
        stored = b''.join(self.read(self.data_offset(block), BLOCK) for block in blocks)[:len(content)]
        if hashlib.sha256(stored).digest() != hashlib.sha256(content).digest():
            raise RuntimeError('BSP2 JPEG readback hash mismatch')
        print(f'BSP2 fixture: /uzytkownicy/Pulpit/{name} {len(content)} B, inode {inode_number}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-disk', required=True, type=Path)
    parser.add_argument('--output-disk', required=True, type=Path)
    parser.add_argument('--jpeg', required=True, type=Path)
    args = parser.parse_args()
    content = args.jpeg.read_bytes()
    if hashlib.sha256(content).hexdigest() != JPEG_SHA256:
        raise RuntimeError('JPEG source hash mismatch')
    if args.output_disk.exists():
        raise RuntimeError('refusing to overwrite output disk')
    subprocess.run(['cp', '--reflink=auto', '--sparse=always', '--',
                    str(args.source_disk), str(args.output_disk)], check=True)
    volume = Bsp2(args.output_disk)
    users = volume.find_child(volume.root, 'uzytkownicy')
    desktop = volume.find_child(users, 'Pulpit') if users else 0
    if not desktop:
        raise RuntimeError('user desktop directory missing')
    volume.insert(desktop, 'tapeta.jpg', content)
    volume.file.close()
    if hashlib.sha256(args.jpeg.read_bytes()).hexdigest() != JPEG_SHA256:
        raise RuntimeError('JPEG source changed')


if __name__ == '__main__':
    main()
