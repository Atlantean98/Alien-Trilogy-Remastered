"""Bounded, read-only BIN/CUE and raw-CD CHD input for the separate demo."""
from pathlib import Path
import re
from chd_reader import Chd


class Disc:
    def __init__(self, image, library):
        image = Path(image).resolve()
        self.chd = None
        self.files = {}
        self.entries = []
        self.seen = set()
        if image.suffix.lower() == '.chd':
            self.chd = Chd(image, library)
            self.tracks = self.chd.tracks
            self.data = self.tracks[0]
        else:
            if image.suffix.lower() == '.bin':
                matches = []
                for cue in image.parent.glob('*.cue'):
                    names = re.findall(r'^\s*FILE\s+"([^"]+)"\s+BINARY', cue.read_text(), re.I | re.M)
                    if any((cue.parent / name).resolve() == image for name in names):
                        matches.append(cue)
                if len(matches) != 1:
                    raise ValueError('Choose the matching CUE file. A BIN alone does not describe the audio tracks.')
                image = matches[0]
            if image.suffix.lower() != '.cue':
                raise ValueError('Choose a BIN/CUE disc image or raw-CD CHDv5. ISO images are not supported.')
            self.tracks = []
            source = None
            for line in image.read_text(encoding='utf-8-sig').splitlines():
                if match := re.match(r'\s*FILE\s+"([^"]+)"\s+BINARY\s*$', line, re.I):
                    source = (image.parent / match[1]).resolve()
                    if not source.is_relative_to(image.parent) or not source.is_file():
                        raise ValueError('A CUE track is missing or lies outside the disc-image folder.')
                elif match := re.match(r'\s*TRACK\s+(\d+)\s+(\S+)', line, re.I):
                    if source is None or len(self.tracks) >= 99:
                        raise ValueError('Invalid CUE track list.')
                    self.tracks.append(dict(track=int(match[1]), mode=match[2].upper(), file=source))
                elif match := re.match(r'\s*INDEX\s+(00|01)\s+(\d+):(\d+):(\d+)', line, re.I):
                    if not self.tracks or int(match[3]) >= 60 or int(match[4]) >= 75:
                        raise ValueError('Invalid CUE track index.')
                    self.tracks[-1]['index' + match[1]] = (int(match[2]) * 60 + int(match[3])) * 75 + int(match[4])
            if not self.tracks or self.tracks[0]['mode'] != 'MODE2/2352':
                raise ValueError('Expected a raw PlayStation MODE2/2352 data track.')
            for i, track in enumerate(self.tracks):
                if 'index01' not in track or track['file'].stat().st_size % 2352:
                    raise ValueError('Missing INDEX 01 or truncated raw track.')
                end = track['file'].stat().st_size // 2352
                if i + 1 < len(self.tracks) and self.tracks[i + 1]['file'] == track['file']:
                    end = self.tracks[i + 1].get('index00', self.tracks[i + 1].get('index01', 0))
                track['start'] = track['index01']
                track['frames'] = end - track['start']
                if track['frames'] <= 0:
                    raise ValueError('Invalid raw track length.')
            self.data = self.tracks[0]
        pvd = self.read(16, 2048)
        if pvd[1:6] != b'CD001':
            raise ValueError('No PlayStation ISO9660 filesystem in this image.')
        record = pvd[156:190]
        self.scan(int.from_bytes(record[2:6], 'little'), int.from_bytes(record[10:14], 'little'))
        self.lookup = {entry[0]: entry for entry in self.entries}

    def sector(self, track, frame):
        if frame < 0 or frame >= track['frames']:
            raise ValueError('Disc read outside track.')
        if self.chd:
            return self.chd.sector(track['start'] + frame)
        source = track['file']
        if source not in self.files:
            self.files[source] = source.open('rb')
        file = self.files[source]
        file.seek((track['start'] + frame) * 2352)
        data = file.read(2352)
        if len(data) != 2352:
            raise ValueError('Truncated disc sector.')
        return data

    def read(self, lba, size):
        if size < 0 or size > 32 * 1024 * 1024:
            raise ValueError('Invalid disc file extent.')
        output = bytearray()
        while size:
            sector = self.sector(self.data, lba)
            if sector[15] != 2 or sector[18] & 32:
                raise ValueError('Unsupported or damaged Mode 2 file sector.')
            count = min(size, 2048)
            output += sector[24:24 + count]
            size -= count
            lba += 1
        return bytes(output)

    def scan(self, lba, size, prefix='', depth=0):
        if depth > 12 or lba in self.seen or len(self.entries) > 4096:
            raise ValueError('Invalid disc directory tree.')
        self.seen.add(lba)
        data = self.read(lba, size)
        offset = 0
        while offset < len(data):
            count = data[offset]
            if not count:
                offset = (offset // 2048 + 1) * 2048
                continue
            record = data[offset:offset + count]
            offset += count
            if len(record) < 34 or 33 + record[32] > len(record):
                raise ValueError('Truncated disc directory record.')
            name = record[33:33 + record[32]]
            if name in (b'\0', b'\1'):
                continue
            name = name.decode('ascii').split(';')[0]
            if not re.fullmatch(r'[A-Z0-9_.-]+', name) or name in ('.', '..'):
                raise ValueError('Unsafe disc filename.')
            path = prefix + name
            start = int.from_bytes(record[2:6], 'little')
            length = int.from_bytes(record[10:14], 'little')
            if record[25] & 2:
                if name != 'STR':
                    self.scan(start, length, path + '/', depth + 1)
            else:
                self.entries.append((path, start, length))

    def file(self, path):
        if path not in self.lookup:
            raise ValueError('Required disc file is missing: ' + path)
        _, start, length = self.lookup[path]
        return self.read(start, length)

    def close(self):
        for file in self.files.values():
            file.close()
        if self.chd:
            self.chd.close()
