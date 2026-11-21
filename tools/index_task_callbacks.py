"""Index direct task registrations; never propagate constants across branches/calls."""
import csv
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
lines=(ROOT/'decompilation/raw/TRILOGY.asm').read_text().splitlines()
rows=[]
def constant_before(index,register):
    aliases={'EAX':{'EAX','AX','AL','AH'},'EDX':{'EDX','DX','DL','DH'}}[register]
    for line in reversed(lines[max(0,index-20):index]):
        address,instruction=line.split('  ',1)
        op,*rest=instruction.split(' ',1)
        if op.startswith('J') or op in ('CALL','RET','RETF','LOOP'): return ''
        if not rest: continue
        args=rest[0].split(',')
        if args[0] not in aliases: continue
        if op=='MOV' and args[0]==register and len(args)==2 and re.fullmatch('0x[0-9a-f]+',args[1]): return f'{int(args[1],16):08x}'
        if op=='XOR' and args==[register,register]: return '00000000'
        # CMP/TEST do not overwrite their first operand.
        if op not in ('CMP','TEST','PUSH'): return ''
    return ''
for i,line in enumerate(lines):
    if line.endswith('CALL 0x00012364'):
        rows.append(dict(callsite=line.split()[0],priority=constant_before(i,'EAX'),callback=constant_before(i,'EDX')))
out=ROOT/'decompilation/reports'
with (out/'task_registrations.tsv').open('w',newline='') as file:
    writer=csv.DictWriter(file,fieldnames=['callsite','priority','callback'],delimiter='\t')
    writer.writeheader();writer.writerows(rows)
candidates=sorted({row['callback'] for row in rows if row['callback'] and 0x10000<=int(row['callback'],16)<0x7efcf})
(out/'task_callback_seeds.txt').write_text(''.join(a+'\n' for a in candidates))
print(f'{len(rows)} direct CreateTask calls; {len(candidates)} distinct constant callback targets. Blank fields remain unresolved.')
