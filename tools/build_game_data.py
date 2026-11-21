"""Build the levels the PC game generates from your imported disc (run tools/import_pc_ps1.py first):

  GameData/SURVIVAL, SURVIVAL2, SURVIVAL3   the three Survival arenas (Xbox360/tools/make_survival_*.py, shared
                                            with the console port)
  GameData/Isolation/1-3                    the Extermination levels (tools/build_extermination_decks.py)

Needs Python 3 with Pillow (pip install pillow).

Usage: python tools/build_game_data.py
"""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "Xbox360" / "tools"


def run(script):
    print(f"> {script.relative_to(ROOT)}")
    subprocess.run([sys.executable, str(script)], cwd=ROOT, check=True)


def main():
    if not (ROOT / "GameData" / "PS1" / "CD" / "SECT11").is_dir():
        raise SystemExit("Import your PS1 disc first: python tools/import_pc_ps1.py --disc <path to the .cue>")
    # Survival arena 1 is written beside the generator; the game reads it from GameData/SURVIVAL.
    run(SHARED / "make_survival_map.py")
    arena = ROOT / "GameData" / "SURVIVAL"
    arena.mkdir(parents=True, exist_ok=True)
    for name in ("L162LEV.MAP", "162GFX.BIN", "level.txt"):
        shutil.copyfile(ROOT / "Xbox360" / "assets" / "survival" / name, arena / name)
    run(SHARED / "make_survival_lairs.py")   # arenas 2 and 3 (copies itself to GameData/SURVIVAL2|3)
    run(ROOT / "tools" / "build_extermination_decks.py")
    print("Game data ready.")


if __name__ == "__main__":
    main()
