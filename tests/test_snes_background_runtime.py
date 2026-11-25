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
#ifndef SNES_STUB_H
#define SNES_STUB_H
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
#endif
''')
        (output / 'snes_assets.h').write_text('''
#include "deceptus/types.h"
typedef struct {const DeceptusU8 *const *chunks;DeceptusU32 bytes;} SnesRomBlob;
typedef struct {DeceptusU16 left,top,columns,rows,frames,frame_ticks,required_flags,forbidden_flags;const DeceptusU16*durations;SnesRomBlob cells;DeceptusU8 plane;} SnesBackgroundAnimation;
typedef struct {SnesRomBlob patterns,background,foreground;const DeceptusU8*palette;DeceptusU16 columns,rows,pattern_count;const SnesBackgroundAnimation*animations;DeceptusU8 animation_count;const char*title;DeceptusU16 pool_first,pool_end;} SnesStage;
#define SNES_STAGE_COUNT 1
#define SNES_FONT_BYTES 2304
extern const SnesStage snes_stages[1];
''')
        source = r'''
#include <assert.h>
#include <stdio.h>
/* the commit's vblank wait reads video registers; the native test has none. */
#define SNES_PPU_TIMING_H
static void snesWaitForVblank(void) {}
#include "BACKGROUND_SOURCE"
#include "COMMIT_SOURCE"
DeceptusU8 snes_background_storage[32768];
SnesBackgroundPacket snes_background_packets[SNES_BG_PACKETS];
void snesVramRunDma(const SnesVramDmaCommand *commands,DeceptusU16 count) {
 unsigned i;
 assert(count<=SNES_BG_DMA_COMMANDS);
 for(i=0;i<count;++i) {
  assert(commands[i].bytes>0);
  assert(commands[i].increment==0x80||commands[i].increment==0x81);
  assert(commands[i].target<0x1000||(commands[i].target>=SNES_BG_OVERFLOW_ADDRESS&&commands[i].target<0x6000));
 }
}
/* what the main loop does with a prepared frame: stage it into a packet, then commit that. */
static void stageAndCommit(void) {
 snesBackgroundStage(0);snesBackgroundCommit(0);
}
static DeceptusU16 bg[120*90*2],fg[120*90*2];
static DeceptusU8 patterns[1600*32],palette[256];
static const DeceptusU8 *const bg_chunks[]={(DeceptusU8*)bg,(DeceptusU8*)bg+32768};
static const DeceptusU8 *const fg_chunks[]={(DeceptusU8*)fg,(DeceptusU8*)fg+32768};
static const DeceptusU8 *const pattern_chunks[]={patterns,patterns+32768};
static DeceptusU16 clip_cells[16];
static const DeceptusU8 *const clip_chunks[]={(DeceptusU8*)clip_cells};
static const SnesBackgroundAnimation clips[]={{10,10,2,2,2,2,1,0,0,{clip_chunks,sizeof(clip_cells)},1}};
const SnesStage snes_stages[]={{{pattern_chunks,sizeof(patterns)},{bg_chunks,sizeof(bg)},{fg_chunks,sizeof(fg)},palette,120,90,1600,clips,1,"test",1,SNES_BG_SLOTS}};
static void check(int cx,int cy) {
 int x,y,p;unsigned slot,total,expected,actual[SNES_BG_SLOTS]={0};
 snesBackgroundPrepare((DeceptusI16)(cx*8),(DeceptusI16)(cy*8));
 assert(!work->status.error);
 for(p=0;p<2;++p)for(y=cy;y<cy+29;++y)for(x=cx;x<cx+33;++x) {
  slot=tileSlot((DeceptusU8)p,work->maps[p][mapAddress((DeceptusI16)x,(DeceptusI16)y)]);
  if(x<0||y<0||x>=120||y>=90) {assert(slot==0);continue;}
  expected=(p?fg:bg)[(y*120+x)*2];
  if(p==1&&(source_flags&1)&&x>=10&&x<12&&y>=10&&y<12) {expected=690+work->clip_frame[0];}
  assert(work->resident[slot]==expected);
  ++actual[slot];
 }
 total=0;
 for(slot=1;slot<SNES_BG_SLOTS;++slot) {assert(actual[slot]==work->references[slot]);total+=actual[slot];}
 assert(total<=2*33*29);
 stageAndCommit();
}
int main(void) {
 int x,y;unsigned a,b,scratch,key;
 for(x=0;x<8;++x) {clip_cells[x*2]=(DeceptusU16)(690+x/4);}
 for(y=0;y<90;++y)for(x=0;x<120;++x) {
  bg[(y*120+x)*2]=(DeceptusU16)(1+(x*7+y*13)%400);
  fg[(y*120+x)*2]=(DeceptusU16)(401+(x*3+y*11)%300);
 }
 snesBackgroundLoad(0);resetCache();
 a=acquirePattern(0,1);b=acquirePattern(1,1);assert(a!=b);
 assert(a>=768&&a<1024&&b>=1024);
 for(x=2;x<=400;++x)a=acquirePattern(0,(DeceptusU16)x);
 assert(a<768);assert(acquirePattern(1,400)==a);
 for(x=0;x<SNES_BG_HASH;++x)if(work->hash_keys[x]==SNES_BG_EMPTY) {
  work->hash_keys[x]=SNES_BG_DELETED;++work->hash_tombstones;
 }
 snes_background_hash_probes=0;assert(hashFind(1500,0)==SNES_BG_EMPTY);
 assert(snes_background_hash_probes==SNES_BG_HASH);
 rebuildHash();assert(work->hash_tombstones==0);
 snes_background_hash_probes=0;hashFind(1500,0);assert(snes_background_hash_probes<SNES_BG_HASH);
 key=hashFind(400,0);assert(work->hash_slots[key]==a);
 key=hashFind(400|16384,0);assert(work->hash_slots[key]==a);
 resetCache();
 for(x=1;x<=745;++x)acquirePattern(0,(DeceptusU16)x);
 for(x=746;x<=1042;++x)acquirePattern(1,(DeceptusU16)x);
 assert(!work->status.error);scratch=0;
 for(x=1024;x<SNES_BG_SLOTS;++x)scratch+=work->references[x]!=0;
 assert(scratch>=19);prepareDma();stageAndCommit();
 snesBackgroundLoad(0);check(0,0);
 snesBackgroundSetFlags(1);for(x=0;x<12;++x)check(0,0);
 check(10,10);check(10,10);check(11,11);check(11,11);
 check(-22,-18);check(-22,-18);check(-23,-19);check(-23,-19);
 check(10,-18);check(10,-18);check(-22,10);check(-22,10);
 check(12,12);check(12,12);check(100,61);check(100,61);
 snesBackgroundSetFlags(0);check(0,0);
 for(x=1;x<88;++x)check(x,x%2);
 for(y=2;y<62;++y)check(87,y);
 for(x=86;x>=0;--x)check(x,61);
 for(y=60;y>=0;--y)check(0,y);
 check(60,45);check(-1,-1);check(0,0);
 snesBackgroundLoad(1);assert(snesBackgroundError()==1);
 puts("background streaming passed");return 0;
}
'''.replace('BACKGROUND_SOURCE', (ROOT / 'src/platform/snes/background.c').as_posix()).replace('COMMIT_SOURCE', (ROOT / 'src/platform/snes/background_commit.c').as_posix())
        (output / 'test.c').write_text(source)
        executable = output / 'test.exe'
        subprocess.run([compiler, '-std=c90', '-pedantic-errors', '-I' + str(ROOT / 'include'),
                        '-I' + str(output), str(output / 'test.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        header = output / 'snes_assets.h'
        original = header.read_text()
        header.write_text(original.replace('#define SNES_FONT_BYTES 2304', '#define SNES_FONT_BYTES 2305'))
        result = subprocess.run([compiler, '-std=c90', '-fsyntax-only', '-I' + str(ROOT / 'include'),
                                 '-I' + str(output), str(output / 'test.c')], capture_output=True, text=True)
        header.write_text(original)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('source_font_overlaps_foreground_overflow_patterns', result.stderr)


if __name__ == '__main__':
    unittest.main()
