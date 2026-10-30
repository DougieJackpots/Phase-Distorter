#!/usr/bin/env python3
"""Fail before packaging if Vita PNGs or LiveArea references are invalid."""
from pathlib import Path
import struct
import zlib
import xml.etree.ElementTree as ET


def validate(root):
    sizes = {'icon0.png': (128, 128), 'livearea/contents/bg.png': (840, 500),
             'livearea/contents/startup.png': (280, 158)}
    for name, size in sizes.items():
        data = (root / name).read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n':
            raise ValueError(f'{name}: invalid PNG signature')
        offset, header, compressed, palette, ended = 8, None, bytearray(), False, False
        while offset < len(data):
            length = struct.unpack_from('>I', data, offset)[0]
            kind = data[offset+4:offset+8]
            body = data[offset+8:offset+8+length]
            crc = struct.unpack_from('>I', data, offset+8+length)[0]
            if zlib.crc32(kind + body) & 0xffffffff != crc:
                raise ValueError(f'{name}: corrupt {kind!r} chunk')
            if kind == b'IHDR': header = struct.unpack('>IIBBBBB', body)
            if kind == b'PLTE': palette = 0 < length <= 768 and length % 3 == 0
            if kind == b'IDAT': compressed.extend(body)
            offset += length + 12
            if kind == b'IEND':
                ended = True
                break
        if not header or header[:2] != size or header[2:] != (8, 3, 0, 0, 0):
            raise ValueError(f'{name}: expected {size}, indexed 8-bit, non-interlaced PNG')
        if not palette or not ended:
            raise ValueError(f'{name}: missing palette or end chunk')
        rows = zlib.decompress(compressed)
        if len(rows) != (size[0] + 1) * size[1]:
            raise ValueError(f'{name}: invalid decoded image length')
    contents = root / 'livearea/contents'
    template = ET.parse(contents / 'template.xml').getroot()
    for tag, expected in [('livearea-background/image', 'bg.png'), ('gate/startup-image', 'startup.png')]:
        if template.findtext(tag) != expected or not (contents / expected).is_file():
            raise ValueError(f'LiveArea {tag}: missing or incorrect image')
    print('Vita artwork validated: dimensions, indexed 8-bit PNGs, CRCs, image data and XML references')


if __name__ == '__main__':
    validate(Path(__file__).resolve().parent / 'sce_sys')
