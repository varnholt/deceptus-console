"""Measure simulation throughput in an already-built stationary SNES fixture."""
import argparse
import collections
import hashlib
import json
from pathlib import Path

from emulator import Emulator, ROOT
from verify_snes_opening import snes_state


def benchmark(core, rom, sample_frames=1200):
    emulator = Emulator(core, rom)
    try:
        emulator.run(2000)
        start_frames = 0
        if snes_state(emulator)['mode'] == 0:
            start_frames = 60
            emulator.run(start_frames, (3,))
        emulator.run(1000)
        before = snes_state(emulator)
        if before['mode'] != 1:
            raise AssertionError(('benchmark requires active stationary play', before))
        histogram = collections.Counter()
        previous, ticks = before, 0
        for frame in range(sample_frames):
            emulator.run(1)
            state = snes_state(emulator)
            histogram[state['phase']] += 1
            if state['room'] != before['room'] or state['mode'] != 1:
                raise AssertionError(('benchmark left the stationary scene', state))
            delta = (state['ticks'] - previous['ticks']) & 65535
            if delta >= 100:
                raise AssertionError(('simulation counter reset unexpectedly', previous, state))
            ticks += delta
            previous = state
        return dict(rom=Path(rom).as_posix(), sha256=hashlib.sha256(Path(rom).read_bytes()).hexdigest(),
                    emulator=emulator.version, fps=emulator.fps, warmup_frames=3000 + start_frames,
                    sample_frames=sample_frames, simulation_ticks=ticks,
                    simulation_ticks_per_second=ticks * emulator.fps / sample_frames,
                    before=before, after=state, phase_histogram=dict(histogram))
    finally:
        emulator.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/snes9x/snes9x_libretro.dll')
    parser.add_argument('--rom', type=Path, action='append', required=True)
    parser.add_argument('--frames', type=int, default=1200)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/snes-warm-throughput.json')
    args = parser.parse_args()
    if args.frames < 1:
        parser.error('--frames must be positive')
    reports = []
    for rom in args.rom:
        report = benchmark(args.core, rom, args.frames)
        reports.append(report)
        print(json.dumps(report), flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(reports, indent=2) + '\n')
