"""Read raw CD CHDv5 images through the bundled BSD-licensed libchdr."""
import ctypes as C
import os
import re
import struct
from pathlib import Path


class Chd:
    def __init__(self, path, library):
        self.path = Path(path)
        header = self.path.open('rb').read(124)
        if len(header) != 124 or header[:8] != b'MComprHD' or int.from_bytes(header[12:16], 'big') != 5:
            raise ValueError('This demo supports raw-CD CHD version 5 images.')
        self.hunk_bytes, self.unit_bytes = struct.unpack_from('>II', header, 56)
        if self.unit_bytes != 2448 or not self.hunk_bytes or self.hunk_bytes > 16 * 1024 * 1024:
            raise ValueError('The CHD must contain raw CD sectors and audio, not an ISO/hard-disk image.')
        self.lib = C.CDLL(str(Path(library).resolve()))
        self.lib.chd_open_file.argtypes = [C.c_void_p, C.c_int, C.c_void_p, C.POINTER(C.c_void_p)]
        self.lib.chd_read.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p]
        self.lib.chd_get_metadata.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_void_p, C.c_uint32,
                                             C.c_void_p, C.c_void_p, C.c_void_p]
        self.lib.chd_close.argtypes = [C.c_void_p]
        self.lib.chd_error_string.argtypes = [C.c_int]
        self.lib.chd_error_string.restype = C.c_char_p
        self.crt = C.CDLL('ucrtbase')
        self.crt._wfopen.argtypes = [C.c_wchar_p, C.c_wchar_p]
        self.crt._wfopen.restype = C.c_void_p
        self.crt.fclose.argtypes = [C.c_void_p]
        self.file = self.crt._wfopen(str(self.path.resolve()), 'rb')
        if not self.file:
            raise OSError('Cannot open the selected CHD.')
        self.handle = C.c_void_p()
        self.check(self.lib.chd_open_file(self.file, 1, None, C.byref(self.handle)))
        self.buffer = C.create_string_buffer(self.hunk_bytes)
        self.cached = -1
        self.tracks = []
        frame = 0
        for index in range(99):
            text = C.create_string_buffer(512)
            error = self.lib.chd_get_metadata(self.handle, int.from_bytes(b'CHT2', 'big'), index,
                                               text, 512, None, None, None)
            if error:
                error = self.lib.chd_get_metadata(self.handle, int.from_bytes(b'CHTR', 'big'), index,
                                                   text, 512, None, None, None)
            if error:
                break
            fields = dict(re.findall(r'(\w+):([^ ]+)', text.value.decode('ascii')))
            frames = int(fields['FRAMES'])
            stored_gap = int(fields.get('PREGAP', 0)) if fields.get('PGTYPE', '').startswith('V') else 0
            self.tracks.append(dict(track=int(fields['TRACK']), mode=fields['TYPE'],
                                    start=frame + stored_gap, frames=frames - stored_gap,
                                    index01=0, file=str(self.path)))
            frame += (frames + 3) // 4 * 4
        if not self.tracks or self.tracks[0]['mode'] not in ('MODE2_RAW', 'MODE2'):
            raise ValueError('No supported PlayStation raw Mode 2 data track in this CHD.')

    def check(self, error):
        if error:
            raise ValueError('CHD error: ' + self.lib.chd_error_string(error).decode('ascii'))

    def sector(self, frame):
        offset = frame * self.unit_bytes
        hunk, within = divmod(offset, self.hunk_bytes)
        if hunk != self.cached:
            self.check(self.lib.chd_read(self.handle, hunk, self.buffer))
            self.cached = hunk
        if within + 2352 <= self.hunk_bytes:
            return self.buffer.raw[within:within + 2352]
        first = self.buffer.raw[within:]
        self.check(self.lib.chd_read(self.handle, hunk + 1, self.buffer))
        self.cached = hunk + 1
        return first + self.buffer.raw[:2352 - len(first)]

    def close(self):
        if self.handle:
            self.lib.chd_close(self.handle)
            self.handle = None
        if self.file:
            self.crt.fclose(self.file)
            self.file = None
