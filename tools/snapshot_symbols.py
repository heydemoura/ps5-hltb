#!/usr/bin/env python3
"""Record symbol offsets for the exact renderer successfully deployed now."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
root=Path(__file__).resolve().parent.parent
elf=root/'build/shell_native.elf'
nm=shutil.which('llvm-nm') or '/opt/homebrew/opt/llvm/bin/llvm-nm'
symbols={'_elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest()}
for line in subprocess.check_output([nm,'-n',str(elf)],text=True).splitlines():
    p=line.split()
    if len(p)==3:
        try:symbols[p[2]]=int(p[0],16)
        except ValueError:pass
(root/'build/loaded-native.symbols.json').write_text(json.dumps(symbols))
