"""Connected enemy construction with original scheduler and animation programs."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
o.dll.enemy_integration_probe.argtypes=[C.POINTER(U8),U32,U32,U32];o.dll.enemy_integration_probe.restype=U32
windows=[(0x94000,0xd000),(0x22ce00,0x100),(0x234000,0x1000),(0x236300,0x400),(0x310000,0x2000),(0x35a000,0x1000),(0x320000,0x10000),(0x330000,0x2000),(0x340000,0x3000)]
rng=random.Random(0x2dfc8);effects=[];cases=0;counts={};POOL=0x320000;SPAWNS=0x330000;GRID=0x340000;TASKS=0x310100
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
animation_tables=bytes(o.mu.mem_read(0x9c000,0x1000))
def setup(kind=2,index=0):
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    put(0x2365e4,POOL);put(0x2365d8,POOL);put(0x2365d4,SPAWNS);put(0x2365e0,0 if rng.randrange(2) else POOL+208)
    for i in range(128):put(POOL+i*208+200,POOL+(i+1)*208 if i<127 else 0);put(POOL+i*208+0x70,i,1)
    put(0x310800,0,1);put(0x310804,TASKS);put(0xa0c92,0,1);put(0x96234,rng.randrange(4),2);put(0x9e0a8,rng.randrange(2),1)
    put(0xa0c30,16,2);put(0xa0c32,16,2);put(0xa0c34,4096,2);put(0xa0c36,4096,2);put(0xa0c40,GRID)
    for i in range(256):put(GRID+i*16+11,rng.randrange(30),1);put(GRID+i*16+13,rng.randrange(40,90),1)
    p=SPAWNS+index*20;put(p,kind,1);put(p+1,rng.randrange(16),1);put(p+2,rng.randrange(16),1)
    put(p+4,rng.randrange(12),1);put(p+5,rng.randrange(5),1)
    o.mu.mem_write(0x9c000,animation_tables)
    put(0x22ceb4,0x310000);o.run(0x12258)
    for resource in [0x234410,0x23440c,0x23442c,0x234408,0x234434,0x2343fc,0x234420,0x234428,0x234438,0x234430,0x234424,0x23443c]:put(resource,0x35a000)
    put(0x35a000,32,2)
    return p
def compare(index=0):
    global cases
    for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    effects.clear();expected=o.run(0x2dfc8,index,instruction_limit=1000000)
    log=(U32*256)();count=U32();actual=o.dll.enemy_integration_probe(native,SIZE,1,index)
    assert actual==expected,('return',cases,hex(actual),hex(expected))
    for addr,n in windows:
        have=C.string_at(base+addr,n);want=bytes(o.mu.mem_read(addr,n))
        assert have==want,('memory',cases,[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
    cases+=1
start=cases
for kind in list(range(20))+[255]:
    for i in range(40):setup(kind,i%16);compare(i%16)
counts['species_constructor_with_native_scheduler_and_original_animation_programs']=cases-start
start=cases
levels=[]
for item in json.loads((o.ROOT/'decompilation/reports/asset_inventory.json').read_text())['maps']:
    setup();put(0x9e0a8,0,1);put(0x96234,1,2)
    original=(o.ROOT/item['source']).read_bytes();payload=original[item['offset']:item['offset']+item['size']]
    o.mu.mem_write(0x100000,payload);put(0xa0bc8+0xa4,0x100000)
    o.run(0x27530,0xa0bc8,instruction_limit=20000000);o.run(0x27c68,0xa0bc8,instruction_limit=20000000)
    for kind in range(256):put(0x22e944+kind*2,kind,2)
    extra=[(0x100000,len(payload)),(0x22e944,512)]
    for addr,n in windows+extra:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    o.run(0x2ee20,instruction_limit=20000000)
    o.dll.enemy_integration_probe(native,SIZE,2,0)
    for addr,n in windows+extra:
        have=C.string_at(base+addr,n);want=bytes(o.mu.mem_read(addr,n))
        assert have==want,('level memory',item['source'],[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
    active=get(0x2365e0);seen=set()
    while active:
        assert active not in seen and POOL<=active<POOL+128*208
        seen.add(active);active=get(active+200)
    levels.append(dict(source=item['source'],spawn_records=get(0xa0c1a,2),initially_active_enemies=len(seen)))
    cases+=1
counts['all_46_levels_native_population_constructor_scheduler_and_animation']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,levels=levels,recovered_original_entries=[],
    limitations=['Sprite resource descriptors are supplied fixtures; original animation programs and frame-group tables are used unchanged.', 'Task callbacks are allocated but not executed in this construction integration test.'])
(o.ROOT/'decompilation/reports/enemy_integration_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
