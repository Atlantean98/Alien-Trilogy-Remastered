"""Compare PS1 egg links and linked enemy records with the playable map data."""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def records(path):
    data = path.read_bytes()
    offset = 12
    while offset + 8 <= len(data):
        size = int.from_bytes(data[offset + 4:offset + 8], 'big')
        if data[offset:offset + 4] == b'MAP0':
            data = data[offset + 8:offset + 8 + size]
            break
        offset += 8 + size + (size & 1)
    else:
        raise ValueError(f'Missing MAP0: {path}')
    word = lambda at: struct.unpack_from('<H', data, at)[0]
    base = 36 + word(0) * 8 + word(2) * 20 + word(4) * word(6) * 16 + data[12] * 8
    return [data[base + i * 20:base + (i + 1) * 20] for i in range(word(14))]


levels = []
for ps1 in sorted((ROOT / 'GameData/PS1/CD').glob('*/*.MAP')):
    pc = ROOT / 'GameData/CD' / ps1.parent.name / ps1.name
    original, playable = records(ps1), records(pc)
    egg_links = lambda rows: [(i, row[6]) for i, row in enumerate(rows) if row[0] == 1]
    levels.append({
        'level': ps1.stem,
        'ps1_eggs': len(egg_links(original)),
        'egg_links_match_pc': egg_links(original) == egg_links(playable),
        'linked_records_match': all(row[6] == 255 or row[6] < len(playable)
                                   and original[row[6]] == playable[row[6]]
                                   for row in original if row[0] == 1),
    })
report = {
    'maps': len(levels),
    'all_egg_links_match': all(row['egg_links_match_pc'] for row in levels),
    'all_linked_records_match': all(row['linked_records_match'] for row in levels),
    'levels': levels,
}
(ROOT / 'decompilation/reports/egg_ps1_comparison.json').write_text(json.dumps(report, indent=2))
assert report['maps'] == 36 and report['all_egg_links_match'] and report['all_linked_records_match']
print('All 36 PS1 maps match the playable egg links and linked enemy records.')
