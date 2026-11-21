"""Item-drop creation and projectile packet/projection rendering versus original x86."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX
mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.drop_render_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
dll.drop_render_probe.restype=U32
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x22c000,0x2000),(0x234000,0x4000),(0x310000,0x1000),(0x320000,0x6000),(0x330000,0x2000),(0x340000,0x7000),(0x3f9000,0x600),(0x23ad00,0x60),(0x350000,0x10000),(0x22f000,0x1000),(0x23a400,0x100),(0x3f6d00,0x20),(0x360000,0x20000),(0x380000,0x2000)]
rng=random.Random(0xD209A11)
services={0x47630:2,0x47294:2,0x46ef4:4,0x46e4c:1,0x477f8:2,0x46ec4:1,0x26358:2,0x4d2a8:4,0x4a000:3,0x4a020:3,
    0x560a4:1,0x40460:3,0x2a3e0:3,0x404f8:3,0x2a6e0:2,0x2a4e8:2,0x51360:2,0x5287c:0,0x40db0:0,0x41044:0,0x2a684:0,0x44fa0:0}
effects=[]
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(mu.mem_read(a,size),'little')
def hook(uc,address,size,_):
    args=[uc.reg_read(r) for r in [UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX]]
    if address==0x46ef4:args=[args[0],*[get(args[1]+i*4) for i in range(3)]]
    args[services[address]:]=[0]*(4-services[address]);effects.extend([address,services[address],*args])
    if address==0x47630:
        for i in range(9):put(0xa0a28+i*2,100+i)
    if address==0x46ec4:
        for i in range(9):put(0x23ad08+i*2,200+i)
    if address==0x4d2a8:uc.reg_write(UC_X86_REG_EAX,0x1357)
for address in services:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
POOL=0x320000;OBJECTS=0x322000;GRID=0x330000;ENTITIES=0x340000;TASK=0x310000
entries=[0x3b61c,0x47f40,0x12ed4,0x4cf78,0x4df44,0x48658,0x2ccfc,0x2cacc,0x2cbd4,0x2c27c,0x2c3d8,0x2c614,0x2c82c,0x2c96c,0x2d004,0x30518,0x2d234,0x2d004]
cases=0;counts={}
def compare(op,a=0,b=0,c=0,d=0,e=0):
    global cases
    for start,length in windows:C.memmove(base+start,bytes(mu.mem_read(start,length)),length)
    effects.clear();mu.reg_write(UC_X86_REG_EBX,c);mu.reg_write(UC_X86_REG_ECX,d)
    if op==1:mu.reg_write(UC_X86_REG_ECX,c)
    if op==3:put(o.STACK+4,e,4)
    expected=run(entries[op],a,b,instruction_limit=2000000)
    log=(U32*16384)();count=U32();actual=dll.drop_render_probe(native,SIZE,op,a,b,c,d,e,log,C.byref(count))
    if op==1:assert actual==expected,('return',op,hex(actual),hex(expected))
    if op==2:assert actual==(expected&65535)
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


SCREEN=0x310400;FLAGS=0x310410;PAL=0x358000;RULES=0x359000;DROPS=0x35a000;PACKETS=0x360000;BUCKETS=0x380000

def setup(n=1):
    combat_setup(n);put(0x2344a8,PAL,4);mu.mem_write(PAL,rng.randbytes(2048))
    mu.mem_write(PACKETS,rng.randbytes(0x20000));mu.mem_write(BUCKETS,rng.randbytes(0x2000));mu.mem_write(0x22f16c,rng.randbytes(0x800))
    put(0x22ceac,PACKETS,4);put(0x22ceb0,BUCKETS,4);put(SCREEN,160);put(SCREEN+2,120);put(FLAGS,0x12345678,4)
    put(0x236c4c,1);put(0x236c34,RULES,4);put(0x236c38,DROPS,4);put(0x236c44,0,4)
    for i in range(32):put(DROPS+i*48+0x28,DROPS+(i+1)*48 if i<31 else 0,4)
    for i in range(256):
        rule=RULES+i*8;put(rule+2,rng.randrange(27),1);put(rule+3,rng.randrange(256),1);put(rule+4,rng.randrange(256),1);put(rule+5,0,1);put(rule+6,1,1)
    for i in range(28):put(0x236bc4+i*4,0xAB000000+i,4)
    # Resource table overlaps its following globals in the original; restore
    # its pointers after installing only the valid 28 resource entries.
    put(0x236c34,RULES,4);put(0x236c38,DROPS,4);put(0x236c44,0,4);put(0x236c4c,1)
    for p in [POOL+i*44 for i in range(n)]:
        put(p+8,0x357000,4);put(p+0x21,0,1);put(0x357010,1);put(0x357012,3,1)
    for base in [0x23ad08,0xa0b68]:
        for i in range(9):put(base+i*2,4096 if i in [0,4,8] else 0)
    for i in range(3):put(0x23ad1c+i*4,0,4);put(0xa0a20+i*2,0)
    put(0x3f6d04,256,4);put(0x9d5cc,160);put(0x9d5ce,120);put(0xa0ca0,1024,4);put(0x96234,0)
    put(0x96f6c,0x4a000,4);put(0x96f8c,0x4a020,4);put(0x23a494,0x350000,4)
    put(ENTITIES+0x72,0,1)
    for address in [0xa0ab8,0xa0aba,0xa0ae4,0xa0b30]:put(address,0)

for _ in range(600):
    setup();p=DROPS;rule=RULES;put(ENTITIES+0x6d,rng.randrange(10),1)
    put(rule+5,rng.choice([0,1,2,254,255]),1);put(rule+6,rng.choice([0,1,2,254,255]),1)
    put(0x236c4c,rng.choice([0,1,2]));put(0x236c44,DROPS+48 if rng.randrange(2) else 0,4)
    mu.mem_write(DROPS,rng.randbytes(48));put(DROPS+0x28,DROPS+96,4)
    compare(0,ENTITIES)
counts['drop_threshold_pool_and_direction']=cases
start=cases
for _ in range(600):
    setup();point=0x310420
    for i in range(3):put(point+i*2,rng.getrandbits(16));put(0x23ad1c+i*4,rng.randrange(-4000,4001),4)
    for i in range(9):put(0x23ad08+i*2,rng.randrange(-4096,4097))
    put(0x3f6d04,rng.choice([0,1,256,1024,0x80000000]),4);put(SCREEN,rng.getrandbits(16));put(SCREEN+2,rng.getrandbits(16));compare(1,point,SCREEN,FLAGS)
for _ in range(100):setup();put(0x96234,rng.getrandbits(16));compare(2)
for _ in range(300):
    setup();a=0x310420;b=0x310430;out=rng.choice([a,b,0x310440]);mu.mem_write(a,rng.randbytes(6));mu.mem_write(b,rng.randbytes(6))
    compare(3,a,b,rng.getrandbits(32),rng.getrandbits(32),out)
for _ in range(300):setup();compare(4,PAL,rng.getrandbits(32),PACKETS+8)
for _ in range(100):setup();compare(5,BUCKETS,PACKETS)
counts['projection_modes_blend_shading_and_bucket_links']=cases-start
start=cases
for op in range(6,12):
    for _ in range(120):
        setup();put(0x95f9c,rng.choice([0,1,1,2]),1);put(0xa0ca0,rng.choice([16,512,1024,4608,6144]),4)
        put(POOL+0x1e,rng.choice([0,2,3,4,7,8,9,10,14,16,17,18]),1);put(POOL+0x1c,rng.randrange(32),1);put(POOL+0x1d,rng.randrange(2),1);put(POOL+0x1a,rng.randrange(4));put(POOL+0x21,rng.randrange(5),1)
        put(0xa0c90,rng.choice([0,128]),1)
        if op in [6,8]:compare(op,POOL,SCREEN)
        elif op in [7,9]:compare(op,SCREEN,POOL)
        else:compare(op,SCREEN,rng.choice([-1,0,1,15,27,28,31,32]),rng.choice([14,16,17,18]))
counts['packet_builders_and_original_tables']=cases-start
start=cases
for op in [12,13]:
    for _ in range(150):
        setup();put(POOL+0x1e,rng.choice([6,11,12,13,15,19,20,21]),1);put(POOL+0x1c,rng.randrange(32),1)
        position=get(POOL);put(position,rng.choice([-10000,0,7679,7680,8192]));put(position+4,rng.choice([511,512,513,7679,7680,8191,8192]))
        for i in range(3):put(POOL+0x10+i*2,rng.getrandbits(16))
        put(0x95f9c,rng.choice([0,1,1]),1);compare(op,POOL)
counts['mesh_culling_transform_callbacks_and_rotation']=cases-start
start=cases
for kind in list(range(23))+[255]:
    for depth in [-100,0,31,32,511,512,513,6144,6145,7680]:
        setup();put(POOL+0x1e,kind,1);position=get(POOL);put(position,0);put(position+2,0);put(position+4,depth)
        put(POOL+0x1c,20,1);put(0x96234,rng.randrange(3));compare(14,POOL)
counts['all_type_render_dispatch_and_depth_boundaries']=cases-start
start=cases
# Consecutive drop triggers retain counters, consume free links and prepend
# active records. Saturated counters do not make this entry idempotent.
for direction in range(8):
    setup();put(ENTITIES+0x6d,direction,1);put(RULES+5,0,1);put(RULES+6,3,1)
    for _ in range(8):compare(0,ENTITIES)
for kind in [2,3,4,5,6,8,9,11,12,13,14]:
    setup();put(ENTITIES+0x74,kind,1);put(ENTITIES+0x72,0,1);compare(15,ENTITIES)
    assert get(0x236c44)!=0
counts['consecutive_drop_allocation_and_death_entry']=cases-start
start=cases
for kind in list(range(23))+[255]:
    for mode in [0,1,2]:
        for xy in [(0,0),(-160,-120),(160,120),(-161,-121),(161,121)]:
            setup();put(POOL+0x1e,kind,1);put(0x96234,mode);put(0x3f6d04,1024,4)
            position=get(POOL);put(position,xy[0]);put(position+2,xy[1]);put(position+4,1024)
            compare(17,POOL)
counts['screen_edges_and_render_mode_dispatch']=cases-start
start=cases
for mode in [0,1]:
    for kind in list(range(23))+[255]:
        setup();put(0x9e0a8,mode,1);put(POOL+0x1e,kind,1)
        for i,value in enumerate([-2800,-1000,-1500]):put(0x23ad1c+i*4,value,4)
        put(POOL+0x1c,12,1)
        for _ in range(14):compare(16,TASK)
counts['projectile_motion_render_packets_and_expiry']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Low-level mesh transforms/submission and texture-page query are scripted providers; final rasterization is unrecovered.',
    'Drop creation requires a valid triggered free record; the original dereferences null if its enabled pool is exhausted.',
    'Original projection near clipping preserves output coordinates/flags; render callers cannot submit below their depth thresholds.',
    'Packet bytes/order and projection are verified, not visual gameplay equivalence.'])
(ROOT/'decompilation/reports/drop_rendering_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
