"""Full original enemy constructor against native C++ across species and pool paths."""
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
o.dll.enemy_construction_probe.argtypes=[C.POINTER(U8),U32,U32,C.POINTER(U32),C.POINTER(U32)];o.dll.enemy_construction_probe.restype=U32
windows=[(0x94000,0xd000),(0x22ce00,0x100),(0x234000,0x1000),(0x236300,0x400),(0x310000,0x1000),(0x320000,0x10000),(0x330000,0x2000),(0x340000,0x3000)]
rng=random.Random(0x2dfc8);effects=[];cases=0;counts={};POOL=0x320000;SPAWNS=0x330000;GRID=0x340000;TASKS=0x310100
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
def provider(uc,address,size,_):
    a=uc.reg_read(UC_X86_REG_EAX);b=uc.reg_read(UC_X86_REG_EDX);effects.extend([address,a,0 if address==0x2840c else b])
    if address==0x12364:
        t=0 if get(0x310800,1) else get(0x310804);put(0x310804,get(0x310804)+32);uc.reg_write(UC_X86_REG_EAX,t)
for addr in [0x12364,0x2ee40,0x2840c]:o.mu.hook_add(UC_HOOK_CODE,provider,begin=addr,end=addr);o.mu.mem_write(addr,b'\xc3');o.mu.ctl_remove_cache(addr,addr+32)
def setup(kind=2,index=0):
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    put(0x2365e4,POOL);put(0x2365d8,POOL);put(0x2365d4,SPAWNS);put(0x2365e0,0 if rng.randrange(2) else POOL+208)
    for i in range(128):put(POOL+i*208+200,POOL+(i+1)*208 if i<127 else 0);put(POOL+i*208+0x70,i,1)
    put(0x310800,0,1);put(0x310804,TASKS);put(0xa0c92,0,1);put(0x96234,rng.randrange(4),2);put(0x9e0a8,rng.randrange(2),1)
    put(0xa0c30,16,2);put(0xa0c32,16,2);put(0xa0c34,4096,2);put(0xa0c36,4096,2);put(0xa0c40,GRID)
    for i in range(256):put(GRID+i*16+11,rng.randrange(30),1);put(GRID+i*16+13,rng.randrange(40,90),1)
    p=SPAWNS+index*20;put(p,kind,1);put(p+1,rng.randrange(16),1);put(p+2,rng.randrange(16),1)
    put(p+4,rng.randrange(12),1);put(p+5,rng.randrange(5),1)
    return p
def compare(index=0):
    global cases
    for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    effects.clear();expected=o.run(0x2dfc8,index,instruction_limit=1000000)
    log=(U32*256)();count=U32();actual=o.dll.enemy_construction_probe(native,SIZE,index,log,C.byref(count))
    assert actual==expected,('return',cases,hex(actual),hex(expected))
    assert list(log)[:count.value]==effects,('calls',cases,list(log)[:count.value],effects)
    for addr,n in windows:
        have=C.string_at(base+addr,n);want=bytes(o.mu.mem_read(addr,n))
        assert have==want,('memory',cases,[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
    cases+=1
start=cases
for kind in list(range(20))+[255]:
    for i in range(70):setup(kind,i%16);compare(i%16)
counts['all_species_fields_tasks_animation_calls_and_list_insertion']=cases-start
start=cases
for i in range(100):
    setup(rng.randrange(20));put(0xa0c92,2 if i%2 else 0,1);put(0x2365d8,POOL if i%2 else 0);compare()
for kind in [10,16,17,18,19]:
    for i in range(30):setup(kind);put(0x310800,1,1);compare()
counts['demo_gates_empty_entity_pool_and_guarded_task_exhaustion']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,recovered_original_entries=['0x2dfc8'],
    limitations=['Task allocation, animation selection and sprite release are observed providers in this isolated constructor suite.', 'The original unguarded null-task dereference paths are excluded; guarded exhaustion paths are checked.', 'Full AI decision/movement/attack and sprite resource loading remain unfinished.'])
(o.ROOT/'decompilation/reports/enemy_construction_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
