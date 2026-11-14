"""Build the curated, four-area GBA preview from authoritative source content."""
import argparse
import json
from pathlib import Path
import re
import struct
import hashlib
from PIL import Image, ImageDraw, ImageFont, __version__ as pillow_version
import numpy as np
import gba_assets
from assets import ROOT


def font_tiles(data, output):
    font = ImageFont.truetype(str(data / 'fonts/3x5px.ttf'), 5)
    tiles = bytearray(32)
    for code in range(32, 128):
        image = Image.new('L', (8, 8))
        ImageDraw.Draw(image).text((2, 1), chr(code), font=font, fill=255)
        pixels = [1 if value >= 128 else 0 for value in image.getdata()]
        tiles.extend(bytes(pixels[index] | (pixels[index + 1] << 4) for index in range(0, 64, 2)))
    (output / 'font.bin').write_bytes(tiles)


def asset_identity(data):
    """Hash the original inputs and converter code before opting into room reuse."""
    data = Path(data)
    files = []
    for name in ('level-catacombs','level-graveyard','sprites','scripts','fonts','config','effects','weapons','shaders','light'):
        files += sorted(path for path in (data/name).rglob('*') if path.is_file())
    digest = hashlib.sha256()
    for path in files:
        digest.update(path.relative_to(data).as_posix().encode())
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    for name in ('watersurface.cpp','watersurface.h','ringshaderlayer.cpp','ringshaderlayer.h','shaderlayer.cpp','shaderlayer.h'):
        path=data.parent/'src/game/mechanisms'/name
        if path.exists():
            digest.update(name.encode())
            digest.update(path.read_bytes())
    for relative in ('src/game/items/itemheadtorch.cpp','src/game/items/itemheadtorch.h',
                     'src/game/player/playereyepositions.cpp','src/game/player/playereyepositions.h',
                     'src/game/effects/lightsystem.cpp','src/game/effects/lightsystem.h',
                     'src/game/mechanisms/rope.cpp','src/game/mechanisms/rope.h',
                     'src/game/mechanisms/grabrope.cpp','src/game/mechanisms/grabrope.h',
                     'src/game/player/playerrope.cpp','src/game/player/playerrope.h',
                     'src/game/player/playerropehold.cpp','src/game/player/playerropehold.h',
                     'src/game/player/playeranimation.cpp','src/game/player/playeranimation.h',
                     'src/game/level/tmxenemy.cpp','src/game/level/tmxenemy.h','src/game/level/level.cpp'):
        path=data.parent/relative
        if path.exists():
            digest.update(relative.encode())
            digest.update(path.read_bytes())
    source_digest = digest.hexdigest()
    digest = hashlib.sha256()
    digest.update((pillow_version+np.__version__).encode())
    for name in ('assets.py','gba_assets.py','campaign_assets.py','tiled.py','palettes.py',
                 'world_export.py','content.py','lua_source.py','enemies.py','scenery.py','tileanimation.py','water.py','ringshader.py','headtorch_assets.py','rope_assets.py','enemy_paths.py'):
        digest.update(name.encode())
        digest.update((ROOT/'tools'/name).read_bytes())
    return dict(source_sha256=source_digest,converters_sha256=digest.hexdigest())



ROOM_OUTPUTS = ('room.c','gba_assets.h','data.s','budget.json','background.pal','stream-patterns.bin','stream-cells.bin',
                'animation-cells.bin','animation-durations.bin','foreground-cells.bin',
                'gba_sword_resources.h','gba_headtorch_resources.h','gba_rope_resources.h','mechanisms.bin','mechanisms.pal','mechanisms.tbl','mechanisms-frames.json')


def validate_room_assets(folder, expected):
    manifest_path=folder/'asset-inputs.json'
    if not manifest_path.exists() or json.loads(manifest_path.read_text()) != expected:
        raise ValueError(f'{folder.name}: room assets cannot be reused after input changes; run a full conversion')
    for filename in ROOM_OUTPUTS:
        if not (folder/filename).is_file():
            raise ValueError(f'{folder.name}: missing converted asset {filename}; run a full conversion')


def compile_campaign(data, config=None, scene='tonight', reuse_assets=False):
    config = config or json.loads((ROOT / 'assets/campaigns/tonight.json').read_text())
    output = ROOT / 'assets/generated/gba'
    output.mkdir(parents=True, exist_ok=True)
    common_source = ROOT / 'assets/source/campaign'
    identity = asset_identity(data)
    assemblies, room_sources, stages, goals, reports = [], [], [], [], []
    header = None
    for index, stage in enumerate(config['stages']):
        folder = output / f'area{index}'
        folder.mkdir(parents=True, exist_ok=True)
        recipe = stage.get('room_recipe') or json.loads((ROOT / 'assets/rooms' / stage['recipe']).read_text())
        recipe.update(stream_background=True, max_health=100)
        recipe_path = folder / 'recipe.json'
        manifest_path = folder / 'asset-inputs.json'
        expected = dict(identity,recipe=recipe)
        if reuse_assets:
            validate_room_assets(folder,expected)
        else:
            manifest_path.unlink(missing_ok=True)
        recipe_path.write_text(json.dumps(recipe, indent=2) + '\n')
        gba_assets.OUTPUT = folder
        gba_assets.SOURCE = common_source / f'area{index}'
        if not reuse_assets:
            gba_assets.compile_assets(data, stage.get('recipe', scene), recipe_path)
            manifest_path.write_text(json.dumps(expected,sort_keys=True,indent=2)+'\n')
        report = json.loads((folder / 'budget.json').read_text())
        reports.append(report)
        prefix = f'gba_area{index}'
        for suffix, filename in [('palette', 'background.pal'), ('patterns', 'stream-patterns.bin'), ('cells', 'stream-cells.bin'), ('animation_cells','animation-cells.bin'), ('foreground_cells','foreground-cells.bin'),('animation_durations','animation-durations.bin')]:
            label = prefix + '_' + suffix
            assemblies += ['.balign 4', f'.global {label}', f'{label}:', f'.incbin "{(folder / filename).as_posix()}"']
        source = (folder / 'room.c').read_text()
        source = re.sub(r'\broom_', f'area{index}_room_', source)
        source = source.replace('deceptus_scene', f'deceptus_area{index}')
        source = source.replace('gba_background_animations', prefix + '_animations').replace('gba_animation_cells', prefix + '_animation_cells')
        source = source.replace('gba_animation_durations',prefix+'_animation_durations')
        source = '#include "gba_assets.h"\nextern const deceptus_u8 ' + prefix + '_animation_cells[], ' + prefix + '_animation_durations[];\n' + source
        room_sources.append(source)
        animation_count = len(report.get('baked_background_animations', []))
        animation_pointer = prefix + '_animations' if animation_count else '0'
        stages.append(f'    {{&deceptus_area{index}, {prefix}_palette, {prefix}_patterns, {prefix}_cells, {report["background_patterns"]}, {json.dumps(stage["title"])}, {json.dumps(stage["instruction"])}, {json.dumps(stage["hint1"])}, {json.dumps(stage["hint2"])}, {animation_pointer}, {animation_count}, {prefix}_foreground_cells}},')
        goals.append('    {{' + ','.join(map(str, stage['goal'])) + '}, ' + str(stage.get('disabled_mechanism', 0)) + '},')
        if index == 0:
            header = (folder / 'gba_assets.h').read_text()
            common_assembly = (folder / 'data.s').read_text()
            # room-specific background resources have their own labels above.
            common_assembly = common_assembly[:common_assembly.index('.balign 4\n.global gba_stream_patterns')]
    maximum = max(report['background_patterns'] for report in reports)
    header = header.replace('#define GBA_CAMPAIGN 0', '#define GBA_CAMPAIGN 1')
    header = header.replace('#define GBA_TUTORIAL 0', f'#define GBA_TUTORIAL {int(scene == "tutorial")}')
    header = re.sub(r'#define GBA_BACKGROUND_ANIMATION_COUNT \d+', '#define GBA_BACKGROUND_ANIMATION_COUNT (active_stage->animation_count)', header)
    header = re.sub(r'#define GBA_BACKGROUND_ANIMATION_CAPACITY \d+', '#define GBA_BACKGROUND_ANIMATION_CAPACITY 4', header)
    header = header.replace('extern const gba_background_animation gba_background_animations[];', '#define gba_background_animations (active_stage->animations)')
    header = re.sub(r'#define GBA_PATTERN_COUNT \d+', f'#define GBA_PATTERN_COUNT {maximum}', header)
    header = re.sub(r'#define GBA_ROOM_COLUMNS \d+', '#define GBA_ROOM_COLUMNS (active_stage->room->columns)', header)
    header = re.sub(r'#define GBA_ROOM_ROWS \d+', '#define GBA_ROOM_ROWS (active_stage->room->rows)', header)
    header = header.replace('extern const deceptus_u8 gba_stream_patterns[];', '').replace('extern const deceptus_u8 gba_stream_cells[];', '')
    header = header.replace('extern const deceptus_u8 gba_foreground_cells[];', '#define gba_foreground_cells (active_stage->foreground_cells)')
    additions = ['#include "deceptus/campaign.h"',
        'typedef struct gba_stage { const deceptus_room *room; const deceptus_u8 *palette, *patterns, *cells; deceptus_u16 pattern_count; const char *title, *instruction, *hint1, *hint2; const gba_background_animation *animations; deceptus_u8 animation_count; const deceptus_u8 *foreground_cells; } gba_stage;',
        f'extern const gba_stage gba_stages[{len(stages)}];', 'extern const deceptus_campaign_def gba_campaign_definition;',
        'extern const deceptus_u8 gba_font[];', '#define GBA_FONT_BYTES 3104',
        '#define gba_stream_patterns (active_stage->patterns)', '#define gba_stream_cells (active_stage->cells)']
    header = header.replace('#endif', '\n'.join(additions) + '\n#endif')
    (output / 'gba_assets.h').write_text(header)
    font_tiles(data, output)
    assemblies += ['.balign 4', '.global gba_font', 'gba_font:', f'.incbin "{(output / "font.bin").as_posix()}"']
    (output / 'data.s').write_text(common_assembly + '\n'.join(assemblies) + '\n')
    declarations = ['#include "gba_assets.h"']
    for index in range(len(stages)):
        declarations += [f'extern const deceptus_u8 gba_area{index}_{suffix}[];' for suffix in ('palette', 'patterns', 'cells','foreground_cells')]
    declarations += ['const gba_stage gba_stages[] = {', *stages, '};',
                     'static const deceptus_room *const campaign_rooms[] = {' + ','.join(f'&deceptus_area{index}' for index in range(len(stages))) + '};',
                     'static const deceptus_campaign_goal campaign_goals[] = {', *goals, '};',
                     f'const deceptus_campaign_def gba_campaign_definition = {{campaign_rooms, campaign_goals, {len(stages)}}};']
    (output / 'room.c').write_text('\n'.join(room_sources + declarations) + '\n')
    (output / 'budget.json').write_text(json.dumps(dict(scene=scene, stages=reports, full_game=False), indent=2) + '\n')
    print(f'Built {len(stages)}-room {scene} assets')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=ROOT.parent / 'deceptus_engine/data')
    args = parser.parse_args()
    compile_campaign(args.data)
