"""Export real U8g2 glyphs for a browser preview; no fonts are approximated."""
import codecs
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / '.pio/libdeps/ultra/U8g2_for_Adafruit_GFX/src/u8g2_fonts.c'
NAMES = ['6x12', '6x13', '7x13']


def font_bytes(name, source):
    symbol = f'u8g2_font_{name}_t_cyrillic'
    match = re.search(r'const uint8_t ' + symbol + r'\[\d*\][^=]+?=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', source)
    if not match:
        raise ValueError(f'Missing font {symbol}')
    strings = re.findall(r'"((?:[^"\\]|\\.)*)"', match[1])
    return b''.join(codecs.decode(s, 'unicode_escape').encode('latin1') for s in strings) + b'\0'


def glyph(data, cp):
    word = lambda p: (data[p] << 8) | data[p + 1]
    p = 23
    if cp <= 255:
        if cp >= 97:
            p += word(19)
        elif cp >= 65:
            p += word(17)
        while data[p + 1]:
            if data[p] == cp:
                p += 2
                break
            p += data[p + 1]
        else:
            return None
    else:
        p += word(21)
        table = p
        while True:
            p += word(table)
            end = word(table + 2)
            table += 4
            if end >= cp:
                break
        while word(p):
            if word(p) == cp:
                p += 3
                break
            p += data[p + 2]
        else:
            return None
    bit = p * 8

    def unsigned(n):
        nonlocal bit
        result = sum(((data[(bit + i) // 8] >> ((bit + i) % 8)) & 1) << i for i in range(n))
        bit += n
        return result

    def signed(n):
        return unsigned(n) - (1 << (n - 1))

    width, height = unsigned(data[4]), unsigned(data[5])
    x, y, advance = signed(data[6]), signed(data[7]), signed(data[8])
    rows = [0] * height
    position = 0
    if width:
        for _ in range(1000):
            zeros, ones = unsigned(data[2]), unsigned(data[3])
            while True:
                position += zeros
                for index in range(position, min(position + ones, width * height)):
                    rows[index // width] |= 1 << (index % width)
                position += ones
                if not unsigned(1):
                    break
            if position >= width * height:
                break
        else:
            raise ValueError('Invalid run-length glyph')
    return [width, height, x, y, advance, rows]


if __name__ == '__main__':
    source = SOURCE.read_text(encoding='latin1')
    result = {}
    for name in NAMES:
        data = font_bytes(name, source)
        result[name] = {}
        for cp in [*range(32, 127), *range(0x400, 0x530)]:
            decoded = glyph(data, cp)
            if decoded:
                result[name][chr(cp)] = decoded
        assert all(ch in result[name] for ch in 'ҐґЄєІіЇї'), f'Ukrainian glyphs missing in {name}'
    destination = ROOT / 'preview/fonts.json'
    destination.parent.mkdir(exist_ok=True)
    destination.write_text(json.dumps(result, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    print(f'Exported {sum(len(v) for v in result.values())} glyphs to {destination}')
