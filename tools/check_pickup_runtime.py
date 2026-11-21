"""Pickup movement, collection, inventory awards and pool reuse versus original x86."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX
mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.pickup_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
dll.pickup_probe.restype=U32
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x22c000,0x2000),(0x234000,0x4000),(0x310000,0x1000),(0x320000,0x6000),(0x330000,0x2000),(0x340000,0x7000),(0x3f9000,0x600),(0x23ad00,0x60),(0x350000,0x10000)]
rng=random.Random(0x91C0A11)
services={0x5225c:2,0x52138:1,0x3c074:1,0x3ab6c:1,0x40460:3,0x3b2bc:2,0x3af20:0}
effects=[]
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(mu.mem_read(a,size),'little')
def hook(uc,address,size,_):
    args=[uc.reg_read(r) for r in [UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX]]
    args[services[address]:]=[0]*(3-services[address]);effects.extend([address,*args])
    if address==0x3ab6c and get(0x310800,1):put(0x9e0a8,1,1)
for address in services:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
POOL=0x320000;OBJECTS=0x322000;GRID=0x330000;ENTITIES=0x340000;TASK=0x310000
entries=[0x3b0ac,0x3b43c,0x3bbd8,0x3bf50,0x380c0,0x380f4,0x38170,0x3f8c8,0x3c250,0x3c250,0x40098,0]
cases=0;counts={}
def compare(op,a=0,b=0,c=0):
    global cases
    for start,length in windows:C.memmove(base+start,bytes(mu.mem_read(start,length)),length)
    effects.clear();mu.reg_write(UC_X86_REG_EBX,c);mu.reg_write(UC_X86_REG_ECX,0)
    if op==11:run(0x3b61c,a,instruction_limit=1500000);expected=run(0x3c250,TASK,instruction_limit=1500000)
    else:expected=run(entries[op],a,b,instruction_limit=1500000)
    log=(U32*16384)();count=U32();actual=dll.pickup_probe(native,SIZE,op,a,b,c,log,C.byref(count))
    if op==3:assert actual==expected,('return',op,actual,expected)
    if op==10:assert actual==mu.reg_read(UC_X86_REG_ECX)==0
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
def combat_setup(n=1):
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


DROPS=0x35a000;RULES=0x359000

def setup(n=1):
    combat_setup();put(0x236c40,DROPS,4);put(0x236c44,DROPS if n else 0,4);put(0x236c38,DROPS+n*48 if n<128 else 0,4)
    put(0x236c34,RULES,4);put(0x236c4c,1);put(0xa0c1c,0);put(0xa0c54,RULES,4)
    put(0xa0a48,2800);put(0xa0a4a,128);put(0xa0a4c,2800);put(0x3f953e,1,1)
    put(0xa0c90,0,4);put(0xa0ab4,0,4);put(0xa0ab8,100);put(0xa0aba,0);put(0xa0aac,0);put(0xa0aae,0);put(0xa0aa8,0,4)
    for i in range(9):put(0xa0abe+i*2,0)
    for i in range(5):put(0x2368f8+i*4,i);put(0x2368fa+i*4,0)
    mu.mem_write(DROPS,bytes(128*48));mu.mem_write(RULES,bytes(8))
    for i in range(128):
        p=DROPS+i*48;put(p,2800);put(p+2,1024);put(p+4,2800);put(p+0x20,20,1);put(p+0x21,10,1);put(p+0x22,2,1);put(p+0x24,32)
        put(p+0x28,DROPS+(i+1)*48 if i+1<n else 0,4);put(p+0x2c,DROPS+(i-1)*48 if 0<i<n else 0,4)
        put(p+0x1c,GRID+(5*16+5)*16,4)
    for i in range(n,127):put(DROPS+i*48+0x28,DROPS+(i+1)*48,4)
    for i in range(256):put(GRID+i*16,1,4)
    for i in range(26):put(0x9cd34+i*4,0x358000+i*4,4);put(0x358000+i*4,500+i,4)
    put(0x310800,0,1)

for _ in range(50):
    setup();put(0xa0c1c,rng.choice([0,1,16]));compare(1)
    for i in range(5):compare(0,DROPS+i*48)
for _ in range(100):setup();compare(4);put(0xa0aae,rng.getrandbits(16));compare(5)
counts['pool_reset_release_and_weapon_slots']=cases
start=cases
for kind in list(range(27))+[65535]:
    for _ in range(30):
        setup();mu.mem_write(0xa0ab4,rng.randbytes(0x30));mu.mem_write(0x2368f8,rng.randbytes(20))
        compare(6,kind,rng.getrandbits(32),rng.getrandbits(32))
for selected in range(7):
    for state in [0,1,2,65535]:
        for weapon in range(7):setup();put(0xa0aae,selected);put(0xa0aac,state);compare(7,weapon)
for _ in range(40):setup();compare(10);assert get(0xa0abe,2)==15 and get(0xa0ac0,2)==2
counts['inventory_awards_refresh_and_reset_arguments']=cases-start
start=cases
for _ in range(1000):
    setup();p=DROPS
    for offset,player in [(0,0xa0a48),(2,0xa0a4a),(4,0xa0a4c)]:put(p+offset,(get(player,2)+rng.choice([-1024,-1023,-768,-767,0,767,768,1023,1024]))&65535)
    put(p+0x20,rng.randrange(27),1);put(0x9e0a8,rng.randrange(2),1);put(0xa0ab4,rng.randrange(256),1);put(0xa0c90,rng.choice([0,0,8,32,64,0x80000]),4)
    compare(3,p)
counts['pickup_touch_boundaries_and_flags']=cases-start
start=cases
for _ in range(700):
    setup();p=DROPS;put(0x95f9c,rng.choice([0,1,2,3]),1);put(0x9e0a8,rng.randrange(2),1);put(0x3f953e,rng.randrange(2),1)
    put(p,rng.randrange(2300,3000));put(p+4,rng.randrange(2300,3000));put(p+2,rng.choice([0,128,255,512,768,1024,4096]))
    for offset in [8,10,12]:put(p+offset,rng.choice([-128,-16,-4,-2,0,4,16,128]))
    put(p+0x20,rng.randrange(26),1);put(p+0x24,rng.choice([0,4,8,16,32,32768]))
    for i in range(256):
        cell=GRID+i*16;put(cell+10,rng.choice([0,6,19,20,44,45,49,50,51,52]),1);put(cell+11,rng.randrange(20),1);put(cell+13,rng.randrange(40),1);put(cell+12,rng.choice([0,0,19,20,255]),1)
        if rng.randrange(12)==0:put(cell,0,4)
    compare(2,p)
counts['pickup_motion_collision_gravity_and_network_gates']=cases-start
start=cases
for kind in range(27):
    for mode,host in [(0,0),(1,0),(1,1)]:
        for full in [0,1]:
            setup();p=DROPS;put(p+0x20,kind,1);put(0x9e0a8,mode,1);put(0x3f953e,host,1)
            for address,value in [(0xa0abe,15),(0xa0ac0,9),(0xa0ac2,100),(0xa0ac4,100),(0xa0ac6,9),(0xa0ac8,20),(0xa0aca,500),(0xa0acc,100),(0xa0ace,9),(0xa0ab8,200),(0xa0aba,100)]:put(address,value if full else 0)
            compare(8,TASK)
counts['collection_kind_capacity_and_multiplayer_dispatch']=cases-start
start=cases
for axis in [0,1]:
    for speed in [-16,16]:
        for scenario in ['open','wall','occupied','empty_cell','step256','step288','low_ceiling']:
            setup();p=DROPS;put(p+2,1024);put(p+8,speed if axis==0 else 0);put(p+12,speed if axis==1 else 0)
            put(p,2560 if speed<0 and axis==0 else 3065 if axis==0 else 2800)
            put(p+4,2560 if speed<0 and axis==1 else 3065 if axis==1 else 2800)
            x=get(p,2)+(speed if axis==0 else 0);z=get(p+4,2)+(speed if axis==1 else 0);cell=GRID+((z>>9)*16+(x>>9))*16
            if scenario=='wall':put(cell+10,20,1)
            if scenario=='occupied':put(cell+12,20,1)
            if scenario=='empty_cell':put(cell,0,4)
            if scenario=='step256':put(cell+11,12,1)
            if scenario=='step288':put(cell+11,13,1)
            if scenario=='low_ceiling':put(cell+10,6,1);put(cell+13,20,1)
            compare(2,p)
counts['directed_axis_wall_step_and_ceiling_clips']=cases-start
start=cases
for mode,host in [(0,0),(1,0),(1,1)]:
    setup(8);put(0x9e0a8,mode,1);put(0x3f953e,host,1);put(0xa0ab8,20);put(0xa0aae,0)
    for i,kind in enumerate([9,20,12,18,11,21,14,25]):
        p=DROPS+i*48;put(p+0x20,kind,1);put(p+0x21,10,1)
    for _ in range(4):compare(9,TASK)
for mutation in [0,1]:
    setup(3);put(0x310800,mutation,1);put(0x3f953e,1,1);put(0xa0ab8,10)
    for i,kind in enumerate([9,20,21]):put(DROPS+i*48+0x20,kind,1)
    compare(8,TASK)
# The same record is spawned, moved through repeated bounces, collected,
# returned to the free list, then reused by the next drop.
setup(0);put(RULES+2,20,1);put(RULES+3,25,1);put(RULES+6,1,1);put(ENTITIES+0x72,0,1)
put(0xa0a48,6000);put(0xa0a4c,6000);put(0xa0ab8,50)
compare(11,ENTITIES);p=get(0x236c44)
for _ in range(120):compare(9,TASK)
assert get(p+10,2)==0,('not_settled',hex(p),get(p+10,2))
for offset,player in [(0,0xa0a48),(2,0xa0a4a),(4,0xa0a4c)]:put(player,get(p+offset,2))
compare(9,TASK);assert get(0x236c44)==0 and get(0x236c38)==p and get(0xa0ab8,2)==75
put(0xa0a48,6000);put(0xa0a4c,6000);compare(11,ENTITIES);assert get(0x236c44)==p
counts['mixed_lists_host_client_callbacks_and_spawn_settle_collect_reuse']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Pickup drawing, HUD message presentation, multiplayer messages and level pickup population remain controlled service providers.',
    'Original motion, touch checks, inventory changes, list/pool updates, weapon refresh and task cursor run natively.',
    'Motion fixtures stay within allocated map backing; full multiplayer synchronization and visual equivalence are not established.'])
(ROOT/'decompilation/reports/pickup_runtime_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
