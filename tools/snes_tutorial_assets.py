"""Build native-size SNES opening assets from validated lossless source imports.

The shared simulation tables may be reused; target-reduced GBA pixels are never read.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

import numpy as np
from PIL import Image
from assets import ROOT, planar4
from campaign_assets import asset_identity, validate_room_assets
from palettes import banked_background, palette_rgb555
from pixel_font import NativePixelFont
from world_export import export_world

CHUNK_BYTES=32768


def planar2(tile):
    values=np.asarray(tile,dtype=np.uint8)
    result=bytearray()
    for row in values:
        for plane in range(2):
            result.append(sum(((int(value)>>plane)&1)<<(7-index) for index,value in enumerate(row)))
    return bytes(result)


class BinaryWriter:
    def __init__(self,output):
        self.output=Path(output)
        self.sections=['.include "hdr.asm"']
        self.externs=[]
        self.tables=[]
        self.total=0

    def binary(self,label,payload):
        path=self.output/(label+'.bin');path.write_bytes(payload)
        self.sections += [f'.section "{label}_section" superfree',label+':',f'.incbin "{path.as_posix()}"','.ends']
        self.externs.append('extern const deceptus_u8 '+label+'[];')
        self.total+=len(payload)
        return label

    def blob(self,label,payload):
        labels=[]
        for index,start in enumerate(range(0,len(payload),CHUNK_BYTES)):
            labels.append(self.binary(label+'_'+str(index),payload[start:start+CHUNK_BYTES]))
        if not labels:
            labels.append(self.binary(label+'_0',bytes(4)))
        name=label+'_chunks'
        self.tables.append('static const deceptus_u8 *const '+name+'[] = {'+','.join(labels)+'};')
        return '{'+name+','+str(len(payload))+'}'

    def finish(self):
        (self.output/'data.asm').write_text('\n'.join(self.sections)+'\n')


def compile_background(image,foreground,clips,output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    height=image.height*2+sum(clip['bounds'][3]*clip['frame_count'] for clip in clips)
    atlas=Image.new('RGB',(image.width,height))
    atlas.paste(image,(0,0));atlas.paste(foreground.convert('RGB'),(0,image.height))
    cursor=image.height*2
    for clip in clips:
        clip['atlas_y']=cursor
        for frame in clip['frames']:
            atlas.paste(frame,(0,cursor));cursor+=frame.height
    converted,banks,metrics=banked_background(atlas,bank_count=7)
    pixels=np.asarray(converted).copy()
    foreground_mask=np.asarray(foreground)[:,:,3]>=128
    pixels[image.height:image.height*2][~foreground_mask]=0
    for clip in clips:
        if clip.get('plane',0):
            for index,frame in enumerate(clip['frames']):
                top=clip['atlas_y']+index*frame.height
                mask=np.asarray(frame.convert('RGBA'))[:,:,3]>=128
                pixels[top:top+frame.height,:frame.width][~mask]=0
    palette=converted.getpalette()
    indexed=Image.fromarray(pixels);indexed.putpalette(palette)
    indexed.crop((0,0,image.width,image.height)).save(output/'background-preview.png')
    patterns=[bytes(32)];lookup={patterns[0]:0}
    def region(top,width,height,plane):
        entries=[]
        for y in range(0,height,8):
            for x in range(0,width,8):
                tile=indexed.crop((x,top+y,x+8,top+y+8))
                packed=planar4(tile)
                if packed not in lookup:
                    number=len(patterns);patterns.append(packed);lookup[packed]=number
                    for transform,flag in ((Image.Transpose.FLIP_LEFT_RIGHT,0x4000),(Image.Transpose.FLIP_TOP_BOTTOM,0x8000),(Image.Transpose.ROTATE_180,0xc000)):
                        lookup.setdefault(planar4(tile.transpose(transform)),number|flag)
                value=lookup[packed]
                bank=banks[((top+y)//8)*(image.width//8)+x//8]+1
                entries += [value&0x3fff,(value&0xc000)|(bank<<10)|(0x2000 if plane else 0)]
        return struct.pack('<'+str(len(entries))+'H',*entries)
    background=region(0,image.width,image.height,0)
    front=region(image.height,image.width,image.height,1)
    animated=[]
    for clip in clips:
        animated.append(region(clip['atlas_y'],clip['bounds'][2],clip['bounds'][3]*clip['frame_count'],clip.get('plane',0)))
    if len(patterns)>16384:
        raise ValueError('SNES room exceeds the 14-bit logical pattern identity budget')
    palette_image=Image.new('P',(8,8));palette_image.putpalette(palette)
    palette_bytes=bytes(32)+palette_rgb555(palette_image,7)
    return dict(patterns=b''.join(patterns),background=background,foreground=front,animations=animated,
                palette=palette_bytes,pattern_count=len(patterns),metrics=metrics)


def compile_font(data,output,writer):
    font=NativePixelFont(Path(data)/'fonts')
    graphics=bytearray(bytes(16)+bytes([0,255]*8))
    glyphs=[]
    for code in range(32,127):
        source=font.glyph(chr(code));bounds=source.getbbox()
        width=(bounds[2]-bounds[0]) if bounds else 1
        columns=max(1,(width+7)//8)
        image=Image.new('L',(columns*8,24),2)
        if bounds:
            alpha=source.getchannel('A').crop((bounds[0],0,bounds[2],18))
            image.paste(1,(0,0,width,18),alpha)
        first=128+len(graphics)//16
        for row in range(3):
            for column in range(columns):
                graphics.extend(planar2(image.crop((column*8,row*8,column*8+8,row*8+8))))
        glyphs.append((first,columns))
    if len(graphics)>6144:
        raise ValueError('native SNES font exceeds its 6KiB VRAM budget')
    writer.binary('snes_font',graphics)
    lines=['const snes_font_glyph snes_font_glyphs[95] = {']
    lines+=['    {'+str(tile)+','+str(columns)+'},' for tile,columns in glyphs]
    lines+=['};','const deceptus_u16 snes_ui_palette[16] = {0,32767,4226,1599,0,0,0,0,0,0,0,0,0,0,0,0};']
    return lines,len(graphics)


def source_clips(data,recipe,image):
    result=[]
    if recipe.get('bake_water'):
        from water import bake_room
        result+=bake_room(data,recipe,image)
    if recipe.get('scenery_variants'):
        from scenery import variants
        result+=variants(data,recipe,image)
    if recipe.get('tile_animations'):
        from tileanimation import bake_tile_animations
        result+=bake_tile_animations(data,recipe)
    if recipe.get('ring_shaders'):
        from ringshader import bake_ring_shaders
        result+=bake_ring_shaders(data,recipe)
    return result


def original_imports(data):
    identity=asset_identity(data)
    cache=ROOT/'assets/generated/gba'
    recipes=[]
    for index in range(26):
        folder=cache/f'area{index}'
        recipe=json.loads((folder/'recipe.json').read_text())
        validate_room_assets(folder,dict(identity,recipe=recipe))
        source=ROOT/'assets/source/campaign'/f'area{index}'/'gba'
        for name in ('import.json','catacombs.png','foreground.png','collision.png','one_way.png','water.png','mechanisms.json','enemies.json'):
            if not (source/name).is_file():
                raise ValueError(f'missing lossless original import: {source/name}')
        recipes.append((recipe,source))
    for name in ('room.c','gba_assets.h'):
        if not (cache/name).is_file():
            raise ValueError(f'missing portable opening metadata: {cache/name}')
    return recipes,identity


def ensure_original_imports(data,refresh_metadata=False):
    """Bootstrap actual source imports before either target starts reading them."""
    data=Path(data).resolve()
    try:
        result=original_imports(data)
    except (OSError,ValueError,KeyError) as error:
        print(f'opening imports need conversion: {error}',flush=True)
    else:
        if not refresh_metadata:
            return result
        from tutorial_assets import compile_tutorial as compile_original_opening
        compile_original_opening(data,reuse_assets=True)
        return original_imports(data)
    # this existing packer also emits the lossless imports and source frame metadata.
    # no GBA reduced image is used by the SNES converter, and no ROM is built here.
    from tutorial_assets import compile_tutorial as compile_original_opening
    compile_original_opening(data,reuse_assets=False)
    return original_imports(data)


def compile_worlds(recipes,output):
    files=[]
    for index,(recipe,source) in enumerate(recipes):
        folder=output/f'area{index}';folder.mkdir(parents=True,exist_ok=True)
        export_world(source,folder,recipe)
        text=(folder/'room.c').read_text().replace('deceptus_scene',f'deceptus_snes_area{index}')
        filename=f'opening_area{index}.c';(output/filename).write_text(text);files.append(filename)
    # only portable simulation declarations are reused from the validated opening binding output.
    source=(ROOT/'assets/generated/gba/room.c').read_text()
    campaign=source[source.index('static const deceptus_room *const campaign_rooms'):source.index('#include "gba_assets.h"',source.index('const deceptus_campaign_def gba_campaign_definition'))]
    start=source.index('static const char *const tutorial_pages_0_0')
    end=source.index('const gba_ui_resource gba_ui_resources',start)
    adventure=source[start:end]
    text='#include "snes_assets.h"\n'
    text+='\n'.join(f'extern const deceptus_room deceptus_snes_area{index};' for index in range(26))+'\n'
    text+=campaign+adventure
    text=re.sub(r'\bdeceptus_area(\d+)\b',r'deceptus_snes_area\1',text)
    text=text.replace('gba_campaign_definition','snes_campaign_definition').replace('gba_tutorial_definition','snes_tutorial_definition')
    if 'gba_' in text:
        raise ValueError('target graphics leaked into portable SNES simulation bindings')
    (output/'opening_adventure.c').write_text(text);files.append('opening_adventure.c')
    return files


def resource_family(index):
    boundaries=[0,33,73,105,257,327,371,387,413,474,494,638,724,728,842,942,944,945,953,1016,1024,1033,1039]
    return next(position for position in range(len(boundaries)-1) if boundaries[position]<=index<boundaries[position+1])


def fit_sprite_palette(images):
    samples=[np.asarray(image)[:,:,:3][np.asarray(image)[:,:,3]>=128] for image in images]
    samples=[sample for sample in samples if len(sample)]
    if not samples:
        return np.zeros((15,3),dtype=np.int16)
    pixels=np.concatenate(samples)
    colors=np.unique(pixels>>3,axis=0)
    if len(colors)>15:
        sample=Image.fromarray(pixels.reshape(1,-1,3))
        reduced=sample.quantize(colors=15,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE).convert('RGB')
        colors=np.unique(np.asarray(reduced).reshape(-1,3)>>3,axis=0)
    colors=colors.tolist()
    return np.asarray(colors+[colors[-1]]*(15-len(colors)),dtype=np.int16)


def sprite_planar(image,palette):
    padded=Image.new('RGBA',(((image.width+15)//16)*16,((image.height+15)//16)*16))
    padded.paste(image,(0,0))
    pixels=np.asarray(padded);opaque=pixels[:,:,3]>=128
    indices=np.zeros(opaque.shape,dtype=np.uint8)
    values=(pixels[opaque,:3]>>3).astype(np.int16)
    if len(values):
        indices[opaque]=1+((values[:,None,:]-palette[None,:,:])**2).sum(axis=2).argmin(axis=1)
    indexed=Image.fromarray(indices)
    packed=bytearray()
    for y in range(0,padded.height,16):
        for x in range(0,padded.width,16):
            for dy in (0,8):
                for dx in (0,8):
                    packed.extend(planar4(indexed.crop((x+dx,y+dy,x+dx+8,y+dy+8))))
    return bytes(packed),padded.width//16,padded.height//16


def sprite_opaque_mask(packed):
    if len(packed)%128 or len(packed)>64*128:
        raise ValueError('SNES sprite occupancy requires at most64 complete16x16 pieces')
    mask=bytearray(8)
    for piece,start in enumerate(range(0,len(packed),128)):
        if any(packed[start:start+128]):
            mask[piece//8]|=1<<(piece%8)
    return bytes(mask)


def sprite_quads(packed):
    if len(packed)%128 or len(packed)>64*128:
        raise ValueError('SNES sprite quadrants require at most64 complete16x16 pieces')
    return bytes(sum((1<<quadrant) for quadrant in range(4)
                     if any(packed[start+quadrant*32:start+(quadrant+1)*32]))
                 for start in range(0,len(packed),128))


def compile_sprites(data,output,writer):
    from headtorch_assets import compile_headtorch
    from rope_assets import compile_rope
    compile_headtorch(data,output);compile_rope(data,output)
    records=json.loads((ROOT/'assets/generated/gba/area0/mechanisms-frames.json').read_text())
    cache={};images=[]
    for record in records:
        path=(output if record.get('source_generated') else data/'sprites')/record['source']
        if str(path) not in cache:
            cache[str(path)]=Image.open(path).convert('RGBA')
        x,y,width,height=record['source_rect']
        image=cache[str(path)].crop((x,y,x+width,y+height))
        if record.get('rotation'):
            image=image.transpose({90:Image.Transpose.ROTATE_90,180:Image.Transpose.ROTATE_180,270:Image.Transpose.ROTATE_270}[record['rotation']])
        images.append(image)
    groups={}
    for index,image in enumerate(images):
        groups.setdefault(resource_family(index),[]).append(image)
    palette_names={};palettes={}
    lines=[]
    for family,family_images in groups.items():
        palette=fit_sprite_palette(family_images);palettes[family]=palette
        name=f'snes_sprite_palette{family}';palette_names[family]=name
        values=[0]+[int(r)|(int(g)<<5)|(int(b)<<10) for r,g,b in palette]
        lines.append('static const deceptus_u16 '+name+'[16] = {'+','.join(map(str,values))+'};')
    payloads={};resources=[];shared_masks=[];shared_quads=[]
    for index,(image,record) in enumerate(zip(images,records)):
        family=resource_family(index)
        packed,columns,rows=sprite_planar(image,palettes[family])
        shared_masks.append(sprite_opaque_mask(packed))
        shared_quads.append(sprite_quads(packed))
        digest=hashlib.sha256(packed).hexdigest()
        if digest not in payloads:
            payloads[digest]=writer.binary('snes_sprite_gfx'+str(len(payloads)),packed)
        trim=record.get('trim_offset',[0,0])
        resources.append('{'+','.join(map(str,[payloads[digest],palette_names[family],len(packed),columns,rows,*trim]))+'}')
    shared_resources=resources
    # normal player frames use their original authored source rectangles, not the prototype portal art.
    animations=json.loads((data/'sprites/animations.json').read_text());player=Image.open(data/'sprites/player.png').convert('RGBA')
    selection=[('player_idle_r',i) for i in range(8)]+[('player_run_r',i) for i in range(8)]+[('player_jump_up_r',0),('player_jump_down_r',0),(None,0)]+[('player_bend_down_r',i) for i in range(7)]
    normal=[]
    for name,index in selection:
        if name is None:
            normal.append(Image.new('RGBA',(32,48)));continue
        animation=animations[name];x,y=animation['frame_offset'];width,height=animation['frame_size'];ox,oy=animation['origin']
        left=round(x+index*width+ox-16);top=round(y+oy-48)
        normal.append(player.crop((left,top,left+32,top+48)))
    palette=fit_sprite_palette(normal);values=[0]+[int(r)|(int(g)<<5)|(int(b)<<10) for r,g,b in palette]
    lines+=['static const deceptus_u16 snes_actor_palette[16] = {'+','.join(map(str,values))+'};']
    actor_resources=[];actor_masks=[];actor_quads=[]
    for index,image in enumerate(normal):
        packed,columns,rows=sprite_planar(image,palette)
        actor_masks.append(sprite_opaque_mask(packed))
        actor_quads.append(sprite_quads(packed))
        name=writer.binary('snes_actor_gfx'+str(index),packed)
        actor_resources.append('{'+name+',snes_actor_palette,'+','.join(map(str,[len(packed),columns,rows,0,0]))+'}')
    lines+=['const snes_sprite_resource snes_sprite_resources[1065] = {',*['    '+value+',' for value in actor_resources+shared_resources],'};']
    lines+=['const deceptus_u8 snes_sprite_opaque[1065][8] = {',
            *['    {'+','.join(map(str,mask))+'},' for mask in actor_masks+shared_masks],'};']
    offsets=[];quads=bytearray()
    for resource in actor_quads+shared_quads:
        offsets.append(len(quads));quads.extend(resource)
    if len(quads)>65535:
        raise ValueError('SNES sprite quadrant offsets exceed the16-bit ROM table budget')
    lines+=['const deceptus_u16 snes_sprite_quad_offsets[1065] = {'+','.join(map(str,offsets))+'};',
            'const deceptus_u8 snes_sprite_quads['+str(len(quads))+'] = {'+','.join(map(str,quads))+'};']
    return lines,dict(logical_resources=len(records),unique_graphics=len(payloads),normal_actor_frames=26,
                      source='original source atlases and lossless generated light/rope effects',dithering=False,resampling=False,
                      limitation='unused prototype portal actor frame18 is transparent; original opening does not use it')


def compile_tutorial(data,output,rooms=26,sprites=True):
    data,output=Path(data).resolve(),Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    recipes,identity=ensure_original_imports(data)
    writer=BinaryWriter(output)
    worlds=compile_worlds(recipes,output)
    stage_lines=[];animation_lines=[];reports=[]
    for index,(recipe,source) in enumerate(recipes[:rooms]):
        image=Image.open(source/'catacombs.png').convert('RGB')
        foreground=Image.open(source/'foreground.png').convert('RGBA') if (source/'foreground.png').exists() else Image.new('RGBA',image.size)
        clips=source_clips(data,recipe,image)
        converted=compile_background(image,foreground,clips,output/f'area{index}')
        prefix=f'snes_area{index}'
        patterns=writer.blob(prefix+'_patterns',converted['patterns'])
        background=writer.blob(prefix+'_background',converted['background'])
        front=writer.blob(prefix+'_foreground',converted['foreground'])
        palette=writer.binary(prefix+'_palette',converted['palette'])
        animations=[]
        for clip_index,(clip,cells) in enumerate(zip(clips,converted['animations'])):
            name=prefix+'_animation'+str(clip_index)
            blob=writer.blob(name+'_cells',cells)
            durations=clip.get('frame_durations',[])
            duration_name='0'
            if durations:
                duration_name=name+'_durations'
                animation_lines.append('static const deceptus_u16 '+duration_name+'[] = {'+','.join(map(str,durations))+'};')
            x,y,width,height=clip['bounds']
            animations.append('{'+','.join(map(str,[x//8,y//8,width//8,height//8,clip['frame_count'],clip['frame_ticks'],clip.get('required_flags',0),clip.get('forbidden_flags',0)]))+','+duration_name+','+blob+','+str(clip.get('plane',0))+'}')
        pointer='0'
        if animations:
            pointer=prefix+'_animations'
            animation_lines+=['static const snes_background_animation '+pointer+'[] = {',*['    '+value+',' for value in animations],'};']
        stage_lines.append('    {'+','.join([patterns,background,front,palette,str(image.width//8),str(image.height//8),str(converted['pattern_count']),pointer,str(len(animations)),json.dumps(recipe['title'])])+'},')
        reports.append(dict(room=index,patterns=converted['pattern_count'],animations=len(clips),world_palette_banks=7,color_fidelity=converted['metrics']))
        print(f'converted SNES room{index}: {converted["pattern_count"]}patterns',flush=True)
    font_lines,font_bytes=compile_font(data,output,writer)
    sprite_lines,sprite_report=compile_sprites(data,output,writer) if sprites else ([],dict(logical_resources=0))
    for stem in ('sword','headtorch','rope'):
        path=output/f'gba_{stem}_resources.h'
        if not path.exists():
            path=ROOT/'assets/generated/gba/area0'/f'gba_{stem}_resources.h'
        text=path.read_text().replace('gba_','snes_').replace('GBA_','SNES_')
        (output/f'snes_{stem}_resources.h').write_text(text)
    header=['#ifndef SNES_ASSETS_H','#define SNES_ASSETS_H','#include "deceptus/adventure.h"',
            '#define DECEPTUS_SNES_OPENING 1',f'#define SNES_STAGE_COUNT {rooms}',f'#define SNES_FONT_BYTES {font_bytes}',
            '#define SNES_FONT_TILE_BASE 128','#define SNES_SPRITE_COUNT 1039','#define SNES_ACTOR_COUNT 26',
            '#define SNES_SPRITE_RESOURCE_COUNT 1065','#define SNES_ACTOR_FIRST 0',
            '#define SNES_SPRITE_OPAQUE_MASK 1',
            '#define SNES_SPRITE_QUADS 1',
            'typedef struct snes_rom_blob { const deceptus_u8 *const *chunks; deceptus_u32 bytes; } snes_rom_blob;',
            'typedef struct snes_background_animation { deceptus_u16 left,top,columns,rows,frames,frame_ticks,required_flags,forbidden_flags; const deceptus_u16 *durations; snes_rom_blob cells; deceptus_u8 plane; } snes_background_animation;',
            'typedef struct snes_stage { snes_rom_blob patterns,background,foreground; const deceptus_u8 *palette; deceptus_u16 columns,rows,pattern_count; const snes_background_animation *animations; deceptus_u8 animation_count; const char *title; } snes_stage;',
            'typedef struct snes_sprite_resource { const deceptus_u8 *graphics; const deceptus_u16 *palette; deceptus_u16 bytes; deceptus_u8 columns,rows,trim_x,trim_y; } snes_sprite_resource;',
            'typedef struct snes_font_glyph { deceptus_u16 tile; deceptus_u8 columns; } snes_font_glyph;',
            'extern const snes_stage snes_stages[SNES_STAGE_COUNT];','extern const snes_sprite_resource snes_sprite_resources[SNES_SPRITE_RESOURCE_COUNT];',
            'extern const deceptus_u8 snes_sprite_opaque[1065][8];',
            'extern const deceptus_u16 snes_sprite_quad_offsets[1065];','extern const deceptus_u8 snes_sprite_quads[];',
            '#define snes_actors (snes_sprite_resources)','#define snes_sprites (snes_sprite_resources+26)',
            'extern const snes_font_glyph snes_font_glyphs[95];','extern const deceptus_u8 snes_font[];','extern const deceptus_u16 snes_ui_palette[16];',
            'extern const deceptus_campaign_def snes_campaign_definition;','extern const deceptus_adventure_def snes_tutorial_definition;']
    # logical resource family prefixes are shared, never their target pixel encodings.
    gba_header=(ROOT/'assets/generated/gba/gba_assets.h').read_text()
    for name,value in re.findall(r'#define (GBA_\w+_FIRST) (\d+)',gba_header):
        header.append('#define '+name.replace('GBA_','SNES_')+' '+str(int(value)+26))
    header+=['#endif'];(output/'snes_assets.h').write_text('\n'.join(header)+'\n')
    resource='#include "snes_assets.h"\n'+'\n'.join(writer.externs+writer.tables+animation_lines+font_lines+sprite_lines+['const snes_stage snes_stages[SNES_STAGE_COUNT] = {',*stage_lines,'};'])+'\n'
    (output/'opening_resources.c').write_text(resource)
    writer.finish()
    report=dict(full_game=False,rooms=reports,adventure_rooms=26,graphics_rooms=rooms,sprites=sprite_report,
                binary_bytes=writer.total,source_identity=identity,
                snes_converter_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                target_palette_banks=8,world_palette_banks=7,
                pixel_scale='1:1',dithering=False,used_reduced_gba_images=False,
                c_sources=worlds+['opening_resources.c'],
                limitations=['lossless source imports bootstrap automatically when absent or stale; target-reduced GBA image data is never read',
                             'Mode1 world color conversion uses7banks withUIbank0 reserved',
                             'graphics rooms beyond graphics_rooms are unavailable in partial bootstrap builds'])
    (output/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'SNES assets ready: {rooms}graphics rooms, {writer.total}binarybytes',flush=True)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data',type=Path,default=ROOT.parent/'deceptus_engine/data')
    parser.add_argument('--output',type=Path,default=ROOT/'assets/generated/snes')
    parser.add_argument('--rooms',type=int,default=26)
    parser.add_argument('--no-sprites',action='store_true')
    args=parser.parse_args()
    if not 1<=args.rooms<=26:
        parser.error('--rooms must be1..26')
    compile_tutorial(args.data,args.output,args.rooms,not args.no_sprites)
