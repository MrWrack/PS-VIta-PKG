#!/usr/bin/env python3
from pathlib import Path
import sys, zipfile, tempfile, os

if len(sys.argv) != 2:
    raise SystemExit('usage: repack_vpk_uncompressed.py <VPKManager.vpk>')

src = Path(sys.argv[1])
if not src.is_file():
    raise SystemExit(f'VPK not found: {src}')

fd, tmp_name = tempfile.mkstemp(prefix=src.stem + '-', suffix='.vpk', dir=str(src.parent))
os.close(fd)
tmp = Path(tmp_name)
try:
    with zipfile.ZipFile(src, 'r') as zin, zipfile.ZipFile(tmp, 'w', compression=zipfile.ZIP_STORED) as zout:
        for info in zin.infolist():
            data = zin.read(info.filename)
            new = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            new.external_attr = info.external_attr
            new.internal_attr = info.internal_attr
            new.create_system = info.create_system
            new.flag_bits = info.flag_bits & ~0x08
            new.compress_type = zipfile.ZIP_STORED
            zout.writestr(new, data)
    tmp.replace(src)
finally:
    if tmp.exists():
        tmp.unlink()
print(f'Repacked Vita-safe uncompressed VPK: {src}')
