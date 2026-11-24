"""Audit every converted water frame and viewport through the real C cache."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def audit(assets, output):
    assets, output = Path(assets).resolve(), Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang')
    if not compiler:
        raise SystemExit('native clang is required for the background audit')
    header = (assets / 'snes_assets.h').read_text()
    (output / 'snes_assets.h').write_text(header)
    registers = 'VMAIN VMADDLH DMAP0 BBAD0 A1T0LH A1B0 DAS0LH MDMAEN'.split()
    sdk = ['typedef unsigned char u8;',
           'typedef struct {union {void*p;struct {unsigned short addr;unsigned char bank;} c;} mem;} dmaMemory;',
           'static unsigned short reg;', '#define SC_64x32 1']
    sdk += ['#define REG_' + name + ' reg' for name in registers]
    sdk += ['static void bgSetGfxPtr(int a,int b){(void)a;(void)b;}',
            'static void bgSetMapPtr(int a,int b,int c){(void)a;(void)b;(void)c;}',
            'static void bgSetScroll(int a,int b,int c){(void)a;(void)b;(void)c;}',
            'static void WaitForVBlank(void){}',
            'static void dmaCopyCGram(u8*a,int b,int c){(void)a;(void)b;(void)c;}',
            'static void dmaCopyVram(u8*a,int b,int c){(void)a;(void)b;(void)c;}']
    (output / 'snes.h').write_text('\n'.join(['#ifndef SNES_STUB_H', '#define SNES_STUB_H', *sdk, '#endif']) + '\n')
    resources = (assets / 'opening_resources.c').read_text()
    # columns, rows, pattern count, then the cache pool after the title.
    stages = {int(room): tuple(map(int, values)) for room, *values in re.findall(
        r'snes_area(\d+)_palette,(\d+),(\d+),(\d+),[^\n]*,(\d+),(\d+)\},\n', resources)}
    reports = []
    for room, body in re.findall(r'static const SnesBackgroundAnimation snes_area(\d+)_animations\[\] = \{(.*?)\n\};', resources, re.S):
        room = int(room)
        for animation, line in enumerate(body.strip().splitlines()):
            match = re.fullmatch(r'\s*\{(\d+),(\d+),(\d+),(\d+),12,6,0,0,0,\{[^,]+,(\d+)\},0\},', line)
            if not match:
                continue
            left, top, columns, rows, animation_bytes = map(int, match.groups())
            width, height, count, pool_first, pool_end = stages[room]
            for name in ('background', 'foreground', f'animation{animation}_cells'):
                chunks = sorted(assets.glob(f'snes_area{room}_{name}_*.bin'), key=lambda p: int(p.stem.rsplit('_', 1)[1]))
                if not chunks:
                    raise ValueError('missing converted cells: ' + name)
                (output / (name + '.bin')).write_bytes(b''.join(path.read_bytes() for path in chunks))
            source = r'''
#include <assert.h>
#include <stdio.h>
#include "BACKGROUND"
#include "COMMIT"
void snesVramRunDma(const SnesVramDmaCommand *commands,DeceptusU16 count) {(void)commands;(void)count;}
DeceptusU8 snes_background_storage[32768];
static DeceptusU8 bg[131072],fg[131072],cells[131072],dummy[32768],palette[256];
static const DeceptusU8 *const bc[]={bg,bg+32768,bg+65536,bg+98304};
static const DeceptusU8 *const fc[]={fg,fg+32768,fg+65536,fg+98304};
static const DeceptusU8 *const ac[]={cells,cells+32768,cells+65536,cells+98304};
static const DeceptusU8 *const pc[]={dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy,dummy};
static const SnesBackgroundAnimation clips[]={{LEFT,TOP,COLS,ROWS,12,65535,0,0,0,{ac,ANIMBYTES},0}};
const SnesStage snes_stages[SNES_STAGE_COUNT]={{{pc,COUNT*32L},{bc,WIDTH*HEIGHT*4L},{fc,WIDTH*HEIGHT*4L},palette,WIDTH,HEIGHT,COUNT,clips,1,"water",POOLFIRST,POOLEND}};
static void read_cells(const char*name,DeceptusU8*target,unsigned long bytes) {
 FILE*f=fopen(name,"rb");assert(f);assert(fread(target,1,bytes,f)==bytes);fclose(f);
}
int main(void) {
 int frame,x,y,step;unsigned slot,used,scratch,peak=0,peak_scratch=0,views=0,rebuilds=0,compactions=0;
 read_cells("background.bin",bg,WIDTH*HEIGHT*4L);read_cells("foreground.bin",fg,WIDTH*HEIGHT*4L);read_cells("animationANIM_cells.bin",cells,ANIMBYTES);
 for(frame=0;frame<12;++frame) {
  snesBackgroundLoad(0);work->clip_frame[0]=(DeceptusU16)frame;
  for(y=0;y<=HEIGHT-29;++y)for(step=0;step<=WIDTH-33;++step) {
   x=(y&1)?WIDTH-33-step:step;
   snesBackgroundPrepare((DeceptusI16)(x*8),(DeceptusI16)(y*8));
   if(snesBackgroundError()) {fprintf(stderr,"cache_error=%u frame=%d x=%d y=%d\n",snesBackgroundError(),frame,x,y);return 2;}
   if(snesBackgroundFull())++rebuilds;
   used=0;scratch=0;
   for(slot=1;slot<SNES_BG_SLOTS;++slot)if(work->references[slot]) {
    ++used;if(slot>=1024)++scratch;
   }
   if(used>peak)peak=used;if(scratch>peak_scratch)peak_scratch=scratch;
   ++views;snesBackgroundCommit();
  }
  compactions+=snes_background_compactions;
 }
 printf("{\"views\":%u,\"peak_slots\":%u,\"peak_scratch\":%u,\"compactions\":%u,\"extra_forced_blank_batches\":%u}\n",views,peak,peak_scratch,compactions,rebuilds-12);return 0;
}
'''
            replacements = dict(BACKGROUND=(ROOT / 'src/platform/snes/background.c').as_posix(),
                                COMMIT=(ROOT / 'src/platform/snes/background_commit.c').as_posix(), LEFT=left, TOP=top,
                                COLS=columns, ROWS=rows, ANIMBYTES=animation_bytes, WIDTH=width, HEIGHT=height,
                                COUNT=count, POOLFIRST=pool_first, POOLEND=pool_end, ANIM=animation)
            for key, value in replacements.items():
                if key == 'ANIM':
                    source = source.replace('animationANIM_cells', 'animation' + str(value) + '_cells')
                else:
                    source = re.sub(r'\b' + key + r'\b', lambda match: str(value), source)
            (output / 'audit.c').write_text(source)
            executable = output / 'audit.exe'
            subprocess.run([compiler, '-O2', '-std=c90', '-pedantic-errors', '-D_CRT_SECURE_NO_WARNINGS', '-I' + str(ROOT / 'include'),
                            '-I' + str(output), str(output / 'audit.c'), '-o', str(executable)], check=True)
            result = subprocess.run([str(executable)], cwd=output, check=True, capture_output=True, text=True)
            report = dict(room=room, animation=animation, frames=12, **json.loads(result.stdout))
            reports.append(report)
            print(json.dumps(report), flush=True)
    if not reports:
        raise ValueError('no converted twelve-frame water clips found')
    (output / 'report.json').write_text(json.dumps(reports, indent=2) + '\n')
    return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets-dir', type=Path, default=ROOT / 'assets/generated/snes')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/tests/snes-water-cache')
    args = parser.parse_args()
    audit(args.assets_dir, args.output)
