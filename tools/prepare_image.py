"""Prepare a relocated LE image for static analysis, never executing the game.

Supports only the page/fixup forms actually encountered; fails on others.
Format reference: Open Watcom bld/watcom/h/exeflat.h.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'decompilation' / 'image'


def prepare(path):
    data = path.read_bytes()
    def read(fmt, offset):
        return struct.unpack_from('<' + fmt, data, offset)[0]
    if data[:2] != b'MZ':
        raise ValueError('Expected MZ executable')
    header = read('I', 60)
    if data[header:header + 4] != b'LE\0\0':
        raise ValueError('Expected little-endian LE executable')
    u32 = lambda off: read('I', header + off)
    page_size, page_count = u32(0x28), u32(0x14)
    objects = []
    for i in range(u32(0x44)):
        size, base, flags, first, count, _ = struct.unpack_from('<6I', data, header + u32(0x40) + i * 24)
        objects.append(dict(index=i + 1, size=size, base=base, flags=flags,
                            first_page=first, page_count=count, contents=bytearray(size)))
    page_owners = {}
    for obj in objects:
        for local in range(obj['page_count']):
            logical = obj['first_page'] - 1 + local
            p = header + u32(0x48) + logical * 4
            physical = int.from_bytes(data[p:p + 3], 'big')
            flags = data[p + 3]
            if flags != 0:
                raise ValueError(f'Unsupported LE page flags {flags}')
            if not 1 <= physical <= page_count:
                raise ValueError('Invalid physical page')
            size = u32(0x2c) or page_size if physical == page_count else page_size
            size = min(size, obj['size'] - local * page_size)
            off = u32(0x80) + (physical - 1) * page_size
            chunk = data[off:off + size]
            if len(chunk) != size:
                raise ValueError('Truncated page')
            obj['contents'][local * page_size:local * page_size + size] = chunk
            page_owners[logical] = (obj, local)
    relocations = []
    counts = Counter()
    for page in range(page_count):
        start = read('I', header + u32(0x68) + page * 4)
        end = read('I', header + u32(0x68) + (page + 1) * 4)
        pos, limit = header + u32(0x6c) + start, header + u32(0x6c) + end
        obj, local = page_owners[page]
        while pos < limit:
            source, flags = data[pos:pos + 2]
            pos += 2
            # No guessing: reject imports, chains, additive or selector fixups.
            if source not in (7, 8) or flags not in (0, 0x10, 0x40, 0x50):
                raise ValueError(f'Unsupported fixup source={source:#x} flags={flags:#x} at {pos-2:#x}')
            source_offset = read('h', pos)
            pos += 2
            target_object = read('H' if flags & 0x40 else 'B', pos)
            pos += 2 if flags & 0x40 else 1
            target_offset = read('I' if flags & 0x10 else 'H', pos)
            pos += 4 if flags & 0x10 else 2
            if not 1 <= target_object <= len(objects):
                raise ValueError('Invalid fixup target object')
            target = objects[target_object - 1]['base'] + target_offset
            offset = local * page_size + source_offset
            if not 0 <= offset <= obj['size'] - 4:
                raise ValueError('Fixup outside object')
            address = obj['base'] + offset
            value = target if source == 7 else target - (address + 4)
            struct.pack_into('<I', obj['contents'], offset, value & 0xffffffff)
            relocations.append(dict(source=address, target=target, type=source))
            counts[f'{source:02x}/{flags:02x}'] += 1
        if pos != limit:
            raise ValueError('Fixup record exceeds page table boundary')
    entry = objects[u32(0x18) - 1]['base'] + u32(0x1c)
    OUT.mkdir(parents=True, exist_ok=True)
    for obj in objects:
        contents = obj.pop('contents')
        obj['file'] = f"object{obj['index']}.bin"
        (OUT / obj['file']).write_bytes(contents)
        obj['sha256'] = hashlib.sha256(contents).hexdigest()
    manifest = dict(source=str(path.relative_to(ROOT)), sha256=hashlib.sha256(data).hexdigest(),
                    le_header=header, entry=entry, page_size=page_size, page_count=page_count,
                    objects=objects, fixup_counts=dict(counts), fixup_records=len(relocations))
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (OUT / 'relocations.json').write_text(json.dumps(relocations, indent=2) + '\n')
    # Existing code references are research leads, not independently proven symbols.
    references = {}
    for file in sorted((ROOT / 'ALTEngine' / 'src').rglob('*')):
        if file.suffix not in ('.h', '.cpp'):
            continue
        for line, text in enumerate(file.read_text(encoding='utf-8-sig', errors='replace').splitlines(), 1):
            for match in re.finditer(r'\bFUN_([0-9a-fA-F]{8})\b', text):
                address = int(match[1], 16)
                references.setdefault(address, []).append(dict(file=str(file.relative_to(ROOT)), line=line))
    (OUT / 'legacy_references.json').write_text(json.dumps({f'{k:08x}': v for k,v in sorted(references.items())}, indent=2) + '\n')
    (OUT / 'seeds.txt').write_text(''.join(f'{addr:x}\n' for addr in sorted(references)
        if any(o['flags'] & 4 and o['base'] <= addr < o['base'] + o['size'] for o in objects)))
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    prepare(ROOT / 'GameData' / 'TRILOGY.EXE')
