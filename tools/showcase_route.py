"""Derive a natural-looking demo route for the project clips from the verification route.

The verification route is tuned to pass checkpoints, so it hops wherever a hop is harmless. For a
clip this module keeps the route up to a room entrance, then walks the way a player would: hold
the direction, and jump only where the player is stopped by a step or gap. The steps are computed
with the native C simulation, which every backend runs identically, so each platform replays the
same scene.

    python tools/showcase_route.py --prefix-steps 594 --target-x 855 --output docs/media/showcase-route.json
"""
import argparse
import json
from pathlib import Path

from emulator import ROOT
import verify_opening

RIGHT, JUMP = 2, 4


def natural_walk(library, prefix, target_x, limit=3000, settle=40):
    """Ticks of (buttons) after the prefix: walk right, jump only when stuck, stop at target_x."""
    for ticks, buttons in prefix:
        for _ in range(ticks):
            library.openingTick(buttons)
    start = verify_opening.native_state(library)
    inputs, history, jumping = [], [], 0
    for _ in range(limit):
        state = verify_opening.native_state(library)
        if state['room'] != start['room'] or state['x'] >= target_x:
            break
        history.append(state['x'])
        stuck = len(history) > 6 and history[-1] <= history[-7]
        if jumping:
            jumping -= 1
        elif stuck:
            jumping = 14
        buttons = RIGHT | (JUMP if jumping else 0)
        library.openingTick(buttons)
        inputs.append(buttons)
    inputs += [0] * settle
    for _ in range(settle):
        library.openingTick(0)
    return inputs, verify_opening.native_state(library)


def compress(inputs):
    steps = []
    for buttons in inputs:
        if steps and steps[-1][1] == buttons:
            steps[-1][0] += 1
        else:
            steps.append([1, buttons])
    return steps


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--route', type=Path, default=ROOT / 'tests/fixtures/opening-route.json')
    parser.add_argument('--prefix-steps', type=int, required=True, help='verification steps kept before the walk')
    parser.add_argument('--target-x', type=int, required=True, help='room x where the walk ends')
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    route = json.loads(args.route.read_text())
    prefix = route['steps'][:args.prefix_steps]
    library = verify_opening.compile_native(args.clang)
    library.openingInit()
    inputs, end = natural_walk(library, prefix, args.target_x)
    walk = compress(inputs)
    jumps = sum(1 for _, buttons in walk if buttons & JUMP)
    args.output.write_text(json.dumps(dict(
        scope=f'verification route steps 0-{args.prefix_steps - 1}, then a natural walk to x={args.target_x}',
        steps=prefix + walk, checkpoints=[]), indent=1) + '\n')
    print(f'{args.output}: walk of {len(inputs)} ticks, {jumps} jumps, ends room {end["room"]} x={end["x"]} y={end["y"]}')
