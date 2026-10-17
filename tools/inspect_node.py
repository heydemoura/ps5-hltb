#!/usr/bin/env python3
"""Read captured React shadow-node ancestry. Offsets verified on FW 12.20 only."""
import argparse
import struct
import urllib.request
p=argparse.ArgumentParser();p.add_argument('address',type=lambda s:int(s,16));p.add_argument('--host',default='192.168.0.190');p.add_argument('--pid',type=int,default=101);a=p.parse_args()
def read(addr,n):
 return urllib.request.urlopen(f'http://{a.host}:9118/memory/{a.pid}/{addr:x}/{n:x}',timeout=5).read()
def string(ptr):
 if not ptr:return ''
 try:
  b=read(ptr,20);n=struct.unpack_from('<i',b,16)[0]
  if not 0<=n<=1024:return '<not a string>'
  return read(ptr+20,n*2).decode('utf-16-le',errors='replace')
 except Exception:return '<unreadable>'
node=a.address
for depth in range(12):
 if not node:break
 try:b=read(node,160)
 except Exception as e:print(e);break
 q=lambda o:struct.unpack_from('<Q',b,o)[0]
 print(f'{depth}: node={node:x} root={q(16):x} parent={q(40):x} tag={struct.unpack_from("<i",b,124)[0]} view={string(q(88))!r} rect={struct.unpack_from("<4f",b,104)}')
 node=q(40)
