"""Pickup scene setup/drawing and native object transforms versus original x86."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX
mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.pickup_scene_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
dll.pickup_scene_probe.restype=None
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x22c000,0x2000),(0x234000,0x4000),(0x310000,0x1000),(0x320000,0x6000),(0x330000,0x2000),(0x340000,0x7000),(0x3f9000,0x600),(0x23ad00,0x60),(0x350000,0x10000),(0x23a400,0x100),(0x3f6d00,0x20),(0x22f000,0x1000),(0x360000,0x20000),(0x380000,0x2000)]
rng=random.Random(0x5CE0E)
services={0x2504c:2,0x26358:2,0x25a6c:2,0x4d2a8:4,0x4a000:3,0x4a020:3,
    0x5225c:2,0x52138:1,0x3ab6c:1,0x40460:3}
effects=[]
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(mu.mem_read(a,size),'little')
def hook(uc,address,size,_):
    args=[uc.reg_read(r) for r in [UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX]]
    args[services[address]:]=[0]*(4-services[address]);effects.extend([address,services[address],*args])
    if address==0x4d2a8:uc.reg_write(UC_X86_REG_EAX,0)
for address in services:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
POOL=0x320000;OBJECTS=0x322000;GRID=0x330000;ENTITIES=0x340000;TASK=0x310000
entries=[0x47630,0x47294,0x46ef4,0x46e4c,0x46ec4,0x477f8,0x3b2bc,0x3af20,0x3c074,0x2d004,0x3b43c,0x3c250]
cases=0;counts={}
def compare(op,a=0,b=0,c=0,d=0):
    global cases
    for start,length in windows:C.memmove(base+start,bytes(mu.mem_read(start,length)),length)
    effects.clear();mu.reg_write(UC_X86_REG_EBX,c);mu.reg_write(UC_X86_REG_ECX,d)
    run(entries[op],a,b,instruction_limit=4000000)
    log=(U32*16384)();count=U32();dll.pickup_scene_probe(native,SIZE,op,a,b,c,d,log,C.byref(count))
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

def pickup_setup(n=1):
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


SPAWNS=0x359800
def setup(n=1):
    pickup_setup(n);put(0xa0cc0,1);put(0xa0c18,128);put(0xa0c50,SPAWNS,4)
    for i in range(128):put(SPAWNS+i*8,i%16,1);put(SPAWNS+i*8+1,i//16,1)
    for i in range(28):put(0x236bc4+i*4,0x357000,4)
    put(0x236c34,RULES,4);put(0x236c4c,1);put(0x236c38,DROPS+n*48 if n<128 else 0,4)
    for i in range(128):put(DROPS+i*48+24,0x357000,4)
    for matrix in [0x23ad08,0xa0b68]:
        for i in range(9):put(matrix+i*2,4096 if i in [0,4,8] else 0)
    for i in range(3):put(0x23ad1c+i*4,0,4);put(0xa0a20+i*2,0)
    put(0x3f6d04,256,4);put(0x9d5cc,160);put(0x9d5ce,120)
    put(0x2344a8,0x358100,4);mu.mem_write(0x358100,rng.randbytes(2048));mu.mem_write(0x22f16c,rng.randbytes(0x800))
    put(0x22ceac,0x360000,4);put(0x22ceb0,0x380000,4);mu.mem_write(0x360000,bytes(0x20000));mu.mem_write(0x380000,bytes(0x2000))
    put(0x96f6c,0x4a000,4);put(0x96f8c,0x4a020,4);put(0x23a494,0x350000,4);put(0x357010,1);put(0x357012,1,1)

for _ in range(500):
    setup();a=0x310420;b=0x310440
    for i in range(3):put(a+i*2,rng.getrandbits(16))
    compare(0,a,b)
for _ in range(500):
    setup();a=0x310420;b=rng.choice([a,0x310440])
    for i in range(9):put(a+i*2,rng.getrandbits(16));put(b+i*2,rng.getrandbits(16))
    compare(1,a,b)
for _ in range(300):
    setup();a=0x310420;b=0x310440;mu.mem_write(a,rng.randbytes(18))
    for i in range(3):put(b+i*4,rng.getrandbits(32),4)
    compare(2,a,b)
for op in [3,4,5]:
    for _ in range(100):
        setup();a=rng.choice([0x310420,0x23ad08,0x23ad06]);b=0x310440;mu.mem_write(a,rng.randbytes(32));mu.mem_write(b,rng.randbytes(12));compare(op,a,b)
counts['native_object_rotation_composition_scale_and_translation']=cases
start=cases
for _ in range(250):
    setup(0);n=rng.randrange(33)
    for i in range(n):
        rule=RULES+i*8
        for j,v in enumerate([rng.randrange(16),rng.randrange(16),rng.randrange(26),rng.randrange(256),rng.randrange(256),rng.randrange(256),rng.choice([0,0,1]),0]):put(rule+j,v,1)
    compare(6,RULES,n)
for mode,host in [(0,0),(1,0),(1,1)]:
    for n in [0,1,10,11,12,16,24]:
        for _ in range(5):
            setup(n);put(0x9e0a8,mode,1);put(0x3f953e,host,1)
            for i in range(n):put(DROPS+i*48+32,rng.choice([0,7,15,20]),1)
            compare(7)
counts['population_and_authoritative_network_placement']=cases-start
start=cases
for depth in [383,384,385,1535,1536,4095,4096,8191,8192]:
    for x in [-10000,0,8191,8192]:
        for visible in [0,1]:
            setup();p=DROPS;put(p,x);put(p+4,depth);put(0xa0cc0,visible);put(p+32,rng.randrange(26),1)
            for i in range(3):put(p+16+i*2,rng.randrange(4096))
            compare(8,p)
for kind in list(range(23))+[255]:
    for _ in range(15):
        setup();p=POOL;pos=get(p);put(pos,0);put(pos+2,0);put(pos+4,rng.choice([511,512,513,1024,6144,7680,8192]))
        put(p+8,0x357000,4);put(p+0x1e,kind,1);put(p+0x1c,rng.randrange(32),1);put(0x95f9c,rng.choice([0,1,1,3]),1)
        for i in range(3):put(p+16+i*2,rng.randrange(4096))
        compare(9,p)
counts['pickup_and_projectile_drawing_with_native_geometry']=cases-start
start=cases
for mode,host in [(0,0),(1,0),(1,1)]:
    for _ in range(20):
        setup(0);n=16;put(0xa0c1c,n);put(0x9e0a8,mode,1);put(0x3f953e,host,1)
        for i in range(n):
            rule=RULES+i*8
            for j,v in enumerate([i%8+1,i//8+1,rng.choice([9,18,20,24]),10,2,255,0,0]):put(rule+j,v,1)
        compare(10)
        put(0xa0a48,7000);put(0xa0a4c,7000)
        for _ in range(4):compare(11,TASK)
counts['level_reset_population_and_pickup_drawing_ticks']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Model submission/rasterization, HUD presentation, network messages and texture-page query remain controlled providers.',
    'Object transforms, pickup drawing orchestration, level population and host placement execute natively.',
    'Network placement requires valid spawn counts and enough distinct reachable points for its original rejection loop.'])
(ROOT/'decompilation/reports/pickup_scene_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
