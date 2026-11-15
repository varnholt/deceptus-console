"""Exercise the real cache code with native memory and video-register stubs."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BackgroundRuntimeTests(unittest.TestCase):
    def test_streaming_retains_patterns_and_reference_counts(self):
        compiler = shutil.which('clang')
        if not compiler:
            self.skipTest('clang unavailable')
        output = ROOT / 'build/tests/snes-background-native'
        output.mkdir(parents=True, exist_ok=True)
        (output / 'snes.h').write_text('''
typedef unsigned char u8;
typedef struct {union {void *p;struct {unsigned short addr;unsigned char bank;} c;} mem;} dmaMemory;
static unsigned short reg;
#define REG_VMAIN reg
#define REG_VMADDLH reg
#define REG_DMAP0 reg
#define REG_BBAD0 reg
#define REG_A1T0LH reg
#define REG_A1B0 reg
#define REG_DAS0LH reg
#define REG_MDMAEN reg
#define SC_64x32 1
static void bgSetGfxPtr(int a,int b) {(void)a;(void)b;}
static void bgSetMapPtr(int a,int b,int c) {(void)a;(void)b;(void)c;}
static void bgSetScroll(int a,int b,int c) {(void)a;(void)b;(void)c;}
static void dmaCopyCGram(u8*a,int b,int c) {(void)a;(void)b;(void)c;}
static void dmaCopyVram(u8*a,int b,int c) {(void)a;(void)b;(void)c;}
''')
        (output / 'snes_assets.h').write_text('''
#include "deceptus/types.h"
typedef struct {const deceptus_u8 *const *chunks;deceptus_u32 bytes;} snes_rom_blob;
typedef struct {deceptus_u16 left,top,columns,rows,frames,frame_ticks,required_flags,forbidden_flags;const deceptus_u16*durations;snes_rom_blob cells;deceptus_u8 plane;} snes_background_animation;
typedef struct {snes_rom_blob patterns,background,foreground;const deceptus_u8*palette;deceptus_u16 columns,rows,pattern_count;const snes_background_animation*animations;deceptus_u8 animation_count;const char*title;} snes_stage;
#define SNES_STAGE_COUNT 1
#define SNES_FONT_BYTES 4592
extern const snes_stage snes_stages[1];
''')
        source = r'''
#include <assert.h>
#include <stdio.h>
#include "BACKGROUND_SOURCE"
void snes_background_run_dma(const snes_background_dma_command *commands,deceptus_u16 count) {
 unsigned i;
 assert(count<=SNES_BG_DMA_COMMANDS);
 for(i=0;i<count;++i) {
  assert(commands[i].bytes>0);
  assert(commands[i].increment==0x80||commands[i].increment==0x81);
  assert(commands[i].target<0x1000||(commands[i].target>=0x1d00&&commands[i].target<0x6000));
 }
}
deceptus_u8 snes_background_storage[32768];
static deceptus_u16 bg[120*90*2],fg[120*90*2];
static deceptus_u8 patterns[1600*32],palette[256];
static const deceptus_u8 *const bg_chunks[]={(deceptus_u8*)bg,(deceptus_u8*)bg+32768};
static const deceptus_u8 *const fg_chunks[]={(deceptus_u8*)fg,(deceptus_u8*)fg+32768};
static const deceptus_u8 *const pattern_chunks[]={patterns,patterns+32768};
static deceptus_u16 clip_cells[16];
static const deceptus_u8 *const clip_chunks[]={(deceptus_u8*)clip_cells};
static const snes_background_animation clips[]={{10,10,2,2,2,2,1,0,0,{clip_chunks,sizeof(clip_cells)},1}};
const snes_stage snes_stages[]={{{pattern_chunks,sizeof(patterns)},{bg_chunks,sizeof(bg)},{fg_chunks,sizeof(fg)},palette,120,90,1600,clips,1,"test"}};
static void check(int cx,int cy) {
 int x,y,p;unsigned slot,total,expected,actual[1072]={0};
 snes_background_prepare((deceptus_i16)(cx*8),(deceptus_i16)(cy*8));
 assert(!snes_background_error);
 for(p=0;p<2;++p)for(y=cy;y<cy+29;++y)for(x=cx;x<cx+33;++x) {
  slot=tile_slot((deceptus_u8)p,work->maps[p][map_address((deceptus_i16)x,(deceptus_i16)y)]);
  if(x<0||y<0||x>=120||y>=90) {assert(slot==0);continue;}
  expected=(p?fg:bg)[(y*120+x)*2];
  if(p==1&&(source_flags&1)&&x>=10&&x<12&&y>=10&&y<12) {expected=690+work->clip_frame[0];}
  assert(work->resident[slot]==expected);
  ++actual[slot];
 }
 total=0;
 for(slot=1;slot<1072;++slot) {assert(actual[slot]==work->references[slot]);total+=actual[slot];}
 assert(total<=2*33*29);
 snes_background_commit();assert(!snes_background_full);
}
int main(void) {
 int x,y;unsigned a,b,scratch,key;
 for(x=0;x<8;++x) {clip_cells[x*2]=(deceptus_u16)(690+x/4);}
 for(y=0;y<90;++y)for(x=0;x<120;++x) {
  bg[(y*120+x)*2]=(deceptus_u16)(1+(x*7+y*13)%400);
  fg[(y*120+x)*2]=(deceptus_u16)(401+(x*3+y*11)%300);
 }
 snes_background_load(0);reset_cache();
 a=acquire_pattern(0,1);b=acquire_pattern(1,1);assert(a!=b);
 assert(a>=768&&a<1024&&b>=1024);
 for(x=2;x<=400;++x)a=acquire_pattern(0,(deceptus_u16)x);
 assert(a<768);assert(acquire_pattern(1,400)==a);
 for(x=0;x<SNES_BG_HASH;++x)if(work->hash_keys[x]==SNES_BG_EMPTY) {
  work->hash_keys[x]=SNES_BG_DELETED;++work->hash_tombstones;
 }
 snes_background_hash_probes=0;assert(hash_find(1500,0)==SNES_BG_EMPTY);
 assert(snes_background_hash_probes==SNES_BG_HASH);
 rebuild_hash();assert(work->hash_tombstones==0);
 snes_background_hash_probes=0;hash_find(1500,0);assert(snes_background_hash_probes<SNES_BG_HASH);
 key=hash_find(400,0);assert(work->hash_slots[key]==a);
 key=hash_find(400|16384,0);assert(work->hash_slots[key]==a);
 reset_cache();
 for(x=1;x<=745;++x)acquire_pattern(0,(deceptus_u16)x);
 for(x=746;x<=1042;++x)acquire_pattern(1,(deceptus_u16)x);
 assert(!snes_background_error);scratch=0;
 for(x=1024;x<1072;++x)scratch+=work->references[x]!=0;
 assert(scratch>=19);prepare_dma();snes_background_commit();
 snes_background_load(0);check(0,0);
 snes_background_set_flags(1);for(x=0;x<12;++x)check(0,0);
 check(10,10);check(10,10);check(11,11);check(11,11);
 check(-22,-18);check(-22,-18);check(-23,-19);check(-23,-19);
 check(10,-18);check(10,-18);check(-22,10);check(-22,10);
 check(12,12);check(12,12);check(100,61);check(100,61);
 snes_background_set_flags(0);check(0,0);
 for(x=1;x<88;++x)check(x,x%2);
 for(y=2;y<62;++y)check(87,y);
 for(x=86;x>=0;--x)check(x,61);
 for(y=60;y>=0;--y)check(0,y);
 check(60,45);check(-1,-1);check(0,0);
 snes_background_load(1);assert(snes_background_error==1);
 puts("background streaming passed");return 0;
}
'''.replace('BACKGROUND_SOURCE', (ROOT / 'src/platform/snes/background.c').as_posix())
        (output / 'test.c').write_text(source)
        executable = output / 'test.exe'
        subprocess.run([compiler, '-std=c90', '-pedantic-errors', '-I' + str(ROOT / 'include'),
                        '-I' + str(output), str(output / 'test.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        header = output / 'snes_assets.h'
        original = header.read_text()
        header.write_text(original.replace('#define SNES_FONT_BYTES 4592', '#define SNES_FONT_BYTES 4609'))
        result = subprocess.run([compiler, '-std=c90', '-fsyntax-only', '-I' + str(ROOT / 'include'),
                                 '-I' + str(output), str(output / 'test.c')], capture_output=True, text=True)
        header.write_text(original)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('source_font_overlaps_foreground_scratch_tiles', result.stderr)


if __name__ == '__main__':
    unittest.main()
