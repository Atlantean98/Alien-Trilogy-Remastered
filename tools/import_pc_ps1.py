"""Import the full NTSC-U PlayStation disc into GameData/PS1 for the PC build."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
DISC_TOOLS = ROOT / "DemoWork"
sys.dont_write_bytecode = True
sys.path.insert(0, str(DISC_TOOLS))
from importer import EXECUTABLE_SHA256, music
from disc_input import Disc
import mdec


def movie_directory(disc):
    """Read STR directory entries while leaving their Mode 2 sectors intact."""
    record = disc.read(16, 2048)[156:190]
    directory = disc.read(int.from_bytes(record[2:6], "little"),
                          int.from_bytes(record[10:14], "little"))
    offset = 0
    while offset < len(directory):
        length = directory[offset]
        if not length:
            offset = (offset // 2048 + 1) * 2048
            continue
        entry = directory[offset:offset + length]
        if len(entry) >= 36 and entry[25] & 2 and entry[33:33 + entry[32]] == b"STR":
            disc.scan(int.from_bytes(entry[2:6], "little"),
                      int.from_bytes(entry[10:14], "little"), "STR/")
            return [row for row in disc.entries if row[0].startswith("STR/") and row[0].endswith(".STR")]
        offset += length
    raise ValueError("PS1 movie directory missing")


def decode_movie(disc, entry, out, ffmpeg, ffprobe):
    path, sector, size = entry
    name = Path(path).stem
    raw = out / (name + ".STR")
    data = b"".join(disc.sector(disc.data, sector + i) for i in range((size + 2047) // 2048))
    raw.write_bytes(data)
    streams = json.loads(subprocess.check_output([
        ffprobe, "-v", "error", "-show_streams", "-of", "json", str(raw)
    ]))["streams"]
    video = next(stream for stream in streams if stream["codec_type"] == "video")
    width, height = video["width"], video["height"]

    # Read the original frame cadence from the first sectors of each frame.
    # The STR demuxer defaults to 15 fps, while some movies use 30 fps.
    frame_starts = []
    audio_start = None
    sector_count = len(data) // 2352
    for index in range(sector_count):
        offset = index * 2352
        kind = data[offset + 18] & 14
        if kind == 4 and audio_start is None:
            audio_start = index
        if (kind in (2, 8) and data[offset + 24:offset + 28] == bytes.fromhex("60 01 01 80")
                and struct.unpack_from("<H", data, offset + 28)[0] == 0):
            frame_starts.append(index)
    cadence = round(statistics.median(b - a for a, b in zip(frame_starts, frame_starts[1:])))
    divisor = math.gcd(150, cadence)
    rate_num, rate_den = 150 // divisor, cadence // divisor

    target = out / (name + ".bgra")
    subprocess.run([
        ffmpeg, "-v", "error", "-y", "-i", str(raw), "-map", "0:v:0",
        "-fps_mode", "passthrough", "-pix_fmt", "bgra", "-f", "rawvideo", str(target)
    ], check=True)
    video_size = target.stat().st_size
    frame_size = width * height * 4
    if not video_size or video_size % frame_size:
        raise ValueError("Incomplete decoded movie: " + name)
    frames = video_size // frame_size
    audio = out / (name + ".pcm")
    has_audio = any(stream["codec_type"] == "audio" for stream in streams)
    if has_audio:
        subprocess.run([
            ffmpeg, "-v", "error", "-y", "-i", str(raw), "-map", "0:a:0",
            "-ar", "44100", "-ac", "2", "-c:a", "pcm_s16le", "-f", "s16le", str(audio)
        ], check=True)
        if audio_start:
            audio.write_bytes(bytes(round(audio_start * 44100 / 150) * 4) + audio.read_bytes())
    (out / (name + ".fmv")).write_bytes(
        b"ATFMV002" + struct.pack("<6I", width, height, rate_num, rate_den, frames, sector_count)
    )
    raw.unlink()
    return {
        "name": name, "source": entry, "raw_sha256": hashlib.sha256(data).hexdigest(),
        "width": width, "height": height, "fps": f"{rate_num}/{rate_den}", "frames": frames,
        "video_seconds": frames * rate_den / rate_num, "playback_seconds": sector_count / 150,
        "audio_start_sector": audio_start, "audio_seconds": audio.stat().st_size / 176400 if has_audio else 0,
    }


def install(image, root):
    ffmpeg = shutil.which("ffmpeg")
    ffprobe = shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:   # the game plays without them; import again once FFmpeg is installed
        ffmpeg = ffprobe = None
        print("FFmpeg/FFprobe not found: importing without the cinematics", flush=True)
    game_data = Path(root).resolve() / "GameData"
    if game_data.is_symlink() or game_data.is_junction():
        raise ValueError("GameData must be a real directory")
    game_data.mkdir(parents=True, exist_ok=True)
    target = game_data / "PS1"
    if target.is_symlink() or target.is_junction():
        raise ValueError("GameData/PS1 must be a real directory")
    expected = json.loads((DISC_TOOLS / "supported_files.json").read_text())
    disc = Disc(image, DISC_TOOLS / "third_party/libchdr-build/libchdr.dll")
    try:
        executable = disc.file("SLUS_000.07")
        if hashlib.sha256(executable).hexdigest() != EXECUTABLE_SHA256:
            raise ValueError("Use the supported NTSC-U Alien Trilogy PS1 disc (SLUS-00007)")
        missions = disc.file("LANGUAGE/MISSIONU.TXT").decode("ascii")
        codes = [code.replace(".", "") for code in re.findall(r"^(\d\.\d\.\d) ", missions, re.M)]
        if len(codes) != 35:
            raise ValueError("Unexpected campaign table")
        tracks = struct.unpack_from("<35H", executable, 0x63974)
        sound_ids = struct.unpack_from("<35H", executable, 0x6392c)
        banks = []
        for index in sound_ids:
            pointer = struct.unpack_from("<I", executable, 0x5adf8 + index * 36)[0]
            offset = pointer - 0x80010000 + 2048
            end = executable.index(0, offset)
            banks.append(executable[offset:end].decode("ascii").lstrip("\\").split(";")[0].replace("\\", "/"))
        available = {track["track"]: track for track in disc.tracks if track["mode"] == "AUDIO"}
        needed = {2, *tracks}
        if not needed <= available.keys():
            raise ValueError("The disc image is missing CD audio tracks")
        movies = movie_directory(disc)
        if len(movies) != 15:
            raise ValueError("Unexpected number of PS1 movies")
        with tempfile.TemporaryDirectory(prefix=".ps1-import-", dir=game_data) as directory:
            stage = Path(directory) / "PS1"
            cd = stage / "CD"
            for name, descriptor in expected.items():
                data = disc.file(name)
                if len(data) != descriptor["size"] or hashlib.sha256(data).hexdigest() != descriptor["sha256"]:
                    raise ValueError("Disc file failed verification: " + name)
                path = cd / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            (stage / "level_music.txt").write_text("".join(f"{code} {track}\n" for code, track in zip(codes, tracks)))
            (stage / "level_sfx.txt").write_text("".join(f"{code} {bank}\n" for code, bank in zip(codes, banks)))
            for name in ("Decoded", "Music", "Movies"):
                (stage / name).mkdir()
            for name in ("COLONY", "PRISHOLD", "BONESHIP", "BACKDROP"):
                (stage / "Decoded" / (name + ".rgba")).write_bytes(mdec.decode((cd / "GFX" / (name + ".BS")).read_bytes()))
            for number in sorted(needed):
                print(f"Extracting CD audio track {number}", flush=True)
                music(disc, available[number], stage / "Music" / f"track{number:02}.wav")
            movie_report = []
            for entry in movies if ffmpeg else ():
                print("Decoding " + entry[0], flush=True)
                movie_report.append(decode_movie(disc, entry, stage / "Movies", ffmpeg, ffprobe))
            (stage / "import.json").write_text(json.dumps({
                "disc_sha256": EXECUTABLE_SHA256, "disc_files": len(expected),
                "campaign_missions": len(codes), "tracks": sorted(needed), "movies": movie_report
            }, indent=2))
            backup = None
            if target.exists():
                backup = game_data / ("PS1.previous-" + uuid.uuid4().hex)
                target.rename(backup)
            try:
                stage.rename(target)
            except OSError:
                if backup:
                    backup.rename(target)
                raise
            print(f"Full PS1 import ready in {target}")
            if backup:
                print(f"Previous PS1 import preserved at {backup}")
    finally:
        disc.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", default=ROOT, type=Path)
    args = parser.parse_args()
    install(args.disc, args.output)
