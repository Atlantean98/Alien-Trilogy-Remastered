"""Enemy pain/death, animation/knockback and blast propagation versus original x86."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX
mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.combat_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
dll.combat_probe.restype=None
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x22c000,0x2000),(0x234000,0x4000),(0x310000,0x1000),(0x320000,0x6000),(0x330000,0x2000),(0x340000,0x7000),(0x3f9000,0x600),(0x23ad00,0x60),(0x350000,0x10000)]
rng=random.Random(0xA11DEA7)
services={0x3b61c:1,0x560a4:1,0x51360:2,0x5287c:0,0x2a3e0:3,0x3dee8:2,
    0x2a6e0:2,0x2a684:1,0x40db0:1,0x41044:1,0x2d004:1,0x3df14:3,
    0x44fa0:1,0x2a4e8:2,0x404f8:3}
effects=[]
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(mu.mem_read(a,size),'little')
def hook(uc,address,size,_):
    args=[uc.reg_read(r) for r in [UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX]]
    args[services[address]:]=[0]*(3-services[address]);effects.extend([address,*args])
    if address==0x3b61c and get(0x310800,1):
        put(args[0]+0x72,255,1);put(args[0]+0x32,16);put(args[0]+0x6e,2,1)
        if get(0x310800,1)==2:put(args[0]+0x74,8,1)
for address in services:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
POOL=0x320000;OBJECTS=0x322000;GRID=0x330000;ENTITIES=0x340000;TASK=0x310000
entries=[0x46c9c,0x46d78,0x3298c,0x2ee40,0x30ac0,0x30518,0x362a8,0x30dc4,0x2d234]
cases=0;counts={}
def compare(op,a=0,b=0,c=0):
    global cases
    for start,length in windows:C.memmove(base+start,bytes(mu.mem_read(start,length)),length)
    effects.clear();mu.reg_write(UC_X86_REG_EBX,c);mu.reg_write(UC_X86_REG_ECX,0)
    run(entries[op],a,b,instruction_limit=1500000)
    log=(U32*16384)();count=U32();dll.combat_probe(native,SIZE,op,a,b,c,log,C.byref(count))
    for start,length in windows:
        want=bytes(mu.mem_read(start,length));have=C.string_at(base+start,length)
        assert have==want,('memory',op,a,b,c,[(hex(start+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:15])
    assert list(log)[:count.value]==effects,('effects',op,a,b,c,list(log)[:count.value],effects)
    cases+=1

def basic_setup(n=1):
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


# Valid animation resources shared by all fixture entities, including signed
# direct selector indices -2/-1 and all byte-facing indices used by species 10.
for i in range(304):
    program=0x351000+i*32;put(0x350000+i*8,0x357000,4);put(0x350004+i*8,program,4)
    for j,value in enumerate([2,0,0x810d,0x8300,i%4,0x8600]):put(program+j*2,value)
for i in range(4):mu.mem_write(0x357000+i*12,bytes([i+1])*12)
def setup(n=1):
    basic_setup(n);put(0x2365e4,ENTITIES,4);put(0x310800,0,1)
    mu.mem_write(0xa0ae6,bytes(0x200));mu.mem_write(0x23ad00,rng.randbytes(0x60))
    for i in range(8):
        e=ENTITIES+i*208;cell=GRID+(5*16+5+i)*16
        put(e+0x78,cell,4);put(e+0x7c,0x350010,4);put(e+0xa0,0x356000+i*8,4);put(0x356000+i*8,30+i)
        put(e+0xc0,0x346000+i*16,4);put(0x34600c+i*16,0,4)
        put(e+0x72,255,1);put(e+0x54,700);put(e+0x5e,64);put(e+0x32,8)
    # basic_setup's region/global values are above a0ae6; restore them after
    # clearing the kill-counter window, whose high species overlap them.
    put(0xa0c30,16);put(0xa0c32,16);put(0xa0c34,0);put(0xa0c36,0);put(0xa0c40,GRID,4)
    put(0xa0c80,0,4)

for op in [0,1]:
    for _ in range(120):setup();compare(op)
for _ in range(500):
    setup();e=ENTITIES;put(e+0x32,rng.getrandbits(16));compare(2,e,rng.getrandbits(16),rng.getrandbits(16))
counts['transform_save_restore_and_knockback']=cases
start=cases
for animation in [-2,-1,0,1,3,4,8,9,10,11,13,15,16,17,25,271]:
    for flags in [0,0xff,0x30]:
        setup();put(ENTITIES+0xb6,flags,1);compare(3,ENTITIES,animation)
counts['entity_animation_selection']=cases-start
start=cases
for kind in range(256):
    for op in [4,5]:
        setup();e=ENTITIES;put(e+0x74,kind,1);put(e+0x72,rng.choice([0,255]),1)
        put(e+0x32,rng.choice([-32768,-5,0,5,32767]));put(e+0x5e,rng.choice([0,64,255,32768,65535]))
        put(e+0x6e,rng.randrange(8),1);put(0x34600c,rng.choice([0,1,2,3,0xffffffff]),4)
        put(0x9e0a8,rng.randrange(2),1);put(get(e+0x78)+10,rng.choice([0,44,45,255]),1)
        put(e+0xb6,rng.randrange(256),1);compare(op,e)
for kind in range(1,15):
    for mode in [0,1]:
        for state in [0,1,2,3,0xffffffff]:
            for facing in [0,1,2,7]:
                setup();e=ENTITIES;put(e+0x74,kind,1);put(e+0x6e,facing,1);put(0x34600c,state,4);put(0x9e0a8,mode,1)
                put(e+0x72,0,1);put(e+0x5e,rng.choice([64,255,65535]));put(get(e+0x78)+10,rng.choice([44,45]),1)
                compare(4,e);compare(5,e)
counts['pain_death_species_and_state_dispatch']=cases-start
start=cases
for _ in range(700):
    setup();position=get(POOL);x=rng.choice([0,1,5,14,15]);z=rng.choice([0,1,5,14,15]);put(position,x*512+200);put(position+4,z*512+200)
    source=GRID+(z*16+x)*16;put(source+11,rng.randrange(32),1)
    for i in range(256):
        cell=GRID+i*16;put(cell+10,rng.choice([0,0,6,19,20,45]),1);put(cell+11,rng.randrange(32),1);put(cell+13,rng.randrange(40),1)
        put(cell+12,rng.choice([0,0,1,7,15,16,21,25,28,254,255]),1);put(cell+4,rng.randrange(8),1)
    for i in range(8):
        e=ENTITIES+i*208;put(e+0x74,rng.randrange(1,15),1);put(e+0x58,rng.choice([0,1,100,32767]));put(e+0x4e,rng.choice([0,0,1]));put(e+0x6f,rng.choice([0,5,6,7]),1)
        put(OBJECTS+i*40+0x19,rng.choice([0,0,1,128]),1);put(0x2365f4+i,1,1)
    put(0x9e0a8,rng.randrange(2),1);compare(6,position,source)
counts['blast_bounds_occlusion_and_damage']=cases-start
start=cases
for kind in range(1,15):
    for health in [1,100,32767,32768,65535]:
        for amount in [1,16,100,65535]:
            setup();e=ENTITIES;put(e+0x74,kind,1);put(e+0x58,health);put(e+0x72,0,1)
            compare(7,e,amount,65535)
# Expiring explosive shots and collision-driven deaths across consecutive ticks.
for kind in [0,1,2,3,4,11,12,13,19]:
    setup();put(0x2365e0,ENTITIES,4);put(POOL+0x1e,kind,1);put(POOL+0x1c,5,1)
    put(0xa0a48,2800);put(0xa0a4a,128);put(0xa0a4c,2800)
    for tick in range(8):compare(8,TASK)
counts['health_to_state_and_projectile_integration']=cases-start
start=cases
for mode in [0,1]:
    for gate in range(8):
        for scenario in ['open','wall','height12','height13','low_ceiling7','low_ceiling8']:
            setup();position=get(POOL);source=GRID+(5*16+5)*16;put(0x9e0a8,mode,1)
            for i in range(256):
                cell=GRID+i*16;put(cell+12,[0,1,7,15,16,21,25,255][i%8],1);put(cell+4,i%8,1)
            xoff=int.from_bytes(mu.mem_read(0x9c934+gate*4,2),'little',signed=True)
            zoff=int.from_bytes(mu.mem_read(0x9c936+gate*4,2),'little',signed=True)
            cell=GRID+((5+zoff)*16+5+xoff)*16
            if scenario=='wall':put(cell+10,20,1)
            if scenario in ['height12','height13']:put(cell+11,16 if scenario=='height12' else 17,1)
            if scenario in ['low_ceiling7','low_ceiling8']:put(cell+10,6,1);put(cell+13,11 if scenario=='low_ceiling7' else 12,1)
            compare(6,position,source)
counts['directed_blast_gates_and_both_rings']=cases-start
start=cases
for mutation in [1,2]:
    for kind in range(1,15):
        setup();put(0x310800,mutation,1);put(ENTITIES+0x72,0,1);put(ENTITIES+0x74,kind,1);compare(5,ENTITIES)
# Original death entry is deliberately not idempotent: repeated direct calls
# repeat counters and entry work; the higher-level damage gate prevents it.
for kind in range(1,15):
    setup();put(ENTITIES+0x74,kind,1)
    for _ in range(3):compare(5,ENTITIES)
counts['drop_callback_mutation_and_repeated_death_entry']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Item drops, sound playback, visual effects, player damage and network transmission remain observed service stubs.',
    'Native pain/death entry, animation selection, knockback, health updates and blast propagation run together.',
    'Fixtures use valid entity/resource pointers and finite animation programs; AI/death animation playback and whole-game behavior are not established.'])
(ROOT/'decompilation/reports/combat_runtime_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
