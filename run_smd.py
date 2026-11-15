"""Launch the Mega Drive proof of concept with project-local tools."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / 'tools'))
from smd_player import main

if __name__ == '__main__':
    raise SystemExit(main())
