#!/usr/bin/env python3
"""Capture embedded CLI images from a loaded console module for local analysis.

Requires probe.elf. Captures remain in ignored build/; never commit Sony code.
"""
import argparse
from pathlib import Path
import re
import struct
import urllib.request

p = argparse.ArgumentParser()
p.add_argument('module')
p.add_argument('--pid', type=int, default=101)
p.add_argument('--host', default='192.168.0.190')
a = p.parse_args()
base_url = f'http://{a.host}:9118'
def get(path):
    return urllib.request.urlopen(base_url + path, timeout=30).read()
info = get(f'/module/{a.pid}/{a.module}').decode()
m = re.search(r'result=0 handle=[0-9a-f]+ base=([0-9a-f]+)', info)
if not m:
    raise SystemExit(info)
base = int(m[1], 16)
maps = []
for line in get(f'/maps/{a.pid}').decode().splitlines():
    start, end, prot, *_ = line.split()
    maps.append((int(start,16), int(end,16), int(prot,16)))
started = False
out = Path('build/captures'); out.mkdir(parents=True, exist_ok=True)
for start,end,prot in maps:
    if start == base:
        started = True
        continue
    if not started:
        continue
    if prot & 4:  # next module's executable segment
        break
    data = get(f'/memory/{a.pid}/{start:x}/{end-start:x}')
    pos = 0
    while True:
        pos = data.find(b'MZ',pos)
        if pos < 0: break
        if pos+64 <= len(data):
            pe = pos + struct.unpack_from('<I',data,pos+60)[0]
            if data[pe:pe+4] == b'PE\0\0':
                dest = out / (a.module.removesuffix('.sprx'))
                dest.write_bytes(data[pos:])
                print(f'{dest}: memory={start+pos:x} bytes={len(data)-pos}')
        pos += 2
