"""Convert original rope strand slices and the source hanging-player animation."""
import hashlib
import json
from pathlib import Path
import re
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
LINKS_SHEET = 'rope-links.png'


def header_define(name):
    header = (ROOT / 'include/deceptus/ropelink.h').read_text()
    return int(re.search(rf'^#define {name} (\d+)$', header, re.M).group(1))


LINK_ANGLES = header_define('ROPE_LINK_ANGLES')
LINK_CELL = header_define('ROPE_LINK_CELL')
LINK_LENGTH = header_define('ROPE_LINK_LENGTH')
KNOT_LENGTH = header_define('ROPE_KNOT_LENGTH')
LINK_IMAGES = 2 * LINK_ANGLES
# rope.cpp's strand: the knot in its first 16 rows, then the plain strand; the 2px strand runs down
# columns 2 and 3 of the 7px texture.
STRAND_CENTRE_X = 3


def compile_rope_links(texture, output):
    """One row of LINK_CELL cells: a plain link, then the knot piece, each turned from pointing left
    through hanging down to pointing right (ropelink.h). Pieces are a pixel longer than the rope
    between their points, so neighbouring pieces meet."""
    pieces = [texture.crop((0, KNOT_LENGTH, 7, KNOT_LENGTH + LINK_LENGTH + 2)), texture.crop((0, 0, 7, KNOT_LENGTH + 1))]
    sheet = Image.new('RGBA', (LINK_CELL * LINK_IMAGES, LINK_CELL))
    centre = LINK_CELL // 2
    step = 180 / (LINK_ANGLES - 1)
    for kind, piece in enumerate(pieces):
        cell = Image.new('RGBA', (LINK_CELL, LINK_CELL))
        cell.alpha_composite(piece, (centre - STRAND_CENTRE_X, centre - piece.height // 2))
        for index in range(LINK_ANGLES):
            turned = cell.rotate(-90 + index * step, resample=Image.Resampling.NEAREST, center=(centre, centre))
            sheet.alpha_composite(turned, ((kind * LINK_ANGLES + index) * LINK_CELL, 0))
    sheet.save(Path(output) / LINKS_SHEET)


def player_rope_frames(data):
    data=Path(data)
    animations=json.loads((data/'sprites/animations.json').read_text())
    frames=[]
    for side in ('r','l'):
        name='player_jump_midair_'+side
        animation=animations[name]
        if animation['frame_size']!=[72,48] or animation['origin']!=[36,48] or animation['sprite_count']!=3:
            raise ValueError('source rope hanging pose changed; review the native render contract')
        elapsed=previous=0
        for index,duration in enumerate(animation['frame_durations']):
            elapsed+=duration
            tick=(elapsed*60+999)//1000
            x,y=animation['frame_offset']
            frames.append(dict(animation=name,source_frame=index,source_rect=[x+index*72,y,72,48],
                               origin=[36,48],duration_ticks=tick-previous))
            previous=tick
    return frames


def compile_rope(data, output):
    data,output=Path(data),Path(output)
    output.mkdir(parents=True,exist_ok=True)
    source_cpp=data.parent/'src/game/mechanisms/rope.cpp'
    compact=re.sub(r'\s+','',source_cpp.read_text())
    if 'sf::IntRect{{0,0},{7,141}}' not in compact:
        raise ValueError('source rope strand atlas changed; review the native slices')
    player_cpp=data.parent/'src/game/player/playeranimation.cpp'
    if 'returndata._points_right?_jump_midair_r:_jump_midair_l;' not in re.sub(r'\s+','',player_cpp.read_text()):
        raise ValueError('source hanging animation changed; review the native pose mapping')
    source=data/'sprites/ropes.png'
    texture=Image.open(source).convert('RGBA')
    # retain the authored knot once, then repeat the untouched native plain strand.
    slices=[dict(source_rect=[0,0,7,16],placement=[4,0],height=16)]
    slices += [dict(source_rect=[0,16,7,height],placement=[4,4],height=height) for height in range(1,9)]
    atlas=Image.new('RGBA',(16*len(slices),16))
    for index,spec in enumerate(slices):
        x,y,width,height=spec['source_rect']
        atlas.paste(texture.crop((x,y,x+width,y+height)),(index*16+spec['placement'][0],spec['placement'][1]))
    atlas.save(output/'rope-segments.png')
    compile_rope_links(texture, output)
    player=player_rope_frames(data)
    lines=['/* original native-width rope slices; padding is for rotation, never scaled artwork. */',
           '#ifndef GBA_ROPE_RESOURCES_H','#define GBA_ROPE_RESOURCES_H',
           '#define GBA_ROPE_NATIVE_WIDTH 7',
           'static const DeceptusU8 gba_rope_slice_heights[9] = {16,1,2,3,4,5,6,7,8};',
           'static const DeceptusU8 gba_rope_slice_tops[9] = {0,4,4,4,4,4,4,4,4};',
           'static const DeceptusU8 gba_rope_player_clips[2][4] = {{0,3,36,48},{3,3,36,48}};',
           'static const DeceptusU8 gba_rope_player_durations[6] = {'+','.join(str(frame['duration_ticks']) for frame in player)+'};',
           '#endif']
    (output/'gba_rope_resources.h').write_text('\n'.join(lines)+'\n')
    paths=[source,data/'sprites/player.png',data/'sprites/animations.json',source_cpp,
           data.parent/'src/game/mechanisms/rope.h',data.parent/'src/game/mechanisms/grabrope.cpp',
           data.parent/'src/game/mechanisms/grabrope.h',player_cpp]
    report=dict(native_width=7,atlas_size=list(atlas.size),slices=slices,player_frames=player,
                source_hashes={path.relative_to(data.parent).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
                resampling=False,dithering=False,
                limitations=['native plain strand is tiled rather than stretching the original 141px texture over arbitrary rope lengths',
                             'pieces are pre-turned in 11.25 degree steps (rope-links.png); not a full GPU triangle-strip rope renderer'])
    (output/'rope.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
