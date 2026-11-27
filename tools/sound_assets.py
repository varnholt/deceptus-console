"""The original's sound effects for the consoles' sound cues (include/deceptus/soundcue.h).

Each cue plays one of the original's files at the volume the original gives it. A platform's bank is
the cues' samples decoded (the soundfile package reads OGG), mixed to mono, resampled to the
platform's rate and stored as signed 8-bit, with the silence the files end on trimmed.
"""
import re
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'include/deceptus/soundcue.h'
# cue -> (file in data/sounds, volume). footstep volumes are data/config/footsteps.json's.
CUES = {
    'SOUND_JUMP': ('player_jump_liftoff.ogg', 1.0),
    'SOUND_LAND': ('player_jump_land.ogg', 1.0),
    'SOUND_STEP_STONE_LEFT': ('player_footstep_stone_l.ogg', 0.5),
    'SOUND_STEP_STONE_RIGHT': ('player_footstep_stone_r.ogg', 0.5),
    'SOUND_STEP_GRASS_LEFT': ('player_footstep_grass_l_0.ogg', 0.4),
    'SOUND_STEP_GRASS_RIGHT': ('player_footstep_grass_r_0.ogg', 0.4),
    'SOUND_HURT': ('hurt.ogg', 1.0),
    'SOUND_CHECKPOINT': ('player_spawn_01.ogg', 1.0),
    'SOUND_ITEM': ('extra_02.ogg', 1.0),
    'SOUND_LEVER_INSERT': ('mechanism_switch_lever_insert.ogg', 1.0),
    'SOUND_DIALOGUE': ('messagebox_open_01.ogg', 1.0),
}
# the level below which a file's tail counts as silence, in full scale.
SILENCE = 1 / 256


def cue_numbers(header=HEADER):
    values = dict(re.findall(r'#define (SOUND_\w+) (\d+)', header.read_text()))
    numbers = {name: int(value) for name, value in values.items() if name in CUES}
    missing = set(CUES) - set(numbers)
    if missing:
        raise ValueError(f'sound cues missing from {header.name}: {sorted(missing)}')
    return numbers, int(values['SOUND_CUE_COUNT'])


def decode(path, rate, volume):
    import soundfile
    data, source_rate = soundfile.read(str(path), always_2d=True)
    mono = data.mean(axis=1) * volume
    count = max(1, int(round(len(mono) * rate / source_rate)))
    resampled = np.interp(np.arange(count) * source_rate / rate, np.arange(len(mono)), mono)
    loud = np.nonzero(np.abs(resampled) >= SILENCE)[0]
    resampled = resampled[:loud[-1] + 1] if len(loud) else resampled[:1]
    return np.clip(np.round(resampled * 127), -128, 127).astype(np.int8)


def compile_bank(data, rate, name, sample_attribute='', tail=0):
    """C definitions of a platform's bank: name is a const SoundEffect[SOUND_CUE_COUNT]. tail silent
    values follow each cue without counting in its length, for a mixer that checks the end only now
    and then."""
    numbers, count = cue_numbers()
    lines, entries = [], ['   {0, 0},'] * count
    for cue, (filename, volume) in CUES.items():
        samples = decode(Path(data) / 'sounds' / filename, rate, volume)
        label = f'{name}_{cue.lower()}'
        values = list(samples) + [0] * tail
        rows = [', '.join(str(int(value)) for value in values[index:index + 32]) for index in range(0, len(values), 32)]
        lines += [f'static const DeceptusI8 {label}[]{sample_attribute} = {{'] + ['   ' + row + ',' for row in rows] + ['};']
        entries[numbers[cue]] = f'   {{{label}, {len(samples)}}},'
    lines += [f'const SoundEffect {name}[SOUND_CUE_COUNT] = {{'] + entries + ['};']
    return lines
