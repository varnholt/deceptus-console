"""Play a GBA, SNES or Mega Drive cartridge, or the shared C desktop build."""

import argparse
import ctypes as C
import os
from pathlib import Path
import time
import tkinter as tk

from PIL import Image, ImageEnhance, ImageTk

from emulator import Emulator, ROOT
from player_display import present


class Host:
    def __init__(self, library):
        self.library = C.CDLL(str(library.resolve()))
        self.library.previewInit()
        self.room = Image.open(ROOT / "assets/generated/room-preview.png").convert("RGB")
        self.actors = Image.open(ROOT / "assets/generated/actors-preview.png")
        self.actors.info["transparency"] = 0
        self.actors = self.actors.convert("RGBA")
        self.sprites = []
        for frame in range(19):
            left, top = (frame % 4) * 32, (frame // 4) * 48
            self.sprites.append(self.actors.crop((left, top, left + 32, top + 48)))

    def run(self, buttons):
        self.library.previewTick(buttons)
        camera = self.library.previewState(2)
        camera_y = self.library.previewState(11)
        canvas = self.room.crop((camera, camera_y, camera + 256, camera_y + 224)).convert("RGBA")
        for index in range(12):
            frame = self.library.previewDraw(index, 2)
            if frame < 0:
                break
            sprite = self.sprites[frame]
            if self.library.previewDraw(index, 4):
                sprite = sprite.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            if self.library.previewDraw(index, 3):
                sprite = sprite.copy()
                red, green, blue, alpha = sprite.split()
                sprite = Image.merge("RGBA", (red.point(lambda value: value // 2),
                    green.point(lambda value: min(255, value + 45)), blue.point(lambda value: value // 2), alpha))
            canvas.alpha_composite(sprite, (self.library.previewDraw(index, 0), self.library.previewDraw(index, 1)))
        if self.library.previewState(3):
            canvas = ImageEnhance.Brightness(canvas).enhance(0.5)
        return canvas


def console_keyboard_mapping(backend, scene):
    mapping = {'left': 6, 'right': 7, 'z': 0, 'space': 0, 'up': 4, 'return': 3, 'r': 2}
    if backend == 'gba':
        mapping.update(z=8, space=8, x=0, down=5, tab=2, c=10, q=10, e=11)
    elif backend == 'snes' and scene == 'tutorial':
        mapping.update(x=8, down=5, tab=2, c=1, q=10, e=11)
    elif backend == 'smd':
        mapping.update(x=8, down=5)
        if scene == 'tutorial':
            mapping.pop('r')
            mapping.update(c=1, q=10, e=11, tab=2)
        else:
            mapping['r'] = 1
    return mapping


def configure_emulator_controls(emulator, backend, scene):
    if backend == 'smd' and scene == 'tutorial':
        controller = emulator.core.retro_set_controller_port_device
        controller.argtypes = [C.c_uint, C.c_uint]
        controller.restype = None
        controller(0, 513)


def open_audio(emulator):
    """Plays the core's audio through PyAudio when it is installed; returns the stream to close, or None."""
    try:
        import pyaudio
    except ImportError:
        print('PyAudio is not installed: playing without sound.', flush=True)
        return None
    audio = pyaudio.PyAudio()
    stream = audio.open(format=pyaudio.paInt16, channels=2, rate=int(round(emulator.sample_rate)), output=True)
    emulator.audio_sink = stream.write
    return audio, stream


def close_audio(output):
    if output:
        audio, stream = output
        stream.stop_stream()
        stream.close()
        audio.terminate()


def play(backend, core, library, rom=None, scene='tutorial', zoom=3, position=None, run_unfocused=False):
    timer_api = C.WinDLL('winmm') if os.name == 'nt' else None
    if timer_api:
        timer_api.timeBeginPeriod(1)
    window = tk.Tk()
    window.title(f"Deceptus — {rom.stem if rom else backend} | " +
               ("arrows move / Z or X jump / C sword / Up interact / Enter menu / Tab inventory"
                if backend == 'gba' or (backend in ('snes', 'smd') and scene == 'tutorial')
                  else ("arrows move / Z or X jump / Up interact / Enter pause / R restart"
                        if backend == "smd" else "arrows move / Z jump / Up interact / Enter pause / R restart")))
    if position is not None:
        window.geometry(f"+{position[0]}+{position[1]}")
    canvas = tk.Label(window, borderwidth=0)
    canvas.pack()
    if backend == 'gba':
        emulator = Emulator(core, rom or ROOT / 'build/gba/deceptus.gba', {'mgba_use_bios': 'OFF', 'mgba_frameskip': '0'})
    elif backend == 'snes':
        emulator = Emulator(core, rom or ROOT / f'build/snes/deceptus-{scene}.sfc')
    elif backend == 'smd':
        filename = 'deceptus-tutorial.bin' if scene == 'tutorial' else 'deceptus-poc.bin'
        emulator = Emulator(core, rom or ROOT / 'build/smd' / filename)
    else:
        emulator = None
    if emulator:
        configure_emulator_controls(emulator, backend, scene)
    audio_output = open_audio(emulator) if emulator else None
    host = Host(library) if backend == "host" else None
    keys = set()
    window.bind("<KeyPress>", lambda event: keys.add(event.keysym.lower()))
    window.bind("<KeyRelease>", lambda event: keys.discard(event.keysym.lower()))
    deadline = time.perf_counter()
    timer = None
    focused = True

    def focus_changed(event):
        nonlocal focused,deadline
        focused = event.type == tk.EventType.FocusIn
        keys.clear()
        deadline = time.perf_counter()

    window.bind('<FocusOut>',focus_changed)
    window.bind('<FocusIn>',focus_changed)

    def refresh():
        nonlocal deadline, timer
        if not focused and not run_unfocused:
            deadline=time.perf_counter()
            timer=window.after(100,refresh)
            return
        if emulator:
            mapping = console_keyboard_mapping(backend, scene)
            emulator.run(1, [value for key, value in mapping.items() if key in keys])
            image = emulator.last_image
        else:
            mapping = {"left": 1, "right": 2, "z": 4, "space": 4, "up": 8, "return": 16, "r": 32}
            buttons = 0
            for key, value in mapping.items():
                if key in keys:
                    buttons |= value
            image = host.run(buttons)
        if image:
            changed = present(canvas, image, (window.winfo_screenwidth(), window.winfo_screenheight()),
                              ImageTk.PhotoImage, zoom)
            if changed:
                window.update_idletasks()
                x = max(0, min(window.winfo_x(), window.winfo_screenwidth() - window.winfo_width() - 16))
                y = max(0, min(window.winfo_y(), window.winfo_screenheight() - window.winfo_height() - 64))
                window.geometry(f"+{x}+{y}")
        deadline += 1 / (emulator.fps if emulator else 60)
        now = time.perf_counter()
        if deadline < now - 0.1:
            deadline = now
        timer = window.after(max(1, round((deadline - now) * 1000)), refresh)

    def close():
        nonlocal timer
        if timer is not None:
            window.after_cancel(timer)
            timer = None
        if emulator:
            emulator.audio_sink = None
            close_audio(audio_output)
            emulator.close()
        if timer_api:
            timer_api.timeEndPeriod(1)
        window.destroy()

    window.protocol("WM_DELETE_WINDOW", close)
    refresh()
    window.mainloop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=["gba", "snes", "smd", "host"], default="gba")
    parser.add_argument("--core", type=Path)
    parser.add_argument("--library", type=Path, default=ROOT / "build/host/Debug/deceptus_preview.dll")
    parser.add_argument("--rom", type=Path, help="open a particular built ROM")
    parser.add_argument('--scene', choices=['tutorial', 'prototype'], default='tutorial')
    parser.add_argument('--zoom', type=int, choices=[1, 2, 3], default=3, help='maximum integer display scale')
    parser.add_argument('--position', type=int, nargs=2, metavar=('X', 'Y'), help='initial window position')
    parser.add_argument('--run-unfocused', action='store_true', help='keep running while another window has focus')
    args = parser.parse_args()
    cores = {'gba': '.tools/mgba/mgba_libretro.dll', 'snes': '.tools/snes9x/snes9x_libretro.dll',
             'smd': '.tools/genesis_plus_gx/genesis_plus_gx_libretro.dll'}
    core = args.core or ROOT / cores.get(args.backend, cores['snes'])
    play(args.backend, core, args.library, args.rom, args.scene, args.zoom, args.position, args.run_unfocused)
