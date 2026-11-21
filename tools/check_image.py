"""Check this version's analysis image before trusting exported addresses."""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
image = ROOT / 'decompilation' / 'image'
manifest = json.loads((image / 'manifest.json').read_text())
source = ROOT / 'GameData' / 'TRILOGY.EXE'
expected = '588606092836c4a0b9e31bd89a885963cc6edc908e60789378ba23e3f109e666'
assert hashlib.sha256(source.read_bytes()).hexdigest() == manifest['sha256'] == expected
assert manifest['entry'] == 0x5fed8
objects = [(o, (image / o['file']).read_bytes()) for o in manifest['objects']]
for obj, data in objects:
    assert len(data) == obj['size']
    assert hashlib.sha256(data).hexdigest() == obj['sha256']
relocations = json.loads((image / 'relocations.json').read_text())
for record in relocations:
    obj, data = next((o, d) for o, d in objects if o['base'] <= record['source'] < o['base'] + o['size'])
    value = struct.unpack_from('<I', data, record['source'] - obj['base'])[0]
    assert record['type'] == 7 and value == record['target']
code = objects[0][1]
# Independently decoded first two fixup records from the LE record table.
assert struct.unpack_from('<I', code, 0xf39)[0] == 0x90490
assert struct.unpack_from('<I', code, 0xf4b)[0] == 0x9e120
# The entry starts with a jump over the compiler's runtime signature.
assert code[0x4fed8:0x4fee0] == b'\xeb\x76WATCOM'
assert objects[2][1][17 * 4096:] == bytes(len(objects[2][1]) - 17 * 4096)
print(f'Image checks passed: original unchanged, {len(objects)} objects, {len(relocations)} relocation records.')
