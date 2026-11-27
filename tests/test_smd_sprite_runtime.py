"""Check actual Mega Drive quadrant packing against pixels and scanline limits."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SpriteRuntimeTests(unittest.TestCase):
    def test_quadrant_pixels_and_hardware_budgets(self):
        compiler = shutil.which('clang')
        if not compiler:
            self.skipTest('clang unavailable')
        folder = ROOT / 'build/tests/smd-sprite-native'
        folder.mkdir(parents=True, exist_ok=True)
        production = (ROOT / 'src/platform/smd/video/opening_sprites.h').read_text()
        # compile the actual packing seam without requiring generated game artwork.
        budget = production[production.index('static union'):production.index('static DeceptusU16 smd_obj_request_count')]
        packing = production[production.index('static void smdObjPiece('):production.index('static DeceptusU8 smdObjAppearance(')]
        fits = production[production.index('static DeceptusU8 smdObjBudgetFits('):production.index('static void smdSpritesPrepare(const')]
        source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "deceptus/types.h"
#define SPRITE_SIZE(w,h) (((w)<<4)|(h))
#define TILE_ATTR_FULL(p,z,v,f,t) ((t)|((f)<<11)|((p)<<13))
static unsigned char actual[224][320], expected[224][320];
static unsigned short smd_obj_count;
static unsigned char smd_sprite_overflow;
static unsigned source_quads;
static void smdObjHardware(DeceptusU16 n,DeceptusI16 x,DeceptusI16 y,DeceptusU8 size,DeceptusU16 attr)
{
 int dx,dy,sx,sy,px,py,quad;
 unsigned width=(size>>4)*8,height=(size&15)*8,tile=attr&2047;
 (void)n;
 for(dy=0;dy<(int)height;++dy) {
  for(dx=0;dx<(int)width;++dx) {
   sx=(attr&2048)?width-1-dx:dx;sy=dy;
   sx+=(tile/2)*8;sy+=(tile%2)*8;
   quad=(sx/8)+(sy/8)*2;
   px=x+dx;py=y+dy;
   if(px>=0&&px<320&&py>=0&&py<224&&(source_quads&(1U<<quad))) {
    actual[py][px]=(unsigned char)(1+sx+16*sy);
   }
  }
 }
}
BUDGET
PACKING
FITS
static void reset(int fast)
{
 memset(&smd_obj_budget,0,sizeof(smd_obj_budget));
 smd_obj_budget_top=0;smd_obj_budget_bottom=224;
 smd_obj_count=0;smd_sprite_overflow=0;smd_obj_fast_budget=(unsigned char)fast;
}
int main(void)
{
 int quads,flip,x,y,fast,whole,dx,dy,sx,sy,line,i,objects,slivers,valid;
 unsigned seed=7123,width,height;
 unsigned char lines[224],fetch[224];
 const int xs[]={-15,-8,0,307,319};
 const int ys[]={-15,-8,0,208,219};
 for(whole=0;whole<2;++whole) {
 for(fast=0;fast<2;++fast) {
  for(quads=1;quads<16;++quads) {
   for(flip=0;flip<2;++flip) {
    for(x=0;x<5;++x) {
     for(y=0;y<5;++y) {
      reset(fast);source_quads=(unsigned)quads;
      memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
      if(whole) {
       /* the whole-piece path the request emitter takes under the fast budget. */
       if(!fast||!(quads==15||((quads&5)&&(quads&10)))) {continue;}
       smdObjHardware(smd_obj_count++,(DeceptusI16)xs[x],(DeceptusI16)ys[y],SPRITE_SIZE(2,2),(DeceptusU16)TILE_ATTR_FULL(2,0,0,flip,0));
       smdObjBudgetAdd((DeceptusI16)ys[y],16,1,2);
       assert(smd_obj_count==1);
      }
      else if(fast&&!(quads==15||((quads&5)&&(quads&10)))) {
       /* the emitter's inline split of partial pieces under the fast budget. */
       smd_obj_count=smdObjSplit(0,(DeceptusI16)xs[x],(DeceptusI16)ys[y],(DeceptusU16)TILE_ATTR_FULL(2,0,0,flip,0),(DeceptusU16)quads);
      }
      else {
       smdObjQuads(xs[x],ys[y],0,2,(DeceptusU8)flip,(DeceptusU8)quads);
      }
      for(dy=0;dy<16;++dy) {
       for(dx=0;dx<16;++dx) {
        sx=flip?15-dx:dx;sy=dy;
        if(xs[x]+dx>=0&&xs[x]+dx<320&&ys[y]+dy>=0&&ys[y]+dy<224&&
           (quads&(1<<((sx/8)+(sy/8)*2)))) {
         expected[ys[y]+dy][xs[x]+dx]=(unsigned char)(1+sx+16*sy);
        }
       }
      }
      assert(memcmp(actual,expected,sizeof(actual))==0);
      assert(!smd_sprite_overflow);
      if(fast) {assert(smdObjBudgetFits());}
     }
    }
   }
  }
 }
 }
 source_quads=0;
 for(i=0;i<1000;++i) {
  reset(1);memset(lines,0,sizeof(lines));memset(fetch,0,sizeof(fetch));
  for(objects=0;objects<80;++objects) {
   seed=seed*1664525U+1013904223U;x=(int)((seed>>16)%350)-20;
   seed=seed*1664525U+1013904223U;y=(int)((seed>>16)%260)-20;
   width=(seed&1)?8:16;height=(seed&2)?8:16;
   smdObjPiece((DeceptusI16)x,(DeceptusI16)y,0,2,0,(DeceptusU8)width,(DeceptusU8)height);
   if(x<=-(int)width||x>=320||y<=-(int)height||y>=224) {continue;}
   for(line=y<0?0:y;line<y+(int)height&&line<224;++line) {
    ++lines[line];fetch[line]+=(unsigned char)(width/8);
   }
  }
  valid=1;objects=slivers=0;
  for(line=0;line<224;++line) {
   objects+=smd_obj_budget.ranges.objects[line];slivers+=smd_obj_budget.ranges.slivers[line];
   assert(objects==lines[line]&&slivers==fetch[line]);
   if(lines[line]>20||fetch[line]>40) {valid=0;}
  }
  assert(smdObjBudgetFits()==valid);
 }
 reset(1);
 for(i=0;i<20;++i) {smdObjPiece(0,0,0,2,0,16,16);}
 assert(smdObjBudgetFits());
 smdObjPiece(0,0,0,2,0,8,8);assert(!smdObjBudgetFits());
 /* whole pieces feed the same scanline budget as split ones, whether a row enters them at once
    or one by one. */
 reset(1);smd_obj_budget_top=224;smd_obj_budget_bottom=0;
 smdObjBudgetAdd(-4,16,12,24);
 for(i=0;i<8;++i) {smdObjBudgetAdd(-4,16,1,2);}
 assert(smdObjBudgetFits());
 smdObjBudgetAdd(8,16,1,2);assert(!smdObjBudgetFits());
 puts("1500 clipped quadrant images and1000 scanline budget scenes passed");
 return 0;
}
'''.replace('BUDGET', budget).replace('PACKING', packing).replace('FITS', fits)
        bridge = folder / 'sprites.c'
        bridge.write_text(source)
        binary = folder / 'sprites.exe'
        subprocess.run([compiler, '-O2', '-std=c90', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT / 'include'), str(bridge), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
