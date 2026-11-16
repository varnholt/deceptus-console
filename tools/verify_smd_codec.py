"""Compare the actual portable C Mega Drive decoder with every lossless block."""
import argparse
import ctypes as C
import json
from pathlib import Path
import re
import struct
import subprocess

from emulator import ROOT


class Blob(C.Structure):
    _fields_ = [('data', C.POINTER(C.c_ubyte)), ('offsets', C.POINTER(C.c_uint32)),
                ('block_bytes', C.c_ushort), ('blocks', C.c_ushort), ('unit_bytes', C.c_ubyte)]


def verify(assets):
    folder = ROOT / 'build/tests/smd-codec'
    folder.mkdir(parents=True, exist_ok=True)
    bridge = folder / 'bridge.c'
    bridge.write_text('#include "opening_video.h"\n'
        '__declspec(dllexport) int decode(const SmdRomBlob*b,unsigned short i,unsigned char*d)'
        '{return smdDecodeBlock(b,i,d);}\n'
        '__declspec(dllexport) const unsigned char*cell(const SmdRomBlob*b,unsigned short y,unsigned short x)'
        '{return smdCell(b,y,x);}\n')
    library = folder / 'codec.dll'
    subprocess.run(['clang', '-shared', '-O2', '-std=c90', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src/platform/smd'), '-I' + str(assets),
                    str(bridge), str(ROOT / 'src/platform/smd/graphics_io.c'), '-o', str(library)], check=True)
    codec = C.CDLL(str(library))
    codec.decode.argtypes = [C.POINTER(Blob), C.c_ushort, C.POINTER(C.c_ubyte)]
    codec.cell.argtypes = [C.POINTER(Blob), C.c_ushort, C.c_ushort]
    codec.cell.restype = C.c_void_p
    source = (assets / 'opening_resources.c').read_text()
    descriptions = list(dict.fromkeys(re.findall(r'\{(\w+)_rle,\1_offsets,(\d+),(\d+),(\d+)\}', source)))
    blocks, cells = 0, 0
    for name, block_bytes, count, unit in descriptions:
        block_bytes, count, unit = int(block_bytes), int(count), int(unit)
        payload = (assets / (name + '_rle.bin')).read_bytes()
        raw = (assets / (name + '.bin')).read_bytes()
        offsets_raw = (assets / (name + '_offsets.bin')).read_bytes()
        offsets = (C.c_uint32 * (count + 1))(*struct.unpack('>' + str(count + 1) + 'I', offsets_raw))
        data = (C.c_ubyte * len(payload)).from_buffer_copy(payload)
        blob = Blob(data, offsets, block_bytes, count, unit)
        output = (C.c_ubyte * block_bytes)()
        for index in range(count):
            assert codec.decode(C.byref(blob), index, output), (name, index)
            assert bytes(output) == raw[index * block_bytes:(index + 1) * block_bytes], (name, index)
            blocks += 1
            if unit == 4:
                for column in range(block_bytes // 4):
                    address = codec.cell(C.byref(blob), index, column)
                    assert address and C.string_at(address, 4) == raw[index * block_bytes + column * 4:
                                                                      index * block_bytes + column * 4 + 4]
                    cells += 1
        assert not codec.decode(C.byref(blob), count, output)
        if unit == 4:
            assert not codec.cell(C.byref(blob), 0, block_bytes // 4)
    assert blocks > 80000, ('incomplete codec coverage', len(descriptions), blocks)
    report = dict(passed=True, blobs=len(descriptions), blocks=blocks, cells=cells,
                  scope='actual C block decoder and random-access tuple lookup against lossless imports')
    (ROOT / 'artifacts/smd-codec-c-check.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets-dir', type=Path, default=ROOT / 'assets/generated/smd-opening')
    verify(parser.parse_args().assets_dir.resolve())
