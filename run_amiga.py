"""Launch the experimental one-room Amiga 500 build."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / 'tools'))
from amiga_player import main

if __name__ == '__main__':
    raise SystemExit(main())
