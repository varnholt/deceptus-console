"""Bake the source sword-ring shader's powered pose into native-size tile frames.

original counterparts: data/shaders/ring.frag and src/game/mechanisms/ringshaderlayer.cpp.
"""
import hashlib
import math
from pathlib import Path
import re

import numpy as np
from PIL import Image
from content import map_objects
from tiled import TiledMap

# source fragment equations are translated explicitly, rather than accepting unknown GLSL.
RING_FRAGMENT_SIGNATURE = '39ac6b4d79b0917a10580e49492992a0c9c82cb88a7e46977cb98e4b48bcafe4'


def shader_signature(source):
    source=re.sub(r'//[^\n]*','',source)
    source=re.sub(r'\s+','',source)
    return hashlib.sha256(source.encode('utf-8')).hexdigest()


def heartbeat(phase, second_beat=.7, width=.035):
    if width <= 0:
        return 0.0
    def beat(center):
        distance=abs((phase % 1.0)-center)
        distance=min(distance,1.0-distance)
        return math.exp(-.5*(distance/width)**2)
    return min(beat(0.0)+second_beat*beat(.16),1.0)


def repeated_noise(texture, x, y, smooth=True):
    """Match repeat wrapping and normalized OpenGL red-channel texture sampling."""
    height,width=texture.shape
    x=x*width-.5
    y=y*height-.5
    if not smooth:
        return texture[np.floor(y+.5).astype(np.int64)%height,np.floor(x+.5).astype(np.int64)%width]
    left=np.floor(x).astype(np.int64); top=np.floor(y).astype(np.int64)
    fx=x-left; fy=y-top
    return ((texture[top%height,left%width]*(1-fx)+texture[top%height,(left+1)%width]*fx)*(1-fy)+
            (texture[(top+1)%height,left%width]*(1-fx)+texture[(top+1)%height,(left+1)%width]*fx)*fy)


def render_powered_ring(obj, texture, world_bounds, elapsed, pulse, heartbeat_offset=0.0):
    """Evaluate the powered, untouched fragment equations before target conversion."""
    props=obj['properties']
    left,top,width,height=world_bounds
    yy,xx=np.mgrid[top:top+height,left:left+width].astype(np.float64)
    pixel_size=float(props.get('pixel_size',1.0))
    # the source quad flips the vertical UV; pixel centres are then floored by the shader.
    frag_x=xx+.5-obj['x']
    frag_y=obj['height']-(yy+.5-obj['y'])
    px=np.floor(frag_x/pixel_size)*pixel_size/obj['width']-.5
    py=np.floor(frag_y/pixel_size)*pixel_size/obj['height']-.5
    shader_time=elapsed+float(props.get('time_offset_s',0.0))+heartbeat_offset
    time=shader_time*.15
    px += math.sin(time*15.0)*(.6/obj['width'])
    px *= obj['width']/obj['height']
    scale=float(props['ring_scale'])*(1.0+(float(props.get('heartbeat_scale',1.1))-1.0)*pulse)
    px/=scale;py/=scale
    radius=np.hypot(px,py)
    normal=np.maximum(radius,1e-12)
    phases=np.mod(time*2.0+np.array([0.0,.25,.5,.75]),1.0)
    transition=1.0-np.abs(phases-.5)*2.0
    fbm=np.zeros_like(radius)
    smooth=props.get('smooth_texture',False)
    for offset,phase,weight in zip((0.0,1.0,2.0,3.0),phases,transition):
        x=offset+px-px/normal*phase
        y=offset+py-py/normal*phase
        total=np.zeros_like(radius)
        divisor=2.0
        for octave in range(3):
            noise=repeated_noise(texture,x*.01,y*.01,smooth)
            total+=np.abs(noise-.5)*2.0/divisor
            x*=2.0;y*=2.0;divisor*=2.0
        fbm+=total*weight*.25
    ox,oy=px/14.0,py/14.0
    angle=np.arctan2(oy,ox)
    distortion=np.sin(angle*6.0+time)*.005*math.sin(time*10.0)
    circular_radius=(np.hypot(ox,oy)+distortion)*5.0
    safe=np.where(np.abs(circular_radius)>1e-12,circular_radius,1e-12)
    effect=np.abs(5.0/safe-12.0)
    denominator=np.maximum(fbm*effect*effect*2.0,1e-12)
    base=np.array([.2,.1,.4])[None,None,:]/denominator[:,:,None]
    brightness=base[:,:,0]*.299+base[:,:,1]*.587+base[:,:,2]*.114
    alpha=np.clip(brightness-.05,0.0,1.0)
    alpha[(radius<.6)|(radius>2.2)]=0.0
    rgba=np.concatenate((np.clip(base,0.0,1.0),alpha[:,:,None]),axis=2)
    return Image.fromarray(np.rint(rgba*255).astype(np.uint8))


def bake_ring_shaders(data, recipe):
    data=Path(data)
    _,objects=map_objects(data/recipe['map'])
    result=[]
    for spec in recipe.get('ring_shaders',[]):
        obj=next(item for item in objects if item['id']==spec['source_id'] and item['group']=='shader_quads')
        props=obj['properties']
        shader=data.parent/props['fragment_shader']
        if props.get('customization')!='ring' or shader_signature(shader.read_text())!=RING_FRAGMENT_SIGNATURE:
            raise ValueError('sword ring shader changed or unsupported; review the explicit native translation')
        cpp=(data.parent/'src/game/mechanisms/ringshaderlayer.cpp').read_text()
        compact=re.sub(r'\s+','',cpp)
        for expression in ('heartbeat_second_beat_phase=0.16f;',
                           'returnstd::exp(-0.5f*normalized_distance*normalized_distance);',
                           'returnstd::min(first_beat+second_beat,1.0f);'):
            if expression not in compact:
                raise ValueError('source ring heartbeat changed; review the native translation')
        texture_path=data.parent/props['texture']
        texture=np.asarray(Image.open(texture_path).convert('RGB'),dtype=np.float64)[:,:,0]/255.0
        period=float(props.get('heartbeat_period_s',2.0))
        total_ticks=round(period*60)
        frame_ticks=int(spec.get('frame_ticks',5))
        if total_ticks<=0 or total_ticks%frame_ticks:
            raise ValueError('ring heartbeat must fit a whole number of native animation frames')
        center=[obj['x']+obj['width']/2,obj['y']+obj['height']/2]
        size=int(spec.get('size',128))
        if size%8:
            raise ValueError('ring clip must align to the native tile grid')
        world_bounds=[math.floor((center[0]-size/2)/8)*8,math.floor((center[1]-size/2)/8)*8,size,size]
        crop=recipe['crop_pixels']
        if not (crop[0]<=world_bounds[0] and crop[1]<=world_bounds[1] and
                world_bounds[0]+size<=crop[0]+crop[2] and world_bounds[1]+size<=crop[1]+crop[3]):
            raise ValueError('ring clip leaves its imported room')
        foreground=TiledMap(data/recipe['map']).render(world_bounds,recipe.get('foreground_layers',[]))
        frames=[];offset=0.0;pulses=[]
        for tick in range(total_ticks):
            phase=tick/total_ticks
            pulse=heartbeat(phase,float(props.get('heartbeat_second_beat',.7)),float(props.get('heartbeat_beat_width',.035)))
            if tick%frame_ticks==0:
                image=render_powered_ring(obj,texture,world_bounds,tick/60.0,pulse,offset)
                # source layers above z=20 retain their original foreground pixels and occlusion.
                image.alpha_composite(foreground)
                frames.append(image);pulses.append(pulse)
            offset+=pulse*float(props.get('heartbeat_turbulence',1.0))/60.0
        sources=[data/recipe['map'],shader,texture_path,
                 data.parent/'src/game/mechanisms/ringshaderlayer.cpp',data.parent/'src/game/mechanisms/ringshaderlayer.h',
                 data.parent/'src/game/mechanisms/shaderlayer.cpp',data.parent/'src/game/mechanisms/shaderlayer.h']
        result.append(dict(name=obj['name'],source_id=obj['id'],bounds=[world_bounds[0]-crop[0],world_bounds[1]-crop[1],size,size],
                           plane=1,required_flags=spec['required_flags'],forbidden_flags=0,
                           frames=frames,frame_count=len(frames),frame_ticks=frame_ticks,
                           source_center=center,source_resting_radius=(14.0/12.0)*float(props['ring_scale'])*obj['width'],
                           source_heartbeat_period_s=period,sampled_heartbeat=pulses,
                           source_hashes={path.relative_to(data.parent).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
                           limitations=['finite heartbeat loop resets on room entry; source noise time is sampled',
                                        'foreground alpha threshold and target palette replace continuous shader blending',
                                        '128px clip trims the faint outer glow; no pixel resampling or dithering',
                                        'player-responsive touch/recoil and expanding power-down dissolve remain unsupported']))
    return result
