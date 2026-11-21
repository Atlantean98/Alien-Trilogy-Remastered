"""Read original MAP action chains to report door/panel dependencies (no data edits)."""
from pathlib import Path
import json, struct
ROOT=Path(__file__).resolve().parents[1]
def chunks(path):
    data=path.read_bytes(); result={}; offset=12
    assert data[:4]==b"FORM"
    while offset+8<=len(data):
        size=int.from_bytes(data[offset+4:offset+8],"big")
        assert offset+8+size<=len(data)
        result[data[offset:offset+4].decode()]=data[offset+8:offset+8+size]
        offset+=8+size+(size&1)
    return result
rows=[]
warnings=[]
for path in sorted((ROOT/"GameData/CD").rglob("*.MAP")):
    data=chunks(path)["MAP0"]
    word=lambda offset: struct.unpack_from("<H",data,offset)[0]
    vertices,faces,width,height=struct.unpack_from("<4H",data)
    offset=36+vertices*8+faces*20+width*height*16+data[12]*8+word(14)*20
    pickups=[list(data[offset+i*8:offset+i*8+8]) for i in range(word(16))];offset+=word(16)*8
    objects=[list(data[offset+i*16:offset+i*16+16]) for i in range(word(18))];offset+=word(18)*16
    doors=[list(data[offset+i*8:offset+i*8+8]) for i in range(word(20))];offset+=word(20)*8
    lifts=[list(data[offset+i*16:offset+i*16+16]) for i in range(word(22))];offset+=word(22)*16
    actions=[list(data[offset+i*4:offset+i*4+4]) for i in range(data[28])];offset+=data[28]*4
    logic=[list(data[offset+i*4:offset+i*4+4]) for i in range(data[29])]
    chains=[]
    for index,action in enumerate(actions):
        if not index or not action[0]&1 or not action[2]:continue
        pc=action[1];seen=set();commands=[]
        while pc!=255:
            if pc>=len(logic) or pc in seen:
                warnings.append(dict(level=path.stem,action=index,command=pc,reason="out of range" if pc>=len(logic) else "cycle"));break
            seen.add(pc);commands.append(logic[pc]);pc=logic[pc][1]
        chains.append((index,commands))
    panels=[]
    for index,obj in enumerate(objects):
        if obj[2] not in (22,24,26,27,32):continue
        links=[(action,commands) for action,commands in chains if any(c[0]==4 and c[3]==index for c in commands)]
        panels.append(dict(object=index,kind=obj[2],grid=obj[:2],requires_battery=obj[2] in (22,26),actions=[a for a,_ in links],door_targets=sorted({c[3] for _,commands in links for c in commands if c[0] in (1,6)}),lift_targets=sorted({c[3] for _,commands in links for c in commands if c[0] in (5,7)})))
    door_rows=[]
    for index,door in enumerate(doors):
        direct=[action for action,commands in chains if any(c[0]==6 and c[3]==index for c in commands) and not any(c[0]==4 for c in commands)]
        door_rows.append(dict(door=index,grid=door[:2],threshold=door[4],starts_open=bool(door[2]&64),direct_use_actions=direct,panel_objects=[p["object"] for p in panels if index in p["door_targets"]]))
    rows.append(dict(level=path.stem,panels=panels,doors=door_rows,lifts=len(lifts),batteries=[dict(rule=i,grid=p[:2],spawn_threshold=p[6]) for i,p in enumerate(pickups) if p[2]==7]))
report=dict(status="completed",chain_warnings=warnings,levels=rows,level_count=len(rows),panels=sum(len(r["panels"]) for r in rows),battery_panels=sum(p["requires_battery"] for r in rows for p in r["panels"]),doors=sum(len(r["doors"]) for r in rows),lifts=sum(r["lifts"] for r in rows),scope="Original data relationships, not full campaign playthrough verification.")
(ROOT/"decompilation/reports/door_panel_requirements.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
print({k:v for k,v in report.items() if k!="levels"})
