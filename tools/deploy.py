#!/usr/bin/env python3
"""Deploy an ELF to elfldr and require its startup-success message."""
import argparse
from pathlib import Path
import socket
import sys
p=argparse.ArgumentParser()
p.add_argument('payload',type=Path)
p.add_argument('--host',default='192.168.0.190')
p.add_argument('--port',type=int,default=9021)
p.add_argument('--expect',default='HowLongToBeat native title annotations enabled')
a=p.parse_args()
chunks=[]
with socket.create_connection((a.host,a.port),timeout=10) as s:
    s.settimeout(60)
    s.sendall(a.payload.read_bytes())
    s.shutdown(socket.SHUT_WR)
    while data:=s.recv(16384):
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        chunks.append(data)
if a.expect.encode() not in b''.join(chunks):
    print('Payload did not report successful startup.',file=sys.stderr)
    raise SystemExit(1)
