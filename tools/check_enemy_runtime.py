"""Enemy population preparation, pool, facing/path/detection helpers versus x86."""
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
o.dll.enemy_runtime_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)];o.dll.enemy_runtime_probe.restype=U32
rng=random.Random(0x2ddc8);effects=[];cases=0;counts={}
windows=[(0x96000,0x400),(0xa0900,0x600),(0x22ce00,0x100),(0x22e900,0x300),(0x236300,0x400),(0x320000,0x10000),(0x330000,0x2000),(0x340000,0x1000),(0x350000,0x1000)]
POOL=0x320000;SPAWNS=0x330000;ENTITY=0x340000;PATHS=0x350000
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
def provider(uc,address,size,_):effects.extend([address,uc.reg_read(UC_X86_REG_EAX),uc.reg_read(UC_X86_REG_EDX) if address==0x2ee40 else 0])
for addr in [0x2dfc8,0x2ee40]:o.mu.hook_add(UC_HOOK_CODE,provider,begin=addr,end=addr);o.mu.mem_write(addr,b'\xc3');o.mu.ctl_remove_cache(addr,addr+32)
def setup():
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    put(0x2365e4,POOL);put(ENTITY+0x78,ENTITY+512);put(0xa0c50,PATHS)
    put(0x22cec0,0);put(0x96234,rng.randrange(4),2)
    for i in range(256):put(0x22e944+i*2,i%20,2)
def compare(op,a=ENTITY,b=0):
    global cases
    for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    effects.clear();expected=o.run([0x2ddc8,0x2debc,0x2ed40,0x2ee20,0x328d8,0x32d68,0x31cbc,0x30ecc,0x33430,0x33484][op],a,b,instruction_limit=1000000)
    log=(U32*4096)();count=U32();actual=o.dll.enemy_runtime_probe(native,SIZE,op,a,b,log,C.byref(count))
    if op in [1,6,8,9]:assert actual==expected,('return',op,hex(actual),hex(expected))
    assert list(log)[:count.value]==effects,('effects',op,list(log)[:count.value],effects)
    for addr,n in windows:
        have=C.string_at(base+addr,n);want=bytes(o.mu.mem_read(addr,n))
        assert have==want,('memory',op,cases,[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
    cases+=1
start=cases
for i in range(120):setup();compare(0)
for mode in [0,1,2,3,65535]:
    for kind in [1,2,7,19]:
        for health in [0,1,2,15,127,255]:setup();put(0x96234,mode,2);put(SPAWNS,kind,1);put(SPAWNS+5,health,1);compare(1,SPAWNS)
counts['enemy_pool_and_health_scaling']=cases-start
start=cases
for i in range(260):
    setup();n=i%129;put(0xa0c48,SPAWNS);put(0xa0c1a,n,2)
    for j in range(n):
        p=SPAWNS+j*20;put(p,j%20,1);put(p+7,j%4,1);put(p+8,j%3,1)
        put(0x22e944+(j%20)*2,rng.choice([2,5,6,j%20]),2)
    compare(2 if i%2 else 3,SPAWNS,n)
counts['remapped_level_population_rng_counters_and_spawn_dispatch']=cases-start
start=cases
for op in range(4,10):
    for i in range(700):setup();compare(op,ENTITY,rng.getrandbits(16))
counts['turning_paths_arrival_facing_visibility_and_detection']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    recovered_original_entries=['0x2ddc8','0x2debc','0x2ed40','0x2ee20','0x328d8','0x32d68','0x31cbc','0x30ecc','0x33430','0x33484'],limitations=['The full species constructor, AI decision/movement/attack callbacks and resource loading remain unfinished.', 'Spawn and animation entry calls are controlled providers in this batch; existing native combat reactions remain separate.'])
(o.ROOT/'decompilation/reports/enemy_runtime_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
