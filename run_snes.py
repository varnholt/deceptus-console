"""Fetch the SNES emulator if needed, then run Deceptus."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / 'tools'))
from run_game import main

if __name__ == '__main__':
    raise SystemExit(main('snes'))
