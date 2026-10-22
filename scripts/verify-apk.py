#!/usr/bin/env python3
"""Check packaged ABI and ELF load alignment without executing Android code."""
import hashlib
import pathlib
import struct
import sys
import zipfile

apk = pathlib.Path(sys.argv[1])
with zipfile.ZipFile(apk) as archive:
    libraries = [name for name in archive.namelist() if name.startswith('lib/') and name.endswith('.so')]
    assert libraries, 'APK has no native libraries'
    for name in libraries:
        assert name.startswith('lib/arm64-v8a/'), f'Unexpected ABI: {name}'
        data = archive.read(name)
        assert data[:6] == b'\x7fELF\x02\x01', f'Not ELF64 little-endian: {name}'
        assert struct.unpack_from('<H', data, 18)[0] == 183, f'Not AArch64: {name}'
        phoff = struct.unpack_from('<Q', data, 32)[0]
        phentsize, phnum = struct.unpack_from('<HH', data, 54)
        for i in range(phnum):
            header = phoff + phentsize * i
            if struct.unpack_from('<I', data, header)[0] == 1:
                alignment = struct.unpack_from('<Q', data, header + 48)[0]
                assert alignment >= 16384, f'ELF lacks 16 KiB alignment: {name}'
    assert any('libagbplay' in name for name in libraries), 'Missing agbplay engine'
print(f'Validated {len(libraries)} AArch64 libraries with 16 KiB alignment')
print(f'{hashlib.sha256(apk.read_bytes()).hexdigest()}  {apk.name}')
