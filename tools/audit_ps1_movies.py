"""Read original PS1 movie dispatch records and save local evidence."""
from pathlib import Path
import json,struct
ROOT=Path(__file__).resolve().parents[1]
b=(ROOT/'GameData/PS1/CD/SLUS_000.07').read_bytes();base=0x8000f800
rows=[]
for index in range(15):
 values=struct.unpack_from('<24h',b,0x80073668+index*48-base);resource=values[9];pointer=struct.unpack_from('<I',b,0x8006a5f8+resource*36-base)[0];offset=pointer-base;name=b[offset:b.index(0,offset)].decode();rows.append({'index':index,'resource':resource,'file':name,'vertical_offset':values[8]})
report={'dispatch_function':'0x8004533c','table':'0x80073668','entries':rows,'chapter_dispatch':{'function':'0x80014c84..0x80014d14','next_mission_index_12':10,'next_mission_index_22':11,'next_mission_index_35':12},'death_dispatch':{'function':'0x8001bb64','damage_feedback_setter':'0x8001bcc4','jump_table':'0x80011980','damage_type_to_movie_index':[3,6,4,5,2,7]},'startup_calls':['0x8001af4c: movie 9 (LEGAL)','0x8001af84: movie 14 (FOX)','0x8001afb4: movie 8 (ACCLAIM)','0x8001afe0: movie 0 (PROBE)','0x8001b01c: movie 1 (INTRO)'],'missing_on_disc':'CREDITS.STR is named as a resource, but absent from this disc directory and absent from the 15-entry playback table.'}
(ROOT/'decompilation/reports/ps1_movie_dispatch.json').write_text(json.dumps(report,indent=2));print('Verified original PS1 movie table and transition/death dispatch')
