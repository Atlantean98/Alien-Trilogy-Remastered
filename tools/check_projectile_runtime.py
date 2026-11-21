"""Projectile pool, map collision and object damage versus original instructions."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX
mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.projectile_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
dll.projectile_probe.restype=U32
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x234000,0x4000),(0x320000,0x6000),(0x330000,0x2000)]
effects=[]
def hook(uc,address,size,_):
    effects.extend([address,uc.reg_read(UC_X86_REG_EAX) if address in [0x44fa0,0x2acac,0x51cc4] else 0])
for address in [0x44fa0,0x5287c,0x2acac,0x51cc4]:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
rng=random.Random(0xA11E700)
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(size*8))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(mu.mem_read(a,size),'little')
POOL=0x320000;OBJECTS=0x322000;GRID=0x330000
entries=[0x100ec,0x2a2bc,0x2a32c,0x2a2a4,0x2a348,0x2a36c,0x3687c,0x29d38,0x2ae64,0x2b4a4,0x2b4f0,0x2b548]
cases=0;counts={}
def compare(op,a=0,b=0):
    global cases
    for start,length in windows:C.memmove(base+start,bytes(mu.mem_read(start,length)),length)
    effects.clear()
    if op==8:put(0x60fff0-20-18,b>>16)
    expected=run(entries[op],a,b)
    log=(U32*128)();count=U32();actual=dll.projectile_probe(native,SIZE,op,a,b,log,C.byref(count))
    if op in [0,2,7,8]:assert actual==expected,('return',op,a,b,hex(actual),hex(expected))
    for start,length in windows:
        want=bytes(mu.mem_read(start,length));have=C.string_at(base+start,length)
        assert have==want,('memory',op,a,b,[(hex(start+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
    assert list(log)[:count.value]==effects,('effects',op,list(log)[:count.value],effects)
    cases+=1

def setup():
    mu.mem_write(POOL,rng.randbytes(0x6000));put(0x236304,POOL,4);put(0x2367fc,OBJECTS,4)
    for i in range(6):put(0xa09b0+i*4,rng.getrandbits(32),4)
    put(0x9e0a8,rng.randrange(2),1);put(0x9e0a9,rng.randrange(2),1)
    put(0xa0c30,16);put(0xa0c32,16);put(0xa0c34,0);put(0xa0c36,0);put(0xa0c40,GRID,4)
    grid=bytearray(0x2000)
    for i in range(512):grid[i*16+11]=4;grid[i*16+13]=80
    mu.mem_write(GRID,bytes(grid))

for _ in range(1000):setup();compare(0)
# Carry, wrap and sequential random draws.
for x in [0,1,0x7fffffff,0x80000000,0xfffffffe,0xffffffff]:
    for y in [0,1,0x7fffffff,0xffffffff]:
        setup();put(0xa09c0,x,4);put(0xa09b4,y,4)
        for _ in range(8):compare(0)
counts['random_state']=cases
start=cases
for _ in range(10):
    setup();compare(1)
    for i in range(129):compare(2)
    for i in range(128):compare(3,POOL+i*44)
    for i in range(128):
        p=get(0x236308);compare(2);compare(4,p)
    order=list(range(128));rng.shuffle(order)
    for i in order:compare(5,POOL+i*44);compare(3,POOL+i*44)
counts['pool_exhaustion_reuse_and_unlink']=cases-start
start=cases
for _ in range(1500):
    setup();index=rng.randrange(128);p=OBJECTS+index*40
    put(p+0x19,rng.choice([0,2,128,1,255]),1)
    put(p+0x1a,rng.choice([0,1,16,32767,32768,65535,rng.getrandbits(16)]))
    compare(6,index,rng.choice([0,1,16,32767,32768,65535,rng.getrandbits(16)]))
counts['map_object_damage']=cases-start
start=cases
for _ in range(2400):
    setup();p=POOL;pos=0x325000;put(p,pos,4)
    x=rng.randrange(512,15*512);z=rng.randrange(512,15*512);y=rng.choice([-32768,-1,0,255,256,511,512,767,768,1024,5120,32767,rng.randrange(-500,6000)])
    put(pos,x);put(pos+2,y);put(pos+4,z)
    cell=GRID+((z>>9)*16+(x>>9))*16;put(p+12,cell,4)
    put(cell+10,rng.choice(list(range(10))+[20]),1);put(cell+12,rng.choice([0,1,19,20,21,22,23,24,25,28,29,254,255]),1)
    put(cell+4,rng.randrange(128),1);put(p+0x1e,rng.randrange(8),1);put(p+0x1a,rng.getrandbits(16))
    target=OBJECTS+mu.mem_read(cell+4,1)[0]*40;put(target+0x19,0,1);put(target+0x1a,rng.getrandbits(16))
    compare(7,p)
counts['map_collision']=cases-start
start=cases
outcomes={}
for _ in range(1800):
    setup();compare(1)
    # Empty, available, or nonempty active lists.
    if rng.randrange(8)==0:put(0x236308,0,4)
    if rng.randrange(2):
        old=get(0x236308)
        if old:compare(2);compare(4,old)
    x=rng.choice([-100,0,511,512,7900,8192,8500,rng.randrange(8000)])
    z=rng.choice([-100,0,511,512,7900,8192,8500,rng.randrange(8000)])
    put(0xa0a20,x);put(0xa0a22,rng.choice([-32768,-1,128,768,1500,32767]));put(0xa0a24,z)
    for address in [0xa0ba0,0xa0ba2,0xa0ba4,0xa0ba8]:put(address,rng.randrange(4096))
    # Vary blocking and destructible objects throughout both spawn probes.
    for i in range(289):
        cell=GRID+i*16;put(cell+10,rng.choice([0,8,9,20,49,50,51,52]),1)
        put(cell+12,rng.choice([0,0,0,21,23,25,28,29,255]),1);put(cell+4,rng.randrange(128),1)
    for i in range(128):put(OBJECTS+i*40+0x19,0,1);put(OBJECTS+i*40+0x1a,100)
    compare(8,rng.getrandbits(16),rng.choice([0,1,2,3,4,5,255])|(rng.getrandbits(16)<<16))
counts['shared_shot_construction_and_pool_sequences']=cases-start
start=cases
for kind in [0,1,2,4,5,255]:
    for scenario in ['clear','first_hit','second_hit','outside','empty']:
        setup();compare(1)
        for address,value in [(0xa0a20,2800),(0xa0a22,1000),(0xa0a24,2800),(0xa0ba0,0),(0xa0ba2,0),(0xa0ba4,0),(0xa0ba8,0)]:put(address,value)
        if scenario=='outside':put(0xa0a20,10000)
        if scenario=='empty':put(0x236308,0,4)
        # With yaw zero, spawn and initial forward advance cross Z cells 5 -> 6.
        if scenario in ['first_hit','second_hit']:
            row=5 if scenario=='first_hit' else 6
            for x in range(16):put(GRID+(row*16+x)*16+10,20,1)
        compare(8,200,kind)
        assert bool(get(0x236300))==(scenario=='clear'),(kind,scenario)
# Original shot wrappers call the real native constructor through the adapter.
for op in [9,10,11]:
    for _ in range(100):
        setup();compare(1)
        for address,value in [(0xa0a20,2800),(0xa0a22,1000),(0xa0a24,2800),(0xa0ba0,0),(0xa0ba2,0),(0xa0ba4,0),(0xa0ba8,0),(0xa0a64,100),(0xa0abe,8),(0xa0ac2,8),(0xa0ac4,8)]:put(address,value)
        put(0x94b9c,rng.choice([0,32]),1)
        if rng.randrange(3)==0:put(0x236308,0,4)
        compare(op)
counts['directed_spawn_outcomes_and_integrated_wrappers']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Visual cleanup and network transmission are observed service stubs.',
    'Valid allocated records and map coordinates; no invalid-pointer or double-release contract is invented.',
    'This suite isolates pool/construction/map hits; moving-projectile ticks and enemy hits are checked in projectile_simulation_checks.json.',
    'Kind 3 reads an uninitialized original stack yaw; tests supply prior stack contents explicitly.'])
(ROOT/'decompilation/reports/projectile_runtime_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
