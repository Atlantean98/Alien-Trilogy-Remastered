"""Projectile movement/update and entity hits versus original x86 instructions."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX
mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.simulation_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
dll.simulation_probe.restype=U32
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x22c000,0x2000),(0x234000,0x4000),(0x310000,0x1000),(0x320000,0x6000),(0x330000,0x2000),(0x340000,0x6000),(0x3f9000,0x600)]
rng=random.Random(0xA11E71C)
services={0x30518:1,0x30ac0:1,0x51360:2,0x560a4:1,0x2a6e0:2,
    0x362a8:2,0x2a3e0:3,0x2a684:1,0x40db0:1,0x41044:1,
    0x2d004:1,0x3dee8:2,0x3df14:3,0x5287c:0,0x44fa0:1,0x2a4e8:2,0x404f8:3}
effects=[]
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(mu.mem_read(a,size),'little')
def hook(uc,address,size,_):
    args=[uc.reg_read(r) for r in [UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX]]
    args[services[address]:]=[0]*(3-services[address]);effects.extend([address,*args])
    if address==0x30518:put(args[0]+0x6f,6,1)
    if address==0x30ac0:put(args[0]+0x6f,1,1)
for address in services:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
POOL=0x320000;OBJECTS=0x322000;GRID=0x330000;ENTITIES=0x340000;TASK=0x310000
entries=[0x46c18,0x30dc4,0x2a860,0x29f18,0x2a1e8,0x29bc8,0x2d234,0x2d234,0,0x29b50]
cases=0;counts={}
def compare(op,a=0,b=0,c=0,d=0):
    global cases
    for start,length in windows:C.memmove(base+start,bytes(mu.mem_read(start,length)),length)
    effects.clear();mu.reg_write(UC_X86_REG_EBX,c);mu.reg_write(UC_X86_REG_ECX,d)
    if op==8:
        run(0x2ae64,a,b,instruction_limit=1000000);expected=run(0x2d234,TASK,instruction_limit=1000000)
    else:expected=run(entries[op],a,b,instruction_limit=1000000)
    log=(U32*8192)();count=U32();actual=dll.simulation_probe(native,SIZE,op,a,b,c,d,log,C.byref(count))
    if op in [3,4,5]:assert actual==expected,('return',op,a,b,hex(actual),hex(expected))
    for start,length in windows:
        want=bytes(mu.mem_read(start,length));have=C.string_at(base+start,length)
        assert have==want,('memory',op,a,b,[(hex(start+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:15])
    assert list(log)[:count.value]==effects,('effects',op,a,b,list(log)[:count.value],effects)
    cases+=1

def setup(n=1):
    mu.mem_write(0x235700,bytes(0x1000));mu.mem_write(POOL,bytes(0x6000));mu.mem_write(ENTITIES,bytes(0x6000));mu.mem_write(GRID,bytes(0x2000))
    put(0x236304,POOL,4);put(0x236300,POOL if n else 0,4);put(0x236308,POOL+n*44 if n<128 else 0,4)
    put(0x2367fc,OBJECTS,4);put(0x2365e0,0,4)
    for i in range(128):
        p=POOL+i*44;pos=0x235700+i*8;vel=0x235f00+i*8
        put(p,pos,4);put(p+4,vel,4);put(p+12,GRID+(5*16+5)*16,4)
        put(p+0x1a,16);put(p+0x18,24);put(p+0x1c,20,1);put(p+0x1d,20,1);put(p+0x1e,0,1);put(p+0x20,0,1)
        put(p+0x24,POOL+(i+1)*44 if i+1<n else 0,4);put(p+0x28,POOL+(i-1)*44 if 0<i<n else 0,4)
        put(pos,2800);put(pos+2,1000);put(pos+4,2800)
    for i in range(n,127):put(POOL+i*44+0x24,POOL+(i+1)*44,4)
    for i in range(128):put(OBJECTS+i*40+0x1a,100)
    put(TASK+24,0x310100,4);put(0x22cec0,0x12345678,4)
    put(0xa0c30,16);put(0xa0c32,16);put(0xa0c34,0);put(0xa0c36,0);put(0xa0c40,GRID,4);put(0xa0c80,0,4)
    for i in range(512):put(GRID+i*16+11,4,1);put(GRID+i*16+13,80,1);put(GRID+i*16+8,1)
    put(0x9e0a8,0,1);put(0x9e0a9,0,1);put(0x95f9c,1,1)
    put(0xa0a48,6000);put(0xa0a4a,128);put(0xa0a4c,6000)
    for i in range(6):put(0xa09b0+i*4,rng.getrandbits(32),4)
    put(0x3f94f4,99,4)
    for i in range(8):
        e=ENTITIES+i*208;cell=0x345000+i*16;put(e,2800);put(e+2,1000);put(e+4,2800)
        for offset in [8,10,12]:put(e+offset,256)
        put(e+0x58,100);put(e+0x6f,0,1);put(e+0x74,2,1);put(e+0x78,cell,4);put(cell+8,1)
        put(e+0xc8,ENTITIES+(i+1)*208 if i<7 else 0,4)
        peer=i*60;put(0x3f92d0+peer,i,4);put(0x3f9308+peer,i,4);put(0x3f92fc+peer,0,4)
        put(0x3f92f4+peer,2800,4);put(0x3f92f8+peer,2800,4)

for _ in range(600):
    setup();mu.mem_write(0x235700,rng.randbytes(1024));mu.mem_write(0x235f00,rng.randbytes(1024))
    compare(0,0x235700,0x235f00,rng.choice([0,1,2,16,128]),rng.choice([0,1,2048,4096,8192,0xffffffff,0x80000000]))
counts['vector_batch_movement']=cases
start=cases
for _ in range(1400):
    setup();e=ENTITIES
    put(e+0x4e,rng.choice([0,0,1]));put(e+0x6f,rng.randrange(8),1);put(e+0x74,rng.randrange(17),1)
    put(e+0x58,rng.choice([0,1,25,26,32767,32768,65535,rng.getrandbits(16)]))
    put(0x9e0a8,rng.randrange(2),1);put(0x236318,rng.randrange(8),1)
    compare(1,e,rng.choice([0,1,16,100,32767,32768,65535]),rng.choice([0,1,65535]))
for kind in range(256):setup();put(ENTITIES+0x74,kind,1);compare(2,ENTITIES,POOL)
counts['entity_damage_and_hit_effects']=cases-start
start=cases
for _ in range(1300):
    setup();put(0x2365e0,ENTITIES,4);put(0x9e0a8,rng.randrange(2),1)
    p=POOL;put(p+0x1e,rng.randrange(23),1);put(p+0x1c,rng.randrange(256),1);put(p+0x1d,rng.randrange(256),1)
    put(p+0x1a,rng.getrandbits(16));put(p+0x20,rng.choice([0,0,1]),1)
    for i in range(8):
        e=ENTITIES+i*208;put(e+0x6f,rng.randrange(8),1);put(e+0x74,rng.randrange(17),1)
        put(e+0x58,rng.choice([0,1,100,32767]));put(e+0x4e,rng.choice([0,0,1]))
        for offset in [0,2,4]:put(e+offset,rng.choice([-1,2500,2544,2800,3055,3056]))
        for offset in [8,10,12]:put(e+offset,rng.choice([-1,0,128,256,512]))
        put(0x345000+i*16+8,rng.choice([0,1,2,32769]))
        peer=i*60;put(0x3f92d0+peer,rng.choice([0,0xffffffff]),4);put(0x3f9308+peer,rng.choice([i,99]),4);put(0x3f92fc+peer,rng.choice([0,10]),4)
    compare(3,p)
counts['entity_hit_scan']=cases-start
start=cases
for _ in range(1200):
    setup();p=POOL;pos=get(p);put(p+0x1e,rng.choice([0,5,10,11]),1);put(0x9e0a8,rng.randrange(2),1)
    put(p+0x1a,rng.getrandbits(16));put(p+0x12,rng.getrandbits(16))
    for offset,player in [(0,0xa0a48),(2,0xa0a4a),(4,0xa0a4c)]:
        put(pos+offset,2800);put(player,2800+rng.choice([-1025,-1024,-256,-255,0,255,256,1024,1025]))
    compare(4,p)
for _ in range(1300):
    setup();p=POOL;pos=get(p);cell=get(p+12)
    put(pos+2,rng.choice([-32768,-1,0,127,128,512,768,1024,2560,2561,32767]))
    put(cell+10,rng.choice([0,6,8,9,20,49,50,51,52]),1);put(cell+12,rng.choice([0,20,21,23,25,28,29,254,255]),1)
    put(cell+4,rng.randrange(128),1);put(p+0x1a,rng.getrandbits(16));compare(5,p)
counts['player_hits_and_ballistic_map_hits']=cases-start
start=cases
for kind in list(range(23))+[255]:
    for _ in range(32):
        setup();p=POOL;pos=get(p);vel=get(p+4);cell=get(p+12)
        put(p+0x1e,kind,1);put(p+0x1c,rng.choice([0,1,2,3,11,12,19,20,25,255]),1)
        put(p+0x18,rng.choice([0,3,4,5,8,16,24,32767,32768]))
        put(pos+2,rng.choice([-100,0,127,128,129,160,192,512,768,1000,2048,2559,2560,3000]))
        put(vel,rng.randrange(-16,17));put(vel+2,rng.choice([-128,-10,-4,-2,0,1,4,5,10,64]));put(vel+4,rng.randrange(-16,17))
        put(0x9e0a8,rng.randrange(2),1);put(0x95f9c,rng.choice([0,1,1,2,3]),1)
        put(cell+10,rng.choice([0,6,8,9,20]),1);put(cell+12,rng.choice([0,0,21,25,28]),1)
        if rng.randrange(3)==0:put(0x2365e0,ENTITIES,4)
        if rng.randrange(4)==0:
            put(0xa0a48,2800);put(0xa0a4a,128);put(0xa0a4c,2800)
        compare(6,TASK)
counts['all_projectile_type_ticks']=cases-start
start=cases
# Strict hit-box edges, age-based damage, penetrating shots and all species.
for kind in [0,1,2,4,10,11,12,19]:
    for mode in [0,1]:
        for life in [9,10]:
            for offset in [-256,-255,0,255,256]:
                setup();put(0x2365e0,ENTITIES,4);put(POOL+0x1e,kind,1);put(POOL+0x1c,life,1);put(0x9e0a8,mode,1)
                for i in range(8):
                    put(ENTITIES+i*208+0x74,i+1,1);put(ENTITIES+i*208,2800+offset)
                    put(0x3f92f4+i*60,2800+offset,4)
                compare(3,POOL)
# Stable scenes deliberately reach floors, ceilings, impact and expiry branches.
for kind in list(range(23))+[255]:
    for scenario in ['clear','wall','enemy','player','floor','shallow','ceiling','expired','outside']:
        setup();pos=get(POOL);vel=get(POOL+4);cell=get(POOL+12);put(POOL+0x1e,kind,1);put(POOL+0x1c,25,1)
        if scenario=='wall':put(cell+10,20,1)
        if scenario=='enemy':put(0x2365e0,ENTITIES,4)
        if scenario=='player':put(0xa0a48,2800);put(0xa0a4a,128);put(0xa0a4c,2800)
        if scenario=='floor':put(pos+2,100);put(vel+2,-10)
        if scenario=='shallow':put(pos+2,140);put(vel+2,-2)
        if scenario=='ceiling':put(pos+2,2550);put(vel+2,10)
        if scenario=='expired':put(POOL+0x1c,1,1)
        if scenario=='outside':put(pos,10000)
        compare(6,TASK)
counts['directed_hit_and_tick_boundaries']=cases-start
start=cases
# Mixed active/free lists, saved-next traversal, repeated expiry and cursor routing.
for mode in [0,1]:
    for initial_tick in [0,1,2,3]:
        setup(24);put(0x9e0a8,mode,1);put(0x95f9c,initial_tick,1)
        for i in range(24):
            p=POOL+i*44;pos=get(p);vel=get(p+4)
            put(p+0x1e,i if i<23 else 255,1);put(p+0x1c,2+i,1)
            put(pos+2,1000+i*40);put(vel,(i%3-1)*8);put(vel+2,i%11-5);put(vel+4,(i%5-2)*8)
        for tick in range(30):compare(7,TASK)
# Spawned shots use recovered pool, constructor, movement and health handling.
for mode in [0,1]:
    for kind in [0,1,2,4]:
        setup(0);put(0x9e0a8,mode,1);put(0x2365e0,ENTITIES,4)
        for address,value in [(0xa0a20,2800),(0xa0a22,1096),(0xa0a24,2400),(0xa0ba0,0),(0xa0ba2,0),(0xa0ba4,0),(0xa0ba8,0)]:put(address,value)
        compare(8,100,kind)
        for _ in range(30):compare(7,TASK)
counts['mixed_lists_consecutive_ticks_and_spawn_to_hit']=cases-start
start=cases
for _ in range(500):
    setup();pos=get(POOL);vel=get(POOL+4)
    for offset in [0,2,4]:put(pos+offset,rng.getrandbits(16));put(vel+offset,rng.getrandbits(16))
    put(0xa0c34,rng.getrandbits(16));put(0xa0c36,rng.getrandbits(16));compare(9,POOL)
counts['impact_position_rollback']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Rendering, region triggers, blast area effects, player damage, enemy state transitions and networking are controlled service stubs.',
    'Recovered enemy health subtraction, hit selection, projectile motion and lifetime/list updates execute natively.',
    'Valid pointers, finite lists and allocated backing memory; not a whole-game playback or visual equivalence test.'])
(ROOT/'decompilation/reports/projectile_simulation_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
