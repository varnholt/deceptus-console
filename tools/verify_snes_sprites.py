"""Compare the assembly sprite drawing with the C reference in Snes9x."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import build_snes
from emulator import Emulator, ROOT


def verify(assets, sdk, core):
    folder = ROOT / 'build/tests/snes-sprite-differential'
    source = ROOT / 'src/platform/snes/main.c'
    replacement = ROOT / 'tests/snes_sprite_rom_tests.c'
    # the test includes sprites.c for its static reference functions, so sprites.c itself
    # compiles to an empty object.
    sprites = ROOT / 'src/platform/snes/video/sprites.c'
    folder.mkdir(parents=True, exist_ok=True)
    empty = folder / 'sprites_in_test.c'
    empty.write_text('/* sprites.c is compiled through tests/snes_sprite_rom_tests.c. */\n'
                     'typedef int SnesSpritesInTest;\n')
    original = build_snes.run

    def run(command, working_directory):
        if '-c' in command and source in command:
            command = [replacement if argument == source else argument for argument in command]
            command.append('-I' + str(source.parent))
        if '-c' in command and sprites in command:
            command = [empty if argument == sprites else argument for argument in command]
        original(command, working_directory)

    build_snes.run = run
    try:
        build_snes.build(sdk, ROOT / 'assets/game/data',
                         assets_dir=assets, output_dir=folder,
                         reuse_assets=True)
    finally:
        build_snes.run = original
    rom = folder / 'deceptus-tutorial.sfc'
    emulator = Emulator(core, rom)
    try:
        for frame in range(60000):
            emulator.run(1)
            memory = emulator.memory()
            offset = memory.find(struct.pack('<HH', 0x53a7, 0xd19e))
            if offset < 0:
                continue
            values = struct.unpack_from('<16H', memory, offset)
            if values[2]:
                break
        else:
            raise AssertionError('sprite differential ROM did not finish')
        report = dict(passed=values[2] == 1, cases=values[3] + 1, mailbox=values,
                      rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
                      display_frames=emulator.video_frames)
        output = ROOT / 'artifacts/snes-sprite-differential.json'
        output.write_text(json.dumps(report, indent=2) + '\n')
        assert report['passed'], report
        print(json.dumps(report, indent=2))
    finally:
        emulator.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets-dir', type=Path, default=ROOT / 'assets/generated/snes')
    parser.add_argument('--sdk', type=Path, default=ROOT / '.tools/pvsneslib')
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/snes9x/snes9x_libretro.dll')
    args = parser.parse_args()
    verify(args.assets_dir.resolve(), args.sdk.resolve(), args.core.resolve())
