#!/usr/bin/env python3
"""Send the SDK-built title query payload and a title on stdin to elfldr."""
import argparse
from pathlib import Path
import socket
import sys
p = argparse.ArgumentParser()
p.add_argument('title')
p.add_argument('--host', default='192.168.0.190')
p.add_argument('--port', type=int, default=9021)
a = p.parse_args()
if not a.title.strip() or len(a.title.encode()) > 500 or '\n' in a.title or '\r' in a.title:
    p.error('title must be one line, 1–500 UTF-8 bytes')
root = Path(__file__).resolve().parent.parent
chunks = []
with socket.create_connection((a.host, a.port), timeout=10) as s:
    s.settimeout(60)
    s.sendall((root/'build/hltb-query.elf').read_bytes())
    s.sendall(a.title.encode() + b'\n')
    s.shutdown(socket.SHUT_WR)
    while data := s.recv(16384):
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        chunks.append(data)
raise SystemExit(0 if b'HLTB game=' in b''.join(chunks) else 1)
