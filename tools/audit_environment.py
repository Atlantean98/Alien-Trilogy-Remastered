"""Audit original MAP0 floor hazards and steam/flame emitter records, read-only."""
from pathlib import Path
from collections import Counter
import json,struct
ROOT=Path(__file__).resolve().parents[1]
def map_data(path):
 data=path.read_bytes();at=12
 while at+8<=len(data):
  size=int.from_bytes(data[at+4:at+8],'big')
  if at+8+size>len(data):raise ValueError('Truncated level chunk')
  if data[at:at+4]==b'MAP0':return data[at+8:at+8+size]
  at+=8+size+(size&1)
 raise ValueError('MAP0 missing')
def main():
 rows=[]
 for path in sorted((ROOT/'GameData/CD').rglob('*.MAP')):
  data=map_data(path);w=lambda at:struct.unpack_from('<H',data,at)[0]
  start=36+w(0)*8+w(2)*20;grid=data[start:start+w(4)*w(6)*16]
  counts=Counter(grid[i+10] for i in range(0,len(grid),16));at=start+len(grid)+data[12]*8;vents=[]
  for index in range(w(14)):
   p=at+index*20;raw=data[p:p+20]
   if 16<=raw[0]<=19:vents.append(dict(index=index,kind=raw[0],x=raw[1],z=raw[2],health=raw[5],height=raw[6],trigger=raw[7],threshold=raw[8],direction=raw[4],period=w(p+16),window_start=w(p+18),window_length=w(p+14)))
  rows.append(dict(level=path.stem,terrain={str(k):counts[k] for k in (7,8,10,12) if counts[k]},vents=vents))
 totals=Counter(v['kind'] for row in rows for v in row['vents']);report=dict(status='audited',physical_maps=len(rows),maps_with_hazards=sum(bool(r['terrain'] or r['vents']) for r in rows),vent_totals=dict(totals),levels=rows)
 out=ROOT/'decompilation/reports';(out/'environment_audit.json').write_text(json.dumps(report,indent=2))
 text=['# Environment hazard audit','', '| Physical map | Floor 7 (5 damage) | Floor 8 (10 damage) | Floor 10 (lethal) | Floor 12 (acid) | Steam vents | Flame vents |','|---|---:|---:|---:|---:|---:|---:|']
 for r in rows:
  c=Counter(v['kind'] for v in r['vents']);values=[r['level']]+[str(r['terrain'].get(str(k),0)) for k in (7,8,10,12)]+[str(c[16]+c[18]),str(c[17]+c[19])];text.append('| '+' | '.join(values)+' |')
 text+=['','All 46 physical maps are listed, including lighting variants and ten additional PC maps. Zero means no authored hazard of these types; alien corpses may create acid at runtime. The audit records native type codes rather than inferring hazards from texture colours. Triggered and initially inactive emitters are included. Full records and timing parameters are in environment_audit.json.']
 (out/'environment_audit.md').write_text('\n'.join(text)+'\n')
 print(f"Audited {len(rows)} maps: {report['maps_with_hazards']} have authored hazards; {sum(totals.values())} vents")
if __name__=='__main__':main()
