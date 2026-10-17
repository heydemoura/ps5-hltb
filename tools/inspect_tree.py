#!/usr/bin/env python3
"""Read-only FW 12.20 React shadow-tree snapshot via the diagnostic probe."""
import argparse
import json
import struct
import urllib.request
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('root', type=lambda value: int(value, 16))
p.add_argument('--host', default='192.168.0.190')
p.add_argument('--pid', type=int, default=101)
p.add_argument('--output', type=Path, default=Path('build/home-tree.json'))
a = p.parse_args()


def read(address, size):
    url = f'http://{a.host}:9118/memory/{a.pid}/{address:x}/{size:x}'
    return urllib.request.urlopen(url, timeout=5).read()


def pointer(data, offset):
    return struct.unpack_from('<Q', data, offset)[0]


def string(address):
    if not address:
        return ''
    size = struct.unpack_from('<i', read(address, 20), 16)[0]
    if not 0 <= size <= 2048:
        raise ValueError('invalid managed string length')
    return read(address + 20, size * 2).decode('utf-16-le', errors='replace') if size else ''


seen = set()
rows = []


def walk(address, depth):
    if not address or address in seen or depth > 48 or len(seen) >= 3000:
        return
    seen.add(address)
    try:
        data = read(address, 344)
        view = string(pointer(data, 88))
        text = string(pointer(data, 136)) if view == 'RCTRawText' else ''
        if view in ('RCTText', 'RCTVirtualText', 'RCTVirtualTextObject', 'RCTMessageIdText'):
            text = string(pointer(data, 312))
        span_info = {}
        if view in ('RCTText', 'RCTVirtualText', 'RCTVirtualTextObject'):
            span = pointer(data, 152)
            if span:
                span_data = read(span, 88)
                span_info = {name: string(pointer(span_data, offset))
                             for name, offset in [('id', 16), ('value', 24), ('translated', 64)]}
                text = text or span_info['translated'] or span_info['value']
        test_id = string(pointer(data, 184)) if view in ('RCTText', 'RCTVirtualText', 'RCTVirtualTextObject') else ''
        row = dict(node=hex(address), parent=hex(pointer(data, 40)), view=view,
                   text=text, span=span_info, test_id=test_id, rect=struct.unpack_from('<4f', data, 104), depth=depth)
        rows.append(row)
        if text:
            print(hex(address), depth, repr(text))
        children = pointer(data, 32)
        if children:
            listing = read(children, 40)
            count = struct.unpack_from('<i', listing, 32)[0]
            if not 0 <= count <= 500:
                raise ValueError('invalid managed list length')
            if count:
                items = read(pointer(listing, 16) + 32, count * 8)
                for child in struct.unpack('<' + 'Q' * count, items):
                    walk(child, depth + 1)
    except Exception as error:
        rows.append(dict(node=hex(address), error=str(error), depth=depth))


walk(a.root, 0)
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(rows, indent=2) + '\n')
print(f'{len(rows)} records saved to {a.output}; live snapshot may change while reading')
