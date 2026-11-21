"""Record an animated GIF of the opening on GBA, SNES or Mega Drive for the project page.

Plays the recorded normal-input route (tests/fixtures/opening-route.json) from New Game on an
ordinary, paced cartridge and keeps every second display frame of the chosen steps. Each step
lasts until the game reaches the state the native C replay reached at its end, so every platform
shows the same scene at its own real frame rate. A late frame that runs two ticks applies the
step's input to both, which can differ from the tick-exact verifier by a tick but not visibly.

    python tools/capture_showcase.py gba --route docs/media/showcase-route.json --first-step 2 --last-step 5 --output docs/media/gba.gif
"""
import argparse
import json
from pathlib import Path

from PIL import Image

from emulator import Emulator, ROOT
import verify_opening
import verify_smd_opening
import verify_snes_opening

PLATFORMS = {
    'gba': dict(core='.tools/mgba/mgba_libretro.dll', rom='build/gba/deceptus-tutorial.gba',
                options={'mgba_use_bios': 'OFF', 'mgba_frameskip': '0'},
                state=verify_opening.gba_state, buttons=verify_opening.retro_buttons),
    'snes': dict(core='.tools/snes9x/snes9x_libretro.dll', rom='build/snes/deceptus-tutorial.sfc', options=None,
                 state=verify_snes_opening.snes_state, buttons=verify_snes_opening.retro_buttons),
    'smd': dict(core='.tools/genesis_plus_gx/genesis_plus_gx_libretro.dll', rom='build/smd/deceptus-tutorial.bin',
                options=None, state=verify_smd_opening.smd_state, buttons=verify_smd_opening.retro_buttons,
                device=513),  # six-button pad: the route's menu/page button is one of the extra three
}
# every second frame of a ~60 Hz display: 3, 3 and 4 hundredths average 1/30 s, which GIF can express.
FRAME_DURATIONS_MS = (30, 30, 40)


def route_targets(route_path, clang):
    """Route steps with the state the native C replay reaches at the end of each one."""
    route = json.loads(Path(route_path).read_text())
    library = verify_opening.compile_native(clang)
    library.openingInit()
    steps = []
    for ticks, pressed in route['steps']:
        for _ in range(ticks):
            library.openingTick(pressed)
        state = verify_opening.native_state(library)
        previous = steps[-1] if steps else None
        room_ticks = state['ticks'] - previous['engine_ticks'] if previous and previous['room'] == state['room'] else None
        steps.append(dict(ticks=ticks, buttons=pressed, room=state['room'], mode=state['mode'], room_ticks=room_ticks,
                          flags=state['flags'],
                          page=state['page'], engine_ticks=state['ticks']))
    return steps


def same_place(state, step):
    """Room, mode and progression flags, and the dialogue page while a dialogue is open: the page
    counter may differ by one between cartridge and native replay once the dialogue has closed.
    The flags keep an interaction step going until the game has acted on the press."""
    keys = ('room', 'mode', 'flags') if step['mode'] == 1 else ('room', 'mode', 'flags', 'page')
    return all(state[key] == step[key] for key in keys)


def reached(state, step, start):
    """A step ends once the game is where the native replay was at its end, having advanced the
    engine clock as far within the room. The clocks are compared as distances from the step's
    start: the cartridge's clock does not match the native one in the first room, and a room
    change restarts both. Dialogue and menus keep the clock still: there only the place counts."""
    if not same_place(state, step):
        return False
    if step['mode'] != 1:
        return True
    if state['room'] == start['room'] and step['room_ticks'] is not None and state['ticks'] >= start['ticks']:
        return state['ticks'] - start['ticks'] >= step['room_ticks']
    return state['ticks'] >= step['engine_ticks']


class Tapper:
    """Play holds the step's buttons, also while walking into a dialogue. An open dialogue only reacts to a fresh press, which a press during
    the typing animation or a room load can miss, so there the buttons are tapped until the game
    moves on. Where the mailbox reports the buttons the game read, a press is held until it was
    read and released until that was read too: a slow renderer reads input only once per
    presentation."""

    def __init__(self):
        self.pressing = True

    def buttons(self, state, step, frames_in_step):
        if state['mode'] == 1:
            return step['buttons']
        if 'campaign_buttons' not in state:
            return step['buttons'] if frames_in_step % 6 < 2 else 0
        seen = state['campaign_buttons'] & step['buttons'] == step['buttons']
        if self.pressing and seen:
            self.pressing = False
        elif not self.pressing and not state['campaign_buttons'] & step['buttons']:
            self.pressing = True
        return step['buttons'] if self.pressing else 0


def state_or_none(platform, emulator):
    try:
        return platform['state'](emulator)
    except AssertionError:
        return None


def capture(name, rom, core, first_step, last_step, route_path, clang, boot_frames=6000):
    platform = PLATFORMS[name]
    steps = route_targets(route_path, clang)
    emulator = Emulator(ROOT / core, rom, platform['options'])
    if platform.get('device'):
        emulator.core.retro_set_controller_port_device(0, platform['device'])
    frames = []
    stats = dict(display_frames=0, ticks=0, presentations=0)
    try:
        for _ in range(boot_frames):
            emulator.run(1)
            state = state_or_none(platform, emulator)
            if state and state['mode'] == 0 and state['health']:
                break
        else:
            raise AssertionError((name, 'did not reach the New Game title'))
        emulator.run(5)
        # the title does not advance the simulation clock: press and release Start like the route.
        for _ in range(60):
            emulator.run(2, platform['buttons'](steps[0]['buttons']))
            emulator.run(1)
            if platform['state'](emulator)['mode'] != 0:
                break
            emulator.run(30)
            # a slow title presentation can start the game during the wait; another press would pause it.
            if platform['state'](emulator)['mode'] != 0:
                break
        else:
            raise AssertionError((name, 'New Game did not start'))
        index, frames_in_step, count = 2, 0, 0
        start = platform['state'](emulator)
        tapper = Tapper()
        while index <= last_step:
            state = platform['state'](emulator)
            if state['content_error']:
                raise AssertionError((name, 'content error', state))
            if reached(state, steps[index], start):
                index, frames_in_step, start = index + 1, 0, state
                continue
            # a presentation that ran two ticks can carry the game into the next step's place.
            for ahead in range(index + 1, min(index + 6, len(steps))):
                if same_place(state, steps[ahead]) and not same_place(state, steps[index]):
                    index, frames_in_step, start = ahead, 0, state
                    break
            emulator.run(1, platform['buttons'](tapper.buttons(state, steps[index], frames_in_step)))
            frames_in_step += 1
            count += 1
            if index >= first_step:
                after = platform['state'](emulator)
                advanced = after['ticks'] - state['ticks'] if after['room'] == state['room'] else after['ticks']
                stats['display_frames'] += 1
                stats['ticks'] += max(0, advanced)
                stats['presentations'] += 1 if advanced > 0 else 0
                if count % 2 == 0:
                    frames.append(emulator.last_image.copy())
            if frames_in_step > steps[index]['ticks'] * 30 + 600:
                emulator.screenshot(ROOT / 'artifacts' / f'showcase-{name}-stalled.png')
                raise AssertionError((name, 'route stalled at step', index, steps[index], state))
    finally:
        emulator.close()
    return frames, stats


def save_gif(frames, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    durations = [FRAME_DURATIONS_MS[index % len(FRAME_DURATIONS_MS)] for index in range(len(frames))]
    # console output has at most a few hundred colours per frame: an undithered adaptive palette per
    # frame keeps every pixel exact (Pillow's default conversion dithers to a fixed web palette).
    paletted = [frame.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for frame in frames]
    paletted[0].save(output, save_all=True, append_images=paletted[1:], duration=durations, loop=0,
                     optimize=True, disposal=1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('platform', choices=sorted(PLATFORMS))
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--core', type=Path)
    parser.add_argument('--route', type=Path, default=ROOT / 'tests/fixtures/opening-route.json')
    parser.add_argument('--first-step', type=int, default=2, help='first route step to record')
    parser.add_argument('--last-step', type=int, default=5, help='last route step to record')
    parser.add_argument('--clang', default='clang', help='compiles the native replay the steps are checked against')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    settings = PLATFORMS[args.platform]
    captured, stats = capture(args.platform, args.rom or ROOT / settings['rom'], args.core or settings['core'],
                       args.first_step, args.last_step, args.route, args.clang)
    save_gif(captured, args.output)
    seconds = stats['display_frames'] / 60
    print(f'{args.output}: {len(captured)} frames, {args.output.stat().st_size // 1024} KiB, '
          f'{seconds:.1f} s, game speed {stats["ticks"] / seconds:.1f} ticks/s, '
          f'{stats["presentations"] / seconds:.1f} presentations/s')
