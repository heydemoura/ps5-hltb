#!/usr/bin/env python3
"""Inspect this exact loaded native payload's counters through the probe."""
import argparse
from pathlib import Path
import json
import shutil
import struct
import subprocess
import urllib.request
import urllib.error
import sys
p=argparse.ArgumentParser();p.add_argument('--host',default='192.168.0.190');a=p.parse_args()
def get(path):
    try:
        return urllib.request.urlopen(f'http://{a.host}:9118{path}',timeout=5).read()
    except urllib.error.URLError as e:
        print(f'Console inspection failed: {e}. Start the read-only probe with make deploy-probe.',file=sys.stderr)
        raise SystemExit(1)
root=Path(__file__).resolve().parent.parent
symbols=json.loads((root/'build/loaded-native.symbols.json').read_text())
magic,pid,kind,method,hook,tramp=struct.unpack('<QiIQQQ',get('/file/system_tmp/hltb-native-text.req'))
get(f'/maps/{pid}')  # Reject a stale request after ShellUI restarts.
base=hook-symbols['text_hook']
print(f'pid={pid} image-base={base:x} method={method:x}')
for name,fmt in [('frames','<I'),('enabled','<I'),('game_count','<Q'),('setter_thread','<Q'),('update_thread','<Q')]:
 if name not in symbols:continue
 value=struct.unpack(fmt,get(f'/memory/{pid}/{base+symbols[name]:x}/{struct.calcsize(fmt):x}'))[0]
 print(f'{name}={value:x}' if 'thread' in name else f'{name}={value}')
