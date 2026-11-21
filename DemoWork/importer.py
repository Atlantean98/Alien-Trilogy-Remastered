"""Offline PS1-only disc importer for Alien Trilogy Remastered Section 1."""
from pathlib import Path
import argparse
import array
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import traceback
import time
import uuid
import wave
from disc_input import Disc
import mdec

VERSION = 'section1-ps1-demo-1'
EXECUTABLE_SHA256 = 'f424660920764ac70f697b5b909a42fe259a76e60acbf5b01c6ca3ee8c8f8e98'


def publish_status(temporary, destination, text):
    # Status is advisory: a transient reader/virus-scanner lock must not abort
    # extraction or roll back an otherwise valid import.
    try:
        temporary.write_text(text, encoding='utf-8')
        for attempt in range(4):
            try:
                os.replace(temporary, destination)
                return
            except PermissionError:
                if attempt < 3:
                    time.sleep(0.025)
    except OSError:
        pass


def status(root, percent, message):
    temporary = root / 'import_status.tmp'
    publish_status(temporary, root / 'import_status.json', json.dumps(dict(percent=percent, message=message)))
    progress = root / 'import_progress.tmp'
    publish_status(progress, root / 'import_progress.txt', str(percent) + '\n' + message)
    print(message, flush=True)


def safe_remove(root, path):
    path = path.resolve()
    if path.parent != root or not path.name.startswith(('.disc-import-', 'GameData.previous-')):
        raise ValueError('Refusing to remove a folder outside the demo import workspace.')
    shutil.rmtree(path)


def audio_bytes(disc, track, chunk_frames=256):
    if disc.chd:
        for first in range(0, track['frames'], chunk_frames):
            yield b''.join(disc.sector(track, n) for n in range(first, min(track['frames'], first + chunk_frames)))
    else:
        with track['file'].open('rb') as source:
            source.seek(track['start'] * 2352)
            remaining = track['frames'] * 2352
            while remaining:
                data = source.read(min(remaining, chunk_frames * 2352))
                if not data:
                    raise ValueError('An audio track is truncated.')
                remaining -= len(data)
                yield data


def music(disc, track, output):
    chunks = iter(audio_bytes(disc, track))
    first = next(chunks)
    if disc.chd:
        big = True  # Raw CD audio in CHD is in CD (big-endian) sample order.
    else:
        sample = first[:44100 * 4 * 3]
        scores = []
        for endian in ('little', 'big'):
            values = [int.from_bytes(sample[i:i + 2], endian, signed=True) for i in range(0, len(sample), 2)]
            scores.append(sum((values[i] - values[i - 2]) ** 2 for i in range(2, len(values)))
                          / max(1, sum(x * x for x in values)))
        big = scores[1] < scores[0]
    with wave.open(str(output), 'wb') as target:
        target.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
        import itertools
        for data in itertools.chain((first,), chunks):
            if big:
                samples = array.array('h')
                samples.frombytes(data)
                samples.byteswap()
                data = samples.tobytes()
            target.writeframesraw(data)


def install(image, root, engine_check=True):
    root = Path(root).absolute()
    if root.is_symlink() or root.is_junction():
        raise ValueError('The demo data folder must be a real folder beside the launcher.')
    root.mkdir(parents=True, exist_ok=True)
    root = root.resolve()
    tools = Path(__file__).resolve().parent
    disc = None
    staging = None
    previous = None
    installed = False
    try:
        status(root, 2, 'Reading disc image...')
        disc = Disc(image, tools / 'libchdr.dll')
        executable = disc.file('SLUS_000.07')
        if hashlib.sha256(executable).hexdigest() != EXECUTABLE_SHA256:
            raise ValueError('Unsupported disc release. This demo requires Alien Trilogy NTSC-U, SLUS-00007, matching the supported executable revision.')
        missions = disc.file('LANGUAGE/MISSIONU.TXT').decode('ascii')
        codes = [code.replace('.', '') for code in re.findall(r'^(\d\.\d\.\d) ', missions, re.M)]
        if len(codes) != 35:
            raise ValueError('Unexpected mission table in disc image.')
        tracks = struct.unpack_from('<35H', executable, 0x63974)
        sound_ids = struct.unpack_from('<35H', executable, 0x6392c)
        sound_map = {}
        for code, index in zip(codes, sound_ids):
            pointer = struct.unpack_from('<I', executable, 0x5adf8 + index * 36)[0]
            offset = pointer - 0x80010000 + 2048
            end = executable.index(0, offset)
            sound_map[code] = executable[offset:end].decode('ascii').lstrip('\\').split(';')[0].replace('\\', '/')
        needed_sounds = {'SFX/PERMSFX.BIN'} | {sound_map[code] for code in codes[:12]}
        needed_tracks = {2} | set(tracks[:12])
        available_tracks = {t['track']: t for t in disc.tracks if t['mode'] == 'AUDIO'}
        if not needed_tracks <= available_tracks.keys():
            raise ValueError('Required CD audio tracks are missing. Keep every BIN referenced by the CUE beside it, or choose a complete raw-CD CHD.')
        expected = json.loads((tools / 'supported_files.json').read_text())
        selected = [name for name in expected if name in ('SLUS_000.07', 'SYSTEM.CNF')
                    or name.startswith(('GFX/', 'LANGUAGE/', 'NME/', 'SECT11/', 'SECT12/'))
                    or name in needed_sounds]
        staging = Path(tempfile.mkdtemp(prefix='.disc-import-', dir=root))
        game = staging / 'GameData'
        cd = game / 'CD'
        ps1 = game / 'PS1'
        for i, name in enumerate(selected):
            status(root, 5 + i * 30 // len(selected), 'Checking and extracting ' + name)
            data = disc.file(name)
            if len(data) != expected[name]['size'] or hashlib.sha256(data).hexdigest() != expected[name]['sha256']:
                raise ValueError('Disc file does not match the supported release: ' + name)
            destination = cd / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            alias = ps1 / 'CD' / name
            alias.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.link(destination, alias)
            except OSError:
                shutil.copyfile(destination, alias)
        (ps1 / 'level_music.txt').write_text(''.join(f'{code} {track}\n' for code, track in zip(codes, tracks)))
        (ps1 / 'level_sfx.txt').write_text(''.join(f'{code} {sound_map[code]}\n' for code in codes[:12]))
        decoded = ps1 / 'Decoded'
        decoded.mkdir()
        for name in ('COLONY', 'BACKDROP'):
            status(root, 38, 'Decoding original ' + name.lower() + ' background...')
            (decoded / (name + '.rgba')).write_bytes(mdec.decode((cd / 'GFX' / (name + '.BS')).read_bytes()))
        audio = ps1 / 'Music'
        audio.mkdir()
        for i, number in enumerate(sorted(needed_tracks)):
            status(root, 45 + i * 43 // len(needed_tracks), f'Extracting CD music track {number}...')
            music(disc, available_tracks[number], audio / f'track{number:02}.wav')
        target = root / 'GameData'
        if target.is_symlink() or target.is_junction():
            raise ValueError('The existing game-data folder cannot be a link.')
        if target.exists():
            previous = root / ('GameData.previous-' + uuid.uuid4().hex)
            target.rename(previous)
        game.rename(target)
        installed = True
        if engine_check:
            status(root, 94, 'Checking the PS1-only game runtime...')
            engine = root / 'decompilation/cpp/build/demo_engine.exe'
            result = subprocess.run([str(engine), '--import-check'], cwd=root,
                                    creationflags=subprocess.CREATE_NO_WINDOW, timeout=90)
            if result.returncode:
                detail = root / 'decompilation/reports/exploration-error.txt'
                raise ValueError('Game-data check failed: ' + (detail.read_text() if detail.exists() else str(result.returncode)))
        ready = dict(version=VERSION, disc_executable_sha256=EXECUTABLE_SHA256,
                     section=1, source=Path(image).name, imported_files=len(selected))
        temporary = root / 'ready.tmp'
        temporary.write_text(json.dumps(ready, indent=2))
        os.replace(temporary, root / 'ready.json')
        if previous:
            safe_remove(root, previous)
            previous = None
        status(root, 100, 'Section 1 is ready. Choose PLAY.')
        return ready
    except Exception as error:
        if installed and previous:
            rejected = root / ('GameData.previous-' + uuid.uuid4().hex)
            (root / 'GameData').rename(rejected)
            previous.rename(root / 'GameData')
            previous = None
            safe_remove(root, rejected)
        (root / 'import_error.txt').write_text(str(error), encoding='utf-8')
        status(root, 0, str(error))
        raise
    finally:
        if disc:
            disc.close()
        if staging and staging.exists():
            safe_remove(root, staging)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--disc', required=True)
    parser.add_argument('--output', required=True)
    arguments = parser.parse_args()
    try:
        install(arguments.disc, arguments.output)
    except Exception:
        traceback.print_exc()
        sys.exit(1)
