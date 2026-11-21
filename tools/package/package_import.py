"""The packaged game's disc import: checks and imports the player's own CUE/BIN, then builds the generated levels.

Run by the launcher with the bundled Python. Progress goes to import_progress.txt beside the game folder
("<percent>\\n<message>"), a failure's reason to import_error.txt; the exit code is 0 on success.
"""
import argparse
import builtins
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import traceback

GAME = Path(__file__).resolve().parents[2]          # <package>/game
sys.path.insert(0, str(GAME / "tools"))
PROGRESS = GAME / "import_progress.txt"
ERROR = GAME / "import_error.txt"
_print = builtins.print
state = {"percent": 1}


def scrub(text):
    """Keep the player's Windows user folder and name out of the log and errors (players attach them to bug reports)."""
    text = str(text)
    profile = os.environ.get("USERPROFILE", "")
    for form in {profile, profile.replace("\\", "/"), profile.replace("\\", "\\\\")}:
        if len(form) >= 4:
            text = re.sub(re.escape(form), "%USERPROFILE%", text, flags=re.I)
    name = os.environ.get("USERNAME", "")
    if len(name) >= 3:
        text = re.sub(r"(?<![A-Za-z0-9])" + re.escape(name) + r"(?![A-Za-z0-9])", "<user>", text, flags=re.I)
    return text


def progress(percent, message):
    """Tell the launcher how far the import is. Never fails the import: the launcher reads this file several times a
    second (and antivirus may scan it), so Windows can refuse to replace it for a moment; then retry, write it in place,
    or skip this update."""
    state["percent"] = max(state["percent"], percent)
    text = f"{state['percent']}\n{scrub(message)}"
    temporary = PROGRESS.with_suffix(".tmp")
    for _ in range(20):
        try:
            temporary.write_text(text, encoding="utf-8")
            os.replace(temporary, PROGRESS)
            return
        except OSError:
            time.sleep(0.05)
    try:
        PROGRESS.write_text(text, encoding="utf-8")   # the launcher shares write access, so this usually works
    except OSError:
        pass


def watch(*args, **kwargs):
    """Turn the importer's own messages into launcher progress."""
    _print(*(scrub(a) for a in args), **kwargs)
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

        def run(script):   # the level builders run as their own processes: pass their output through scrub()
            print(f"> {script.relative_to(build_game_data.ROOT)}")
            done = subprocess.run([sys.executable, str(script)], cwd=build_game_data.ROOT, capture_output=True,
                                  text=True, errors="replace")
            for line in (done.stdout + done.stderr).splitlines():
                _print(scrub(line))
            if done.returncode:
                raise RuntimeError(f"{script.name} failed (exit code {done.returncode}); see import.log")

        build_game_data.run = run
        build_game_data.main()
        (GAME / "GameData" / "package-ready.txt").write_text("ready\n")
        progress(100, "Done")
        return 0
    except BaseException as error:   # noqa: BLE001 - every failure is reported to the launcher
        builtins.print = _print
        print(scrub(traceback.format_exc()))
        message = str(error) or type(error).__name__
        if isinstance(error, FileNotFoundError):
            message = "A file named by the disc image is missing: " + message
        ERROR.write_text(scrub(message), encoding="utf-8")
        return 1


if __name__ == "__main__":
    sys.exit(main())
