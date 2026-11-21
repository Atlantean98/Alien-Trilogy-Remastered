"""Model dispatch, ordering and native software line pixels versus original x86."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX
mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.software_render_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
dll.software_render_probe.restype=U32
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x22c000,0x2000),(0x234000,0x4000),(0x310000,0x1000),(0x320000,0x6000),(0x330000,0x2000),(0x340000,0x7000),(0x3f9000,0x600),(0x23ad00,0x60),(0x350000,0x10000),(0x23a400,0x100),(0x3f6d00,0x20),(0x22f000,0x1000),(0x360000,0x20000),(0x380000,0x2000),(0x390000,0x2000),(0x3a0000,0x10000),(0x3f6d00,0x200),(0x28c800,0x4b100)]
rng=random.Random(0x11AEBE0)
services={0x5a000+i*32:3 for i in range(9)}
effects=[]
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(mu.mem_read(a,size),'little')
def hook(uc,address,size,_):
    args=[uc.reg_read(r) for r in [UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX]];n=get(0x310810,1)
    args[n:]=[0]*(3-n);effects.extend([address,n,*args])
    if get(0x310800,1) and n==3:put(args[0]+18,get(args[0]+18,1)|128,1)
for address in services:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
POOL=0x320000;OBJECTS=0x322000;GRID=0x330000;ENTITIES=0x340000;TASK=0x310000
entries=[0x608d4,0x48614,0x2504c,0x26358,0x25a6c,0x48bac,0x48a28,0x489c4,0]
cases=0;counts={}
def compare(op,a=0,b=0):
    global cases
    for start,length in windows:C.memmove(base+start,bytes(mu.mem_read(start,length)),length)
    effects.clear();mu.reg_write(UC_X86_REG_EBX,0);mu.reg_write(UC_X86_REG_ECX,0)
    if op==8:
        run(0x48614,0x390000,256);run(0x2d004,a,instruction_limit=6000000)
        p=0x390000+255*12
        while p:run(0x48a28,p,instruction_limit=6000000);p=get(p)
        expected=0
    else:expected=run(entries[op],a,b,instruction_limit=6000000)
    log=(U32*16384)();count=U32();actual=dll.software_render_probe(native,SIZE,op,a,b,log,C.byref(count))
    if op==0:assert actual==expected,('return',hex(actual),hex(expected))
    for start,length in windows:
        want=bytes(mu.mem_read(start,length));have=C.string_at(base+start,length)
        assert have==want,('memory',op,a,b,[(hex(start+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:15])
    assert list(log)[:count.value]==effects,('effects',op,a,b,list(log)[:count.value],effects)
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
def scene_setup(n=1):
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


FRAMEBUFFER=0x360000;PACKET=0x3a0000;MODEL=0x35c000

def setup(n=1):
    scene_setup(n);put(0x310810,3,1);put(0x9d544,FRAMEBUFFER,4);put(0x3f6e44,320,4);put(0x3f6e4c,240,4);put(0x9d504,319,4);put(0x9d508,239,4)
    put(0x22ceac,PACKET,4);put(0x22ceb0,0x390000,4)
    mu.mem_write(PACKET,rng.randbytes(4096));mu.mem_write(FRAMEBUFFER,bytes(0x30000))
    for band,table in enumerate([0x96ed0,0x96fe8,0x96f5c]):
        for kind in range(35):put(table+kind*4,0x5a000+(band*3+kind%3)*32,4)
    put(0x2344a4,0x350000,4);put(0x2344ae,10);put(get(DROPS+28)+14,3,1)

for value in [0,1,0xffffffff,0x7fffffff,0x80000000,0x80000001]+[rng.getrandbits(32) for _ in range(200)]:setup();compare(0,value)
for n in [0,1,2,3,16,128,256]:setup();mu.mem_write(0x390000,rng.randbytes(0x2000));compare(1,0x390000,n)
counts['wrapped_absolute_and_ordering_table_initialization']=cases
start=cases
for op in [2,3,4]:
    for _ in range(200):
        setup();n=rng.randrange(1,20);cell=get(DROPS+28);put(cell+14,rng.randrange(256),1);put(0x2344ae,rng.randrange(20));put(0x310800,rng.randrange(2),1)
        for i in range(n):
            primitive=MODEL+i*20;mu.mem_write(primitive,rng.randbytes(20));put(primitive+16,rng.randrange(20));put(primitive+19,rng.randrange(40),1);put(primitive+18,128 if i==n-1 else 0,1)
        compare(op,MODEL,cell)
counts['near_middle_far_model_primitive_dispatch']=cases-start
start=cases
for _ in range(700):
    setup()
    for offset in [12,14,20,22]:put(PACKET+offset,rng.randrange(-400,801))
    put(0x3f6e44,rng.choice([160,320,640]),4);put(0x3f6e4c,rng.choice([120,240,480]),4)
    compare(5,PACKET)
for points in [(0,0,0,0),(0,0,10,0),(10,10,0,10),(0,0,319,239),(-1,-1,1,1),(319,239,320,240),(-32768,0,0,0),(0,0,-32768,0)]:
    setup()
    for offset,value in zip([12,14,20,22],points):put(PACKET+offset,value)
    compare(5,PACKET)
counts['software_line_pixels_clipping_scale_and_word_edges']=cases-start
start=cases
for callback in [0,0x48bac,0x5a000]:
    setup();put(0x310810,1,1);put(PACKET+4,callback,4)
    for offset,value in zip([12,14,20,22],[10,10,20,20]):put(PACKET+offset,value)
    compare(6,PACKET)
for _ in range(3):setup();mu.mem_write(0x28c800,rng.randbytes(0x4b100));compare(7)
for kind in [0,2,4,5]:
    for _ in range(40):
        setup();put(POOL+0x1e,kind,1);put(0x96234,0);position=get(POOL);velocity=get(POOL+4)
        put(position,rng.randrange(-100,101));put(position+2,rng.randrange(-100,101));put(position+4,1024)
        for offset in [0,2,4]:put(velocity+offset,rng.randrange(-128,129))
        compare(8,POOL)
counts['packet_dispatch_buffer_clear_and_projectile_to_pixels']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Other model primitive handlers, textured/filled polygon rasterizers and display presentation remain providers.',
    'Line output is compared byte-for-byte against original indexed framebuffer writes, including its constant color and strict accumulator thresholds.',
    'Ordering/model fixtures use finite valid chains and buffers; this is not whole-game visual equivalence.'])
(ROOT/'decompilation/reports/software_rendering_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
