"""Original map initialization versus native C++, including all supplied MAP0s."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
o.dll.level_loading_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
o.dll.level_loading_probe.restype=U32
rng=random.Random(0x27530);effects=[];stubbed=False;cases=0;counts={};details=[]
STATE=0xa0bc8;PAYLOAD=0x100000;SYNTH=0x320000
windows=[(0xa0b00,0x200),(0x234780,0x180),(0x236e00,0x200),(0x310000,0x200),(SYNTH,0x20000)]
originals={a:bytes(o.mu.mem_read(a,8)) for a in [0x28860,0x41448]}
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
def callback(uc,address,size,_):
    if stubbed:effects.append(address)
for addr in originals:o.mu.hook_add(UC_HOOK_CODE,callback,begin=addr,end=addr)
def providers(stub):
    global stubbed
    stubbed=stub
    for addr,data in originals.items():o.mu.mem_write(addr,b'\xc3'+data[1:] if stub else data);o.mu.ctl_remove_cache(addr,addr+32)
def compare(op,a=STATE,b=0,extra=()):
    global cases
    for addr,n in windows+list(extra):C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    effects.clear()
    if op==8:o.run(0x27530,a,instruction_limit=20000000);expected_return=o.run(0x27c68,a,instruction_limit=20000000)
    else:expected_return=o.run([0x272f0,0x27530,0x27530,0x28810,0x413f0,0x28860,0x41448,0x27c68,0,0x277c8,0x27950][op],a,b,instruction_limit=20000000)
    log=(U32*64)();count=U32();actual_return=o.dll.level_loading_probe(native,SIZE,op,a,b,log,C.byref(count))
    if op in [9,10]:assert actual_return==expected_return,('height',op,a,b,actual_return,expected_return)
    assert list(log)[:count.value]==effects,('calls',op,list(log)[:count.value],effects)
    for addr,n in windows+list(extra):
        actual=C.string_at(base+addr,n);expected=bytes(o.mu.mem_read(addr,n))
        assert actual==expected,('memory',op,cases,[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y][:12])
    cases+=1
def reset():
    for addr,n in windows:o.mu.mem_write(addr,rng.randbytes(n))
    put(0xa0c20,0,2);put(0xa0c22,0,2)

inventory=json.loads((o.ROOT/'decompilation/reports/asset_inventory.json').read_text())
regions=[('vertices',0x84),('faces',0x7c),('collision_grid',0x78),('paths',0x88),('monsters',0x80),('pickups',0x8c),('objects',0x90),('doors',0x94),('lifts',0x98),('action_groups',0x9c),('logic_groups',0xa0),('lights',0xb0),('list_a',0xa8),('list_b',0xac)]
start=cases
for item in inventory['maps']:
    original=(o.ROOT/item['source']).read_bytes();payload=original[item['offset']:item['offset']+item['size']]
    assert hashlib.sha256(payload).hexdigest()==item['sha256'];assert len(payload)<0x130000
    for integrated in [False,True]:
        reset();o.mu.mem_write(PAYLOAD,payload);put(STATE+0xa4,PAYLOAD);providers(not integrated)
        compare(2 if integrated else 1,extra=[(PAYLOAD,len(payload))])
    reset();o.mu.mem_write(PAYLOAD,payload);put(STATE+0xa4,PAYLOAD);providers(False);compare(8,extra=[(PAYLOAD,len(payload))])
    for sample in range(10):
        x=rng.randrange(get(0xa0c30,2)*512);z=rng.randrange(get(0xa0c32,2)*512)
        compare(9,x,z,extra=[(PAYLOAD,len(payload))]);compare(10,x,z,extra=[(PAYLOAD,len(payload))])
    offsets=[(name,get(STATE+offset)-PAYLOAD) for name,offset in regions]
    end=offsets[-1][1]+get(STATE+0x72,2)*4
    assert all(0<=offset<=len(payload) for _,offset in offsets) and end<=len(payload),('bounds',item['source'],offsets,end,len(payload))
    destination=o.ROOT/'decompilation/assets/verified-level-layouts'/Path(item['source']).stem
    destination.mkdir(parents=True,exist_ok=True)
    sections=[]
    for i,(name,offset) in enumerate(offsets):
        limit=offsets[i+1][1] if i+1<len(offsets) else end
        raw=payload[offset:limit];path=destination/(name+'.bin');path.write_bytes(raw);assert path.read_bytes()==raw
        sections.append(dict(name=name,offset=offset,size=len(raw),file=path.relative_to(o.ROOT).as_posix()))
    header=payload[:36];(destination/'header.bin').write_bytes(header)
    assert (destination/'header.bin').read_bytes()+b''.join((o.ROOT/s['file']).read_bytes() for s in sections)+payload[end:]==payload
    detail=dict(source=item['source'],payload_sha256=item['sha256'],payload_size=len(payload),loader_end=end,unparsed_tail=len(payload)-end,regions=sections,
        runtime_counts={name:get(STATE+offset,2) for name,offset in [('vertices',0x4c),('faces',0x4e),('monsters',0x52),('pickups',0x54),('objects',0x56),('doors',0x58),('lifts',0x5a),('lights',0x74)]})
    (destination/'layout.json').write_text(json.dumps(detail,indent=2)+'\n');details.append(detail)
counts['all_46_maps_layout_grid_reset_door_lift_and_native_height_queries']=cases-start
start=cases
providers(True)
for i in range(350):
    reset();header=SYNTH;put(0x310000+0xa4,header)
    # Complete synthetic serialized record, with large counts to exercise the
    # distinction between serialized pointer offsets and runtime count caps.
    values={0:8,2:rng.randrange(1,15),4:rng.randrange(1,8),6:rng.randrange(1,8),8:12,10:17,12:rng.randrange(8),13:rng.randrange(129),14:rng.randrange(150),16:rng.randrange(150),18:rng.randrange(150),20:rng.randrange(80),22:rng.randrange(45),24:2048,26:rng.randrange(300),28:rng.randrange(100),29:rng.randrange(100),30:rng.randrange(30),32:rng.getrandbits(32)}
    for offset,value in values.items():put(header+offset,value,1 if offset in [12,13,28,29] else 4 if offset==32 else 2)
    faces=header+36+values[0]*8;grid=faces+values[2]*20
    for j in range(values[2]):
        for k in range(4):put(faces+j*20+k*4,rng.randrange(-1,8))
    for j in range(values[4]*values[6]):put(grid+j*16,rng.randrange(-1,values[2]));put(grid+j*16+13,0 if j%2 else rng.randrange(256),1)
    compare(1,0x310000)
counts['synthetic_map_offsets_count_caps_and_geometry_fixups']=cases-start
start=cases
providers(False)
for op,stride in [(3,8),(4,16)]:
    for i in range(300):
        reset();put(0xa0c30,16,2);put(0xa0c40,SYNTH);records=SYNTH+0x10000;n=i%65
        for j in range(n):put(records+j*stride,rng.randrange(16),1);put(records+j*stride+1,rng.randrange(16),1)
        compare(op,records,n)
for op in [5,6]:
    for i in range(60):
        reset();put(0xa0c30,16,2);put(0xa0c40,SYNTH);records=SYNTH+0x10000;n=i%33
        put(0xa0c20 if op==5 else 0xa0c22,n,2);put(0xa0c5c if op==5 else 0xa0c60,records)
        for j in range(n):put(records+j*(8 if op==5 else 16),rng.randrange(16),1);put(records+j*(8 if op==5 else 16)+1,rng.randrange(16),1)
        compare(op)
counts['door_lift_floor_sampling_wrapping_and_wrappers']=cases-start
start=cases
for i in range(200):
    reset();put(0x310000+0x78,SYNTH);put(0x310000+0x68,rng.randrange(17),2);put(0x310000+0x6a,rng.randrange(17),2);compare(7,0x310000)
counts['collision_cell_flags_region_max_and_region_table_reset']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    recovered_original_entries=['0x272f0','0x27530','0x28810','0x413f0','0x28860','0x41448','0x27c68'],maps=details,
    limitations=['Verified byte-level layout/relocation and door/lift floor initialization, not every entity/script field or whole-level gameplay.', 'Input buffers/counts must be valid; original out-of-bounds and nonterminating malformed-count cases are excluded by API preconditions.'])
(o.ROOT/'decompilation/reports/level_loading_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='maps'},indent=2))
