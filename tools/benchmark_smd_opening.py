"""Profile warmed stationary Mega Drive fixtures without changing production sources."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct

import build_smd
from emulator import Emulator, ROOT
from verify_smd_opening import smd_state

FIXTURES={
    'cell':(0,370,312,''),
    'crypt':(25,597,264,'adventure.flags=7327;adventure.inventory.counts[3]=1;adventure.inventory.counts[5]=1;'),
    'loop':(9,370,696,'adventure.flags=31;'),
    'altar':(7,536,312,'adventure.flags=8095;adventure.inventory.counts[5]=1;adventure.inventory.counts[6]=1;adventure.has_sword=1;'
             '{deceptus_i16 fixture_cage;fixture_cage=deceptus_mechanism_find(&adventure.campaign.engine.mechanisms,1386);'
             'if(fixture_cage>=0){adventure.campaign.engine.mechanisms.states[fixture_cage].enabled=0;}}'),
}
PHASES={0:'initialization',1:'gameplay/input',2:'shared render',3:'background prepare',4:'sprite prepare/measure',
        5:'UI prepare',6:'debug publication',7:'wait VBlank',8:'graphics preload',9:'background commit',
        10:'sprite commit',11:'UI commit',12:'loop overhead/display enable',13:'room load'}


def replace_once(code,marker,replacement):
    if code.count(marker)!=1:
        raise ValueError('update benchmark source hook: '+marker)
    return code.replace(marker,replacement)


def fixture_source(code,name):
    room,x,y,progress=FIXTURES[name]
    code=replace_once(code,'static deceptus_adventure adventure;',
                      'volatile deceptus_u16 smd_profile_phase;\nstatic deceptus_adventure adventure;')
    marker='deceptus_adventure_init(&adventure, &smd_tutorial_definition, &video);'
    setup=(f'adventure.campaign.room_index={room};'
           f'deceptus_init(&adventure.campaign.engine,smd_campaign_definition.rooms[{room}],&video);'
           f'deceptus_place_player(&adventure.campaign.engine,{x},{y});'
           'adventure.campaign.mode=DECEPTUS_CAMPAIGN_PLAYING;'+progress)
    code=replace_once(code,marker,marker+setup)
    render='deceptus_render(&adventure.campaign.engine, &frame);'
    code=replace_once(code,render,f'deceptus_place_player(&adventure.campaign.engine,{x},{y});\n'
                      'smd_profile_phase=2;'+render)
    hooks=[('buttons = input_read();',1),('smd_background_set_flags(flags);',3),
           ('smd_sprites_prepare(&adventure, &frame);',4),('smd_ui_prepare(&adventure);',5),
           ('debug_write(buttons, blank);',6),('smd_sprites_preload();',8),
           ('smd_background_commit();',9),('smd_sprites_commit();',10),('smd_ui_commit();',11),
           ('VDP_setEnable(TRUE);',12)]
    for marker,phase in hooks:
        code=replace_once(code,marker,'smd_profile_phase='+str(phase)+';'+marker)
    # both the ordinary wait and optional blanking wait share a sampling bucket.
    if code.count('SYS_doVBlankProcess();')!=2:
        raise ValueError('update benchmark VBlank hooks')
    code=code.replace('SYS_doVBlankProcess();','smd_profile_phase=7;SYS_doVBlankProcess();smd_profile_phase=12;')
    code=code.replace('video_load();','smd_profile_phase=13;video_load();smd_profile_phase=12;')
    return code


def phase_address(symbols):
    match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+_?smd_profile_phase$',symbols,re.MULTILINE)
    if not match:
        raise ValueError('instrumented phase symbol missing from linked ROM')
    address=int(match.group(1),16)
    if address&0xff0000!=0xff0000:
        raise ValueError('phase symbol is not in Mega Drive work RAM')
    return address&65535


def build_fixture(name,assets,sdk,folder):
    folder.mkdir(parents=True,exist_ok=True)
    source=ROOT/'src/platform/smd/opening.c';scratch=folder/'opening.c'
    original=source.read_text();scratch.write_text(fixture_source(original,name))
    original_run=build_smd.run
    def run(command,working_directory):
        if '-c' in command and source in command:
            command=[scratch if argument==source else argument for argument in command]
            command.append('-I'+str(source.parent))
        original_run(command,working_directory)
    build_smd.run=run
    try:
        build_smd.build(sdk,ROOT.parent/'deceptus_engine/data',folder,scene='tutorial',assets_dir=assets,reuse_assets=True)
    finally:
        build_smd.run=original_run
    if source.read_text()!=original:
        raise RuntimeError('production source changed during controlled build; rerun after source freeze')
    return phase_address((folder/'deceptus-tutorial.sym').read_text()),hashlib.sha256(original.encode()).hexdigest()


def packet(emulator):
    state=smd_state(emulator)
    words=struct.unpack_from('<56H',emulator.memory(),state['mailbox_offset'])
    state.update(background_dma_bytes=words[23],background_uploads=words[25],vtimer=words[26],
                 free_memory=words[37],ui_dirty=words[47],background_full=words[48])
    return state


def assert_valid(state,room):
    if state['room']!=room or state['mode']!=1:
        raise AssertionError(('stationary fixture changed room/mode',state))
    for key in ('content_error','background_error','graphics_error','sprite_overflow','ui_overflow'):
        if state[key]:
            raise AssertionError((key,state))


def benchmark(name,assets,sdk,core,output,warmup=3000,sample=1200):
    folder=ROOT/'build/tests/smd-benchmark'/name
    offset,source_sha=build_fixture(name,assets,sdk,folder)
    rom=folder/'deceptus-tutorial.bin';emulator=Emulator(core,rom)
    phases=Counter();loads=Counter();dma=Counter();uploads=Counter();publications=Counter();last=None
    try:
        emulator.core.retro_set_controller_port_device(0,513)
        emulator.run(warmup)
        initial=packet(emulator);assert_valid(initial,FIXTURES[name][0]);last=initial
        for _ in range(sample):
            emulator.run(1);memory=emulator.memory()
            phase=struct.unpack_from('<H',memory,offset)[0];phases[PHASES.get(phase,'unknown:'+str(phase))]+=1
            current=packet(emulator);assert_valid(current,FIXTURES[name][0])
            if current['sequence']!=last['sequence']:
                loads[current['cpu_load']]+=1;dma[current['background_dma_bytes']]+=1;uploads[current['sprite_upload_bytes']]+=1
                publications[current['ui_dirty']]+=1
            last=current
        ticks=(last['ticks']-initial['ticks'])&65535
        updates=((last['sequence']-initial['sequence'])&65535)//2
        output.mkdir(parents=True,exist_ok=True);emulator.screenshot(output/(name+'.png'))
        result=dict(name=name,scope='compiled stationary fixture; warmed display-boundary phase samples, not cycle-accurate profiling or route performance',
                    source_sha256=source_sha,rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),emulator=emulator.version,
                    warmup_display_frames=warmup,sample_display_frames=sample,simulation_ticks=ticks,debug_publications=updates,
                    simulation_fps_at_60hz=60*ticks/sample,phase_word_ram_offset=offset,
                    phase_samples=dict(phases.most_common()),phase_percent={key:round(100*value/sample,2) for key,value in phases.most_common()},
                    cpu_load_by_publication=dict(sorted(loads.items())),background_dma_bytes_by_publication=dict(sorted(dma.items())),
                    sprite_upload_bytes_by_publication=dict(sorted(uploads.items())),ui_dirty_by_publication=dict(sorted(publications.items())),
                    initial=initial,final=last,production_source_unmodified=True)
        (output/(name+'.json')).write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2),flush=True)
        return result
    except Exception:
        output.mkdir(parents=True,exist_ok=True);emulator.screenshot(output/(name+'-failure.png'))
        (output/(name+'-failure-memory.bin')).write_bytes(emulator.memory())
        raise
    finally:
        emulator.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',choices=['all',*FIXTURES],default='all')
    parser.add_argument('--assets-dir',type=Path,default=ROOT/'assets/generated/smd-opening')
    parser.add_argument('--sdk',type=Path,default=ROOT/'.tools/sgdk')
    parser.add_argument('--core',type=Path,default=ROOT/'.tools/genesis_plus_gx/genesis_plus_gx_libretro.dll')
    parser.add_argument('--output-dir',type=Path,default=ROOT/'artifacts/smd-benchmark')
    parser.add_argument('--warmup',type=int,default=3000)
    parser.add_argument('--sample',type=int,default=1200)
    args=parser.parse_args()
    if args.warmup<3000 or args.sample<1200:
        parser.error('use at least3000 warmup and1200 sampled displayframes')
    names=FIXTURES if args.fixture=='all' else [args.fixture]
    results=[benchmark(name,args.assets_dir.resolve(),args.sdk.resolve(),args.core.resolve(),args.output_dir.resolve(),args.warmup,args.sample) for name in names]
    args.output_dir.mkdir(parents=True,exist_ok=True)
    (args.output_dir/'report.json').write_text(json.dumps(dict(scope='controlled warm stationary Mega Drive profiles',fixtures=results),indent=2)+'\n')
