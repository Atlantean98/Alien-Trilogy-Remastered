"""The packaged game's disc import: checks and imports the player's own CUE/BIN, then builds the generated levels.

Run by the launcher with the bundled Python. Progress goes to import_progress.txt beside the game folder
("<percent>\\n<message>"), a failure's reason to import_error.txt; the exit code is 0 on success.
"""
import argparse
import builtins
import os
from pathlib import Path
import re
import sys
import traceback

GAME = Path(__file__).resolve().parents[2]          # <package>/game
sys.path.insert(0, str(GAME / "tools"))
PROGRESS = GAME / "import_progress.txt"
ERROR = GAME / "import_error.txt"
_print = builtins.print
state = {"percent": 1}


def progress(percent, message):
    state["percent"] = max(state["percent"], percent)
    temporary = PROGRESS.with_suffix(".tmp")
    temporary.write_text(f"{state['percent']}\n{message}", encoding="utf-8")
    os.replace(temporary, PROGRESS)


def watch(*args, **kwargs):
    """Turn the importer's own messages into launcher progress."""
    _print(*args, **kwargs)
    text = " ".join(str(a) for a in args)
    if m := re.match(r"Extracting CD audio track (\d+)", text):
        progress(min(55, 20 + int(m.group(1)) * 2), f"Extracting the music (CD track {m.group(1)})...")
    elif text.startswith("Decoding "):
        progress(min(85, state["percent"] + 2), "Decoding the cinematics: " + Path(text[9:]).stem.title() + "...")
    elif text.startswith("FFmpeg/FFprobe not found"):
        progress(56, "FFmpeg is not installed: importing without the cinematics...")
    elif text.startswith("> "):
        progress(min(97, max(state["percent"], 86) + 3), "Building the Survival arenas and Extermination levels...")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--disc", required=True, type=Path)
    disc = parser.parse_args().disc
    for path in (ERROR, PROGRESS):
        path.unlink(missing_ok=True)
    try:
        if disc.suffix.lower() not in (".cue", ".bin"):
            raise ValueError("Choose the disc image's .cue file (a BIN/CUE dump of your own disc).")
        if disc.suffix.lower() == ".cue" and not re.search(r'^\s*FILE\s+"[^"]+"\s+BINARY', disc.read_text(errors="replace"), re.I | re.M):
            raise ValueError("That .cue file does not name any .bin tracks.")
        builtins.print = watch
        progress(5, "Checking your disc image (Alien Trilogy, NTSC-U, SLUS-00007)...")
        import import_pc_ps1
        progress(10, "Verifying and copying the game files...")
        import_pc_ps1.install(disc, GAME)
        progress(86, "Building the Survival arenas and Extermination levels...")
        import build_game_data
        build_game_data.main()
        (GAME / "GameData" / "package-ready.txt").write_text("ready\n")
        progress(100, "Done")
        return 0
    except BaseException as error:   # noqa: BLE001 - every failure is reported to the launcher
        builtins.print = _print
        traceback.print_exc()
        message = str(error) or type(error).__name__
        if isinstance(error, FileNotFoundError):
            message = "A file named by the disc image is missing: " + message
        ERROR.write_text(message, encoding="utf-8")
        return 1


if __name__ == "__main__":
    sys.exit(main())
