"""Turn the original spike ball into the images the consoles pick from by the chain's angle."""
from pathlib import Path
import re

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHEET = 'spike-ball.png'
# spikeball.cpp texture rects: the ball with its origin, then the two chain link sprites.
BALL_RECT, BALL_ORIGIN = (118, 24, 51, 50), (25, 25)
LINK_POSITIONS = ((297, 56), (320, 56))


def header_define(name):
    header = (ROOT / 'include/deceptus/spikeball.h').read_text()
    return int(re.search(rf'^#define {name} (\d+)$', header, re.M).group(1))


ROTATIONS = header_define('SPIKE_BALL_ROTATIONS')
FRAME_SIZE = header_define('SPIKE_BALL_FRAME_SIZE')
LINK_SIZE = header_define('SPIKE_BALL_LINK_SIZE')
IMAGE_COUNT = ROTATIONS + 2


def compile_spike_ball(data, output):
    """Write one row of FRAME_SIZE cells: the ball turned in even steps from pointing right to pointing
    left around the middle of its cell (spikeball.cpp turns it by the chain's angle), then the two
    chain links in the top left corner of their cells."""
    atlas = Image.open(Path(data) / 'sprites/enemy_spikeball.png').convert('RGBA')
    x, y, width, height = BALL_RECT
    centre = FRAME_SIZE // 2
    ball = Image.new('RGBA', (FRAME_SIZE, FRAME_SIZE))
    ball.alpha_composite(atlas.crop((x, y, x + width, y + height)), (centre - BALL_ORIGIN[0], centre - BALL_ORIGIN[1]))
    sheet = Image.new('RGBA', (FRAME_SIZE * IMAGE_COUNT, FRAME_SIZE))
    step = 180 / (ROTATIONS - 1)
    for index in range(ROTATIONS):
        turned = ball.rotate(90 - index * step, resample=Image.Resampling.NEAREST, center=(centre, centre))
        sheet.alpha_composite(turned, (index * FRAME_SIZE, 0))
    for index, (x, y) in enumerate(LINK_POSITIONS):
        sheet.alpha_composite(atlas.crop((x, y, x + LINK_SIZE, y + LINK_SIZE)), ((ROTATIONS + index) * FRAME_SIZE, 0))
    sheet.save(Path(output) / SHEET)
