"""Compile original helmet attachments and a bounded source headtorch light effect."""
import hashlib
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image

NORMAL_FRAMES = ([('player_idle',index) for index in range(8)]+
                 [('player_run',index) for index in range(8)]+
                 [('player_jump_up',0),('player_jump_down',0),(None,0)]+
                 [('player_bend_down',index) for index in range(7)])


def helmet_pose(animations, eyes, animation_name, frame, side):
    """Return helmet top-left relative to the same feet anchor used to draw the body."""
    if animation_name is None:
        return [0,0,side,0]
    animation=animations[animation_name]
    positions=eyes.get(animation_name,{})
    xs,ys=positions.get('eye_positions_x',[]),positions.get('eye_positions_y',[])
    count=min(len(xs),len(ys))
    if not count:
        return [0,0,side,0]
    # PlayerEyePositions clamps to its final valid sample when a cycle has more frames.
    index=min(frame,count-1)
    eye_offset=(-14 if side==0 else -9,-14)
    return [round(xs[index]-animation['origin'][0]+eye_offset[0]),
            round(ys[index]-animation['origin'][1]+eye_offset[1]),side,1]


def compile_headtorch(data, output, sword_clips=None):
    data,output=Path(data),Path(output)
    output.mkdir(parents=True,exist_ok=True)
    if sword_clips is None:
        from gba_assets import SWORD_CLIPS
        sword_clips=SWORD_CLIPS
    source=(data.parent/'src/game/items/itemheadtorch.cpp').read_text()
    compact=re.sub(r'\s+','',source)
    for expression in ('REFERENCE_CYCLE_ORIGIN_PX=sf::Vector2f{36.0f,48.0f};',
                       'helmet_eye_offset_px{pointing_right?-14.0f:-9.0f,-14.0f};',
                       'body_offset_px=sf::Vector2f{0.0f,8.0f};'):
        if expression not in compact:
            raise ValueError('source headtorch attachment changed; review the native offsets')
    animations=json.loads((data/'sprites/animations.json').read_text())
    eyes=json.loads((data/'sprites/eye_positions.json').read_text())
    config=json.loads((data/'config/player_headtorch.json').read_text())
    normal=[];swim=[];sword=[];rope=[];sources=[]
    for side,suffix in enumerate(('_r','_l')):
        normal.append([helmet_pose(animations,eyes,name+suffix if name else None,index,side)
                       for name,index in NORMAL_FRAMES])
        for index in range(13):
            name='player_swim_idle' if index<5 else 'player_swim'
            swim.append(helmet_pose(animations,eyes,name+suffix,index if index<5 else index-5,side))
        rope += [helmet_pose(animations,eyes,'player_jump_midair'+suffix,index,side) for index in range(3)]
        for name in sword_clips:
            animation_name=name+suffix
            for index in range(animations[animation_name]['sprite_count']):
                sword.append(helmet_pose(animations,eyes,animation_name,index,side))
                sources.append(dict(animation=animation_name,source_frame=index))
    if any(not pose[3] for pose in swim+sword):
        raise ValueError('headtorch eye metadata is missing for a displayed player cycle')
    right=config['right']
    texture_path=data.parent/right['texture']
    # this is the explicitly bounded light-effect approximation, not world or character art.
    mask=Image.open(texture_path).convert('RGB').getchannel('R').resize((64,32),Image.Resampling.BILINEAR)
    intensity=np.asarray(mask,dtype=np.float64)/255.0
    levels=np.rint(intensity*float(right.get('color_a',255))/255.0*15).astype(np.uint8)
    color=np.array([right['color_r'],right['color_g'],right['color_b']],dtype=np.float64)
    rgb=np.rint(levels[:,:,None]/15.0*color).astype(np.uint8)
    rgba=np.concatenate((rgb,np.where(levels>0,255,0).astype(np.uint8)[:,:,None]),axis=2)
    beam=Image.fromarray(rgba)
    beam.save(output/'headtorch-beam.png')
    lamp_offsets=[]
    for side,key in enumerate(('right','left')):
        entry=config[key]
        # source LightInstance centers its texture; ItemHeadTorch rotates around the lamp end.
        anchor_shift=(entry['offset_x_px']+(-entry['width_px']/2 if side==0 else entry['width_px']/2),
                      entry['offset_y_px'])
        helmet_eye_offset=(-14 if side==0 else -9,-14)
        lamp_offsets.append([round(36+anchor_shift[0]-helmet_eye_offset[0]),
                             round(48+anchor_shift[1]-8-helmet_eye_offset[1])])
    lines=['/* source eye positions/origins; helmet offsets are relative to the drawn player feet. */',
           '#ifndef GBA_HEADTORCH_RESOURCES_H','#define GBA_HEADTORCH_RESOURCES_H',
           '#include "deceptus/itemheadtorch.h"',
           'typedef DeceptusHeadtorchPose GbaHeadtorchPose;',
           'static const GbaHeadtorchPose gba_headtorch_normal[2][26] = {']
    for poses in normal:
        lines+=['    {']+['        {'+','.join(map(str,pose))+'},' for pose in poses]+['    },']
    lines+=['};','static const GbaHeadtorchPose gba_headtorch_swim[26] = {']
    lines+=['    {'+','.join(map(str,pose))+'},' for pose in swim]
    lines+=['};','static const GbaHeadtorchPose gba_headtorch_sword['+str(len(sword))+'] = {']
    lines+=['    {'+','.join(map(str,pose))+'},' for pose in sword]
    lines+=['};','static const GbaHeadtorchPose gba_headtorch_rope[6] = {']
    lines+=['    {'+','.join(map(str,pose))+'},' for pose in rope]
    lines+=['};','static const DeceptusI16 gba_headtorch_lamp_offsets[2][2] = {']
    lines+=['    {'+','.join(map(str,value))+'},' for value in lamp_offsets]
    lines+=['};','#endif']
    (output/'gba_headtorch_resources.h').write_text('\n'.join(lines)+'\n')
    paths=[data/'sprites/player.png',data/'sprites/animations.json',data/'sprites/eye_positions.json',
           data/'config/player_headtorch.json',texture_path,data.parent/'src/game/items/itemheadtorch.cpp',
           data.parent/'src/game/player/playereyepositions.cpp']
    report=dict(helmet_source_rects=[[0,1776,24,24],[24,1776,24,24]],normal=normal,swim=swim,sword=sword,rope=rope,
                sword_source_frames=sources,lamp_offsets=lamp_offsets,beam_size=[64,32],beam_levels=16,
                configured_beam_size=[right['width_px'],right['height_px']],configured_color=color.astype(int).tolist(),
                source_hashes={path.relative_to(data.parent).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
                limitations=['original helmet artwork stays at 1:1 pixels',
                             'source spotlight is reduced to a bounded 64x32 sprite and 16 intensity levels; no dithering',
                             'no shadow casting, dust, jitter, landing tilt or physical illumination of background palettes'])
    (output/'headtorch.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
