"""Convert the original opening to native Mega Drive graphics and portable metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

import numpy as np
from PIL import Image
from assets import ROOT
from palettes import banked_background
from pixel_font import NativePixelFont
from smd_assets import array,fit_rgb333_palette,packed_tile,palette_word,rgb333,sprite_chunks,words
from snes_tutorial_assets import original_imports,source_clips


def packbits(payload,unit_bytes=1):
    """Encode bounded literal/repeat packets in bytes or native4-byte cells."""
    if unit_bytes not in (1,4) or len(payload)%unit_bytes:
        raise ValueError('RLE input must contain complete1-byte or4-byte units')
    values=[payload[start:start+unit_bytes] for start in range(0,len(payload),unit_bytes)]
    output=bytearray();index=0;threshold=3 if unit_bytes==1 else 2
    while index<len(values):
        run=1
        while index+run<len(values) and values[index+run]==values[index] and run<128:
            run+=1
        if run>=threshold:
            output.append(127+run);output.extend(values[index]);index+=run
        else:
            start=index;index+=run
            while index<len(values) and index-start<128:
                run=1
                while index+run<len(values) and values[index+run]==values[index] and run<threshold:
                    run+=1
                if run>=threshold:
                    break
                index+=min(run,128-(index-start))
            output.append(index-start-1);output.extend(b''.join(values[start:index]))
    return bytes(output)


class Writer:
    def __init__(self,output):
        self.output=Path(output);self.assembly=['.section .rodata'];self.externs=[]
        self.raw_bytes=0;self.rle_bytes=0;self.zlib_bytes=0;self.offset_bytes=0

    def blob(self,name,payload,block_bytes,unit_bytes=1):
        if not block_bytes or len(payload)%block_bytes or len(payload)//block_bytes>65535:
            raise ValueError('invalid bounded Mega Drive ROM block layout')
        (self.output/(name+'.bin')).write_bytes(payload)
        packed=bytearray();offsets=[0]
        for start in range(0,len(payload),block_bytes):
            packed.extend(packbits(payload[start:start+block_bytes],unit_bytes));offsets.append(len(packed))
        offset_data=struct.pack('>'+str(len(offsets))+'I',*offsets)
        for suffix,data,kind in (('_rle',packed,'u8'),('_offsets',offset_data,'u32')):
            label=name+suffix;path=self.output/(label+'.bin');path.write_bytes(data)
            self.assembly+=['.balign 4','.global '+label,label+':','.incbin "'+path.as_posix()+'"']
            self.externs.append('extern const deceptus_'+kind+' '+label+'[];')
        self.raw_bytes+=len(payload);self.rle_bytes+=len(packed);self.offset_bytes+=len(offset_data)
        self.zlib_bytes+=len(zlib.compress(payload,9))
        return '{'+name+'_rle,'+name+'_offsets,'+','.join(map(str,[block_bytes,len(offsets)-1,unit_bytes]))+'}'

    def finish(self):
        (self.output/'data.s').write_text('\n'.join(self.assembly)+'\n')


def compile_background(image,foreground,clips,banks=2):
    height=image.height*2+sum(clip['bounds'][3]*clip['frame_count'] for clip in clips)
    atlas=Image.new('RGB',(image.width,height));atlas.paste(image,(0,0));atlas.paste(foreground.convert('RGB'),(0,image.height))
    cursor=image.height*2
    for clip in clips:
        clip['atlas_y']=cursor
        for frame in clip['frames']:
            atlas.paste(frame,(0,cursor));cursor+=frame.height
    converted,assignments,metrics=banked_background(rgb333(atlas),bank_count=banks)
    pixels=np.asarray(converted).copy()
    pixels[image.height:image.height*2][np.asarray(foreground)[:,:,3]<128]=0
    for clip in clips:
        if clip.get('plane',0):
            for index,frame in enumerate(clip['frames']):
                top=clip['atlas_y']+index*frame.height
                pixels[top:top+frame.height,:frame.width][np.asarray(frame.convert('RGBA'))[:,:,3]<128]=0
    patterns=[bytes(32)];lookup={patterns[0]:(0,0)}
    def region(top,width,height,plane):
        cells=[]
        for y in range(0,height,8):
            for x in range(0,width,8):
                tile=pixels[top+y:top+y+8,x:x+8];packed=packed_tile(tile)
                if packed not in lookup:
                    number=len(patterns);patterns.append(packed);lookup[packed]=(number,0)
                    for transformed,flag in ((tile[:,::-1],0x0800),(tile[::-1,:],0x1000),(tile[::-1,::-1],0x1800)):
                        lookup.setdefault(packed_tile(transformed),(number,flag))
                number,flags=lookup[packed]
                bank=assignments[((top+y)//8)*(image.width//8)+x//8]
                cells.extend((number,flags|(bank<<13)|(0x8000 if plane else 0)))
        return np.asarray(cells,dtype=np.uint16).reshape(height//8,width//8,2)
    background=region(0,image.width,image.height,0);front=region(image.height,image.width,image.height,1)
    animations=[region(clip['atlas_y'],clip['bounds'][2],clip['bounds'][3]*clip['frame_count'],clip.get('plane',0)) for clip in clips]
    palette=converted.getpalette();colors=[palette_word(*palette[index*3:index*3+3]) for index in range(banks*16)]
    preview=Image.fromarray(pixels[:image.height]);preview.putpalette(palette)
    return dict(patterns=b''.join(patterns),background=background,foreground=front,animations=animations,
                palette=colors,preview=preview,pattern_count=len(patterns),metrics=metrics)


def viewport_peak(background,foreground,width=41,height=29):
    rows,columns=background.shape[:2];maximum=(0,0,0)
    for top in range(max(1,rows-height+1)):
        for left in range(max(1,columns-width+1)):
            count=len(np.unique(np.concatenate((background[top:top+height,left:left+width,0].ravel(),
                                                foreground[top:top+height,left:left+width,0].ravel()))))
            if count>maximum[0]:
                maximum=(count,left,top)
    return dict(patterns=maximum[0],left=maximum[1],top=maximum[2],viewport=[width,height],includes_blank=True)


def portable_metadata(source,output):
    files=[]
    for filename in ['opening_area'+str(index)+'.c' for index in range(26)]+['opening_adventure.c']:
        text=(source/filename).read_text().replace('snes_','smd_').replace('SNES_','SMD_')
        if 'smd_sprite' in text or 'smd_stage' in text:
            raise ValueError('graphics declarations leaked into portable opening metadata')
        (output/filename).write_text(text);files.append(filename)
    return files


def source_sprites(data,output):
    from headtorch_assets import compile_headtorch
    from rope_assets import compile_rope
    compile_headtorch(data,output);compile_rope(data,output)
    records=json.loads((ROOT/'assets/generated/gba/area0/mechanisms-frames.json').read_text())
    images=[];cache={}
    for record in records:
        path=(output if record.get('source_generated') else data/'sprites')/record['source']
        if path not in cache:
            cache[path]=Image.open(path).convert('RGBA')
        x,y,width,height=record['source_rect'];image=cache[path].crop((x,y,x+width,y+height))
        if record.get('rotation'):
            image=image.transpose({90:Image.Transpose.ROTATE_90,180:Image.Transpose.ROTATE_180,270:Image.Transpose.ROTATE_270}[record['rotation']])
        images.append(image)
    animations=json.loads((data/'sprites/animations.json').read_text());player=Image.open(data/'sprites/player.png').convert('RGBA')
    selection=[('player_idle_r',i) for i in range(8)]+[('player_run_r',i) for i in range(8)]+[('player_jump_up_r',0),('player_jump_down_r',0),(None,0)]+[('player_bend_down_r',i) for i in range(7)]
    normal=[]
    for name,index in selection:
        if name is None:
            normal.append(Image.new('RGBA',(32,48)));continue
        animation=animations[name];x,y=animation['frame_offset'];width,height=animation['frame_size'];ox,oy=animation['origin']
        left=round(x+index*width+ox-16);top=round(y+oy-48)
        normal.append(player.crop((left,top,left+32,top+48)))
    return normal+images,[dict(source='player.png',trim_offset=[0,0]) for _ in normal]+records


def compile_sprites(data,output,writer):
    images,records=source_sprites(data,output)
    actor_images=[]
    for image,record in zip(images,records):
        if record['source']=='player.png':
            pixels=np.asarray(image).copy();black=np.all(((pixels[:,:,:3].astype(np.uint16)*7+127)//255)==0,axis=2)
            pixels[black,3]=0;actor_images.append(Image.fromarray(pixels))
    palettes={2:np.concatenate((np.zeros((1,3),dtype=np.int16),fit_rgb333_palette(actor_images,13))),
              3:fit_rgb333_palette([image for image,record in zip(images,records) if record['source']!='player.png'],15)}
    lines=[]
    for bank,palette in palettes.items():
        colors=[0]+[int(r)<<1|int(g)<<5|int(b)<<9 for r,g,b in palette]
        if bank==2:
            colors.append(0x0eee)
        lines.append(array('smd_sprite_palette'+str(bank),colors,'u16',16))
    descriptors=[];quads=bytearray();offsets=[];masks=[];dedup={}
    for image,record in zip(images,records):
        bank=2 if record['source']=='player.png' else 3
        packed,columns,rows,occupied=sprite_chunks(image,palettes[bank]);digest=hashlib.sha256(packed).hexdigest()
        if digest not in dedup:
            dedup[digest]=writer.blob('smd_sprite_gfx'+str(len(dedup)),packed,128)
        trim=record.get('trim_offset',[0,0]);name=dedup[digest]
        descriptors.append('{'+name+','+','.join(map(str,[len(packed),columns,rows,*trim,bank]))+'}')
        offsets.append(len(quads));quads.extend(occupied);mask=bytearray(8)
        for index,value in enumerate(occupied):
            if value:
                mask[index//8]|=1<<(index%8)
        masks.append(mask)
    lines+=['const smd_sprite_resource smd_sprite_resources[1065] = {',*['    '+value+',' for value in descriptors],'};',
            'const deceptus_u8 smd_sprite_opaque[1065][8] = {',*['{'+','.join(map(str,mask))+'},' for mask in masks],'};',
            array('smd_sprite_quad_offsets',offsets,'u16',16),array('smd_sprite_quads',quads,'u8',32)]
    return lines,dict(resources=len(images),unique_graphics=len(dedup),pieces=len(quads),actor_colors=13,actor_black_slot=33,common_colors=15,ui_white_slot=47)


def compile_font(data,writer):
    font=NativePixelFont(data/'fonts');payload=bytearray(bytes([0x11])*32);glyphs=[]
    for code in range(32,127):
        source=font.glyph(chr(code));bounds=source.getbbox();width=bounds[2]-bounds[0] if bounds else 1
        if bounds and (bounds[1]<3 or bounds[3]>17):
            raise ValueError('native font ink no longer fits the common source rows3..16')
        columns=max(1,(width+7)//8);image=Image.new('L',(columns*8,16),1)
        if bounds:
            image.paste(15,(0,0,width,14),source.getchannel('A').crop((bounds[0],3,bounds[2],17)))
        glyphs.append((len(payload)//32,columns))
        for top in range(0,16,8):
            for left in range(0,columns*8,8):
                payload.extend(packed_tile(image.crop((left,top,left+8,top+8))))
    blob=writer.blob('smd_font',payload,32)
    return ['const smd_rom_blob smd_font = '+blob+';',
            'const smd_font_glyph smd_font_glyphs[95] = {'+','.join('{'+str(tile)+','+str(columns)+'}' for tile,columns in glyphs)+'};'],len(payload)//32


def compile_opening(data,output,metadata_source,rooms=26):
    data,output,metadata_source=Path(data).resolve(),Path(output).resolve(),Path(metadata_source).resolve()
    output.mkdir(parents=True,exist_ok=True);recipes,identity=original_imports(data)
    files=portable_metadata(metadata_source,output);writer=Writer(output);stages=[];lines=[];reports=[]
    for index,(recipe,source) in enumerate(recipes[:rooms]):
        image=Image.open(source/'catacombs.png').convert('RGB');front=Image.open(source/'foreground.png').convert('RGBA')
        clips=source_clips(data,recipe,image);result=compile_background(image,front,clips)
        prefix='smd_area'+str(index);result['preview'].save(output/(prefix+'-preview.png'))
        patterns=writer.blob(prefix+'_patterns',result['patterns'],32)
        bg=writer.blob(prefix+'_background',result['background'].astype('>u2').tobytes(),image.width//8*4,4)
        fg=writer.blob(prefix+'_foreground',result['foreground'].astype('>u2').tobytes(),image.width//8*4,4)
        lines.append(array(prefix+'_palette',result['palette'],'u16',16));animations=[]
        for clip_index,(clip,cells) in enumerate(zip(clips,result['animations'])):
            name=prefix+'_animation'+str(clip_index);blob=writer.blob(name,cells.astype('>u2').tobytes(),clip['bounds'][2]//8*4,4)
            duration='0'
            if clip.get('frame_durations'):
                duration=name+'_durations';lines.append(array(duration,clip['frame_durations'],'u16',32))
            x,y,width,height=clip['bounds']
            animations.append('{'+','.join(map(str,[x//8,y//8,width//8,height//8,clip['frame_count'],clip['frame_ticks'],clip.get('required_flags',0),clip.get('forbidden_flags',0)]))+','+duration+','+blob+','+str(clip.get('plane',0))+'}')
        pointer='0'
        if animations:
            pointer=prefix+'_animations';lines+=['static const smd_background_animation '+pointer+'[] = {',*['    '+value+',' for value in animations],'};']
        stages.append('{'+patterns+','+bg+','+fg+','+prefix+'_palette,'+','.join(map(str,[image.width//8,image.height//8,result['pattern_count']]))+','+pointer+','+str(len(animations))+','+json.dumps(recipe['title'])+'}')
        peak=viewport_peak(result['background'],result['foreground'])
        reports.append(dict(room=index,patterns=result['pattern_count'],raw_pattern_bytes=len(result['patterns']),static_peak=peak,color_fidelity=result['metrics'],animation_frames=[clip['frame_count'] for clip in clips]))
        print(f'MD room{index}: {result["pattern_count"]}patterns,staticviewport{peak["patterns"]}',flush=True)
    sprite_lines,sprite_report=compile_sprites(data,output,writer);font_lines,font_tiles=compile_font(data,writer)
    header=['#ifndef SMD_ASSETS_H','#define SMD_ASSETS_H','#include "deceptus/adventure.h"',
            '#define SMD_OPENING 1',f'#define SMD_STAGE_COUNT {rooms}','#define SMD_SPRITE_RESOURCE_COUNT 1065',
            '#define SMD_ACTOR_FIRST 0','#define SMD_SPRITE_QUADS 1','#define SMD_SPRITE_OPAQUE_MASK 1',f'#define SMD_FONT_TILES {font_tiles}',
            '#define SMD_FONT_ROWS 2','#define SMD_FONT_SOURCE_TOP 3','#define SMD_ROM_BLOB_RLE 1',
            'typedef struct smd_rom_blob { const deceptus_u8 *data; const deceptus_u32 *offsets; deceptus_u16 block_bytes,blocks; deceptus_u8 unit_bytes; } smd_rom_blob;',
            'typedef struct smd_background_animation { deceptus_u16 left,top,columns,rows,frames,frame_ticks,required_flags,forbidden_flags; const deceptus_u16 *durations; smd_rom_blob cells; deceptus_u8 plane; } smd_background_animation;',
            'typedef struct smd_stage { smd_rom_blob patterns,background,foreground; const deceptus_u16 *palette; deceptus_u16 columns,rows,pattern_count; const smd_background_animation *animations; deceptus_u8 animation_count; const char *title; } smd_stage;',
            'typedef struct smd_sprite_resource { smd_rom_blob graphics; deceptus_u16 bytes; deceptus_u8 columns,rows,trim_x,trim_y,palette_bank; } smd_sprite_resource;',
            'typedef struct smd_font_glyph { deceptus_u16 tile; deceptus_u8 columns; } smd_font_glyph;',
            'extern const smd_stage smd_stages[SMD_STAGE_COUNT];','extern const smd_sprite_resource smd_sprite_resources[1065];',
            'extern const deceptus_u8 smd_sprite_opaque[1065][8],smd_sprite_quads[];','extern const deceptus_u16 smd_sprite_quad_offsets[1065],smd_sprite_palette2[16],smd_sprite_palette3[16];',
            'extern const smd_rom_blob smd_font;','extern const smd_font_glyph smd_font_glyphs[95];',
            'extern const deceptus_campaign_def smd_campaign_definition;','extern const deceptus_adventure_def smd_tutorial_definition;']
    for name,value in re.findall(r'#define (GBA_\w+_FIRST) (\d+)',(ROOT/'assets/generated/gba/gba_assets.h').read_text()):
        header.append('#define '+name.replace('GBA_','SMD_')+' '+str(int(value)+26))
    (output/'smd_assets.h').write_text('\n'.join(header+['#endif'])+'\n')
    for stem in ('sword','headtorch','rope'):
        path=output/f'gba_{stem}_resources.h'
        if not path.exists():
            path=ROOT/'assets/generated/gba/area0'/f'gba_{stem}_resources.h'
        (output/f'smd_{stem}_resources.h').write_text(path.read_text().replace('gba_','smd_').replace('GBA_','SMD_'))
    resources='#include "smd_assets.h"\n'+'\n'.join(writer.externs+lines+sprite_lines+font_lines+['const smd_stage smd_stages[SMD_STAGE_COUNT] = {',*['    '+stage+',' for stage in stages],'};'])
    (output/'opening_resources.c').write_text(resources);writer.finish()
    report=dict(full_game=False,graphics_rooms=rooms,adventure_rooms=26,source_identity=identity,
                converter_sha256=hashlib.sha256(Path(__file__).read_bytes()+Path(__file__).with_name('smd_assets.py').read_bytes()).hexdigest(),
                metadata_source=str(metadata_source),metadata_sha256={name:hashlib.sha256((metadata_source/name).read_bytes()).hexdigest() for name in files},
                c_sources=files+['opening_resources.c'],binary_bytes=writer.raw_bytes,rle_bytes=writer.rle_bytes,rle_offset_bytes=writer.offset_bytes,rom_graphics_bytes=writer.rle_bytes+writer.offset_bytes,zlib_reference_bytes=writer.zlib_bytes,
                sprite_metadata=sprite_report,rooms=reports,pixel_scale='1:1',dithering=False,
                limitations=['static viewport audit excludes animated frames until full animation audit',
                             'actor palette black plus13source colors plus dedicatedwhite; other sprites share15opaque colors',
                             '4MiB fit includes compressed graphics only until code/link metadata are measured'])
    (output/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ('binary_bytes','rom_graphics_bytes','zlib_reference_bytes')},indent=2),flush=True)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data',type=Path,default=ROOT.parent/'deceptus_engine/data')
    parser.add_argument('--output',type=Path,default=ROOT/'build/smd-opening-assets')
    parser.add_argument('--metadata-source',type=Path,default=ROOT/'assets/generated/snes')
    parser.add_argument('--rooms',type=int,default=26)
    args=parser.parse_args()
    if not 1<=args.rooms<=26:
        parser.error('--rooms must be1..26')
    compile_opening(args.data,args.output,args.metadata_source,args.rooms)
