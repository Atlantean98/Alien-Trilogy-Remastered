"""Read-only audit of original secret-area IDs and destructible map geometry."""
from pathlib import Path
import json,struct
from audit_environment import map_data,ROOT

def main():
 rows=[]
 for path in sorted((ROOT/'GameData/CD').rglob('*.MAP')):
  d=map_data(path);w=lambda p:struct.unpack_from('<H',d,p)[0]
  grid_at=36+w(0)*8+w(2)*20;grid=d[grid_at:grid_at+w(4)*w(6)*16]
  objects_at=grid_at+len(grid)+d[12]*8+w(14)*20+w(16)*8
  scripts_at=objects_at+w(18)*16+w(20)*8+w(22)*16+d[28]*4+d[29]*4+d[13]*28
  slots=[struct.unpack_from('<hBB',d,scripts_at+i*4) for i in range(w(26))]
  commands=[struct.unpack_from('<HH',d,scripts_at+w(26)*4+i*4) for i in range(w(30))]
  walls=[]
  for i in range(w(18)):
   at=objects_at+i*16;r=d[at:at+16]
   if r[2] not in (21,28,29):continue
   targets=[]
   for slot in range(w(at+10),w(at+10)+w(at+12)):
    group,progress,required=slots[slot];targets.append(dict(slot=slot,group=group,progress=progress,required=required,program=[value for g,value in commands if g==group]))
   walls.append(dict(index=i,kind=r[2],x=r[0],z=r[1],width=r[8],height=r[9],health=r[4]*16,entrance='blast/smartgun' if r[2]==21 else 'shootable',animations=targets))
  lift_at=objects_at+w(18)*16+w(20)*8
  action_at=lift_at+w(22)*16;command_at=action_at+d[28]*4
  action_rows=[]
  for index in range(d[28]):
   a=d[action_at+index*4:action_at+index*4+4];pc=a[1];seen=set();chain=[]
   while pc!=255 and pc not in seen:
    if pc>=d[29]:raise ValueError(f'{path.stem}: invalid action command')
    seen.add(pc);c=d[command_at+pc*4:command_at+pc*4+4];chain.append(dict(opcode=c[0],delta=c[2],target=c[3]));pc=c[1]
   action_rows.append(dict(index=index,flags=a[0],budget=a[2],commands=chain,cells=[dict(x=n%w(4),z=n//w(4)) for n in range(len(grid)//16) if grid[n*16+15]==index] if index else []))
  b=path.read_bytes();at=12;model_ids=[]
  while at+8<=len(b):
   tag=b[at:at+4];size=int.from_bytes(b[at+4:at+8],'big');at+=8+size+(size&1)
   if tag[:1]==b'L' and tag[1:].isdigit():model_ids.append(int(tag[1:]))
  movers=[]
  for index in range(w(22)):
   r=d[lift_at+index*16:lift_at+(index+1)*16];base=grid[(r[1]*w(4)+r[0])*16+11]
   links=[dict(action=a['index'],flags=a['flags'],budget=a['budget'],delta=c['delta'],opcode=c['opcode']) for a in action_rows for c in a['commands'] if c['opcode'] in (5,7) and c['target']==index]
   movers.append(dict(index=index,x=r[0],z=r[1],width=r[2],height=r[3],low=base*32,high=((base+r[5])&255)*32,flags=r[7],required=r[10],progress=r[11],model=r[12],model_present=r[12] in model_ids,linked_actions=links))
  ids=sorted(set(grid[i+5] for i in range(0,len(grid),16))-{0})
  rows.append(dict(map=path.stem,secret_area_ids=ids,secret_cell_count=sum(bool(grid[i+5]) for i in range(0,len(grid),16)),breakables=walls,movers=movers,projectile_actions=[a for a in action_rows if a["flags"]&2]))
 report=dict(scope='Original MAP0 data; area IDs feed original Secrets assessment. Breakable objects can also be crates or mission barricades. This does not infer collision from visible walls.',maps=len(rows),breakables=sum(len(r['breakables']) for r in rows),movers=sum(len(r['movers']) for r in rows),projectile_actions=sum(len(r['projectile_actions']) for r in rows),levels=rows)
 out=ROOT/'decompilation/reports';(out/'original_secret_audit.json').write_text(json.dumps(report,indent=2)+'\n')
 lines=['# Original secret and breakable geometry audit','','All 46 physical maps, including lighting variants and extra PC maps, are included. IDs are the original Secrets assessment regions; breakable counts include non-secret crates and barricades. See original_secret_audit.json for coordinates, health, activation thresholds and exact animation command words. Runtime verification is in secret_level_audit.json.','','| Map | Secret area IDs | Secret cells | Breakable geometry objects | Movers | Shooting actions |','|---|---|---:|---:|---:|---:|']
 for r in rows:lines.append('| '+r['map']+' | '+(', '.join(map(str,r['secret_area_ids'])) or '-')+' | '+str(r['secret_cell_count'])+' | '+str(len(r['breakables']))+' | '+str(len(r['movers']))+' | '+str(len(r['projectile_actions']))+' |')
 lines+=['','No visible-wall-to-collision conversion is performed. Original walk-through walls and authored floor/ceiling data are retained. This audit and automated state-transition tests are not a manual campaign playthrough.']
 (out/'original_secret_audit.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
 print(f"Audited {len(rows)} maps and {report['breakables']} breakable geometry objects")
if __name__=='__main__':main()
