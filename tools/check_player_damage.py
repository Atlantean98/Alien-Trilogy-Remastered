"""Player damage, armor, feedback, status timers and combat integration versus original x86."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX
mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.damage_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
dll.damage_probe.restype=None
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x22c000,0x2000),(0x234000,0x4000),(0x310000,0x1000),(0x320000,0x6000),(0x330000,0x2000),(0x340000,0x7000),(0x3f9000,0x600),(0x23ad00,0x60),(0x350000,0x10000),(0x3e8800,0x200),(0x3f9800,0x100),(0x3fa900,0x100)]
rng=random.Random(0xDADA6E)
services={0x560a4:1,0x40460:3,0x3c8a8:1,0x403ec:3,0x3a120:0,0x3a444:0,
    0x18450:1,0x42ea4:3,0x512d0:1,0x51808:0,0x52fb0:0,0x55dfc:0,0x50c40:0,
    0x3b61c:1,0x51360:2,0x5287c:0,0x2a3e0:3,0x2a6e0:2,0x2a684:1,
    0x40db0:1,0x41044:1,0x2d004:1,0x44fa0:1,0x2a4e8:2,0x404f8:3}
effects=[]
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(mu.mem_read(a,size),'little')
def hook(uc,address,size,_):
    args=[uc.reg_read(r) for r in [UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX]]
    args[services[address]:]=[0]*(3-services[address])
    opacity=uc.reg_read(UC_X86_REG_ECX) if address==0x42ea4 else 0
    effects.extend([address,*args,opacity])
    if address==0x3a444 and get(0x310800,1):put(0xa0cc7,1,1);put(0x9d53c,1,1)
    if address==0x18450:
        uc.reg_write(UC_X86_REG_EAX,get(0x310804))
        if get(0x310808,1):put(0x3f98f0,0x56789abc,4)
for address in services:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
POOL=0x320000;OBJECTS=0x322000;GRID=0x330000;ENTITIES=0x340000;TASK=0x310000
entries=[0x157c4,0x34ab4,0x34acc,0x3df58,0x3dde8,0x3dee8,0x3df04,0x3df14,0x3dfc0,0x2a1e8,0x362a8,0x2d234,0x3dfc0]
cases=0;counts={}
def compare(op,a=0,b=0,c=0):
    global cases
    for start,length in windows:C.memmove(base+start,bytes(mu.mem_read(start,length)),length)
    effects.clear();mu.reg_write(UC_X86_REG_EBX,c);mu.reg_write(UC_X86_REG_ECX,0)
    run(entries[op],a,b,instruction_limit=1500000)
    log=(U32*16384)();count=U32();dll.damage_probe(native,SIZE,op,a,b,c,log,C.byref(count))
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


def setup(n=1):
    combat_setup(n)
    for address,value in [(0xa0ab8,100),(0xa0aba,0),(0xa0b30,0),(0xa0ae4,0),(0xa0ad6,0),(0xa0ada,0),(0xa0adc,0),(0xa0abc,0),(0xa0ad2,0)]:put(address,value)
    for address in [0x94b9c,0x9d53c,0xa0cc7,0xa0c90,0xa0c91,0xa0c92,0x3e88ed,0x3e8909]:put(address,0,1)
    put(0x3f94f8,1234,4);put(0x310804,101,4);put(0x3fa91f,7,1);put(0x310808,0,1)
    for i in range(4):put(0x3f98e4+i*4,0x12340000+i,4)

for op in [0,1,2]:
    for mode in [0,1,2,255]:
        for value in [0,1,3,5,36,32768,65535]:setup();put(0x9e0a8,mode,1);compare(op,value)
for health in [0,1,15,16,100,32767,32768,65535]:
    for _ in range(60):setup();put(0xa0ab8,health);compare(3)
counts['feedback_markers_and_damage_voice']=cases
start=cases
for armor in [0,1,16,32767,32768,65535]:
    for health in [0,1,100,32767,32768,65535]:
        for amount in [0,1,16,100,32767,32768,65535]:
            for mode in [0,1]:
                setup();put(0xa0aba,armor);put(0xa0ab8,health);put(0x9e0a8,mode,1);compare(4,amount,3)
for _ in range(700):
    setup();put(0xa0aba,rng.getrandbits(16));put(0xa0ab8,rng.getrandbits(16))
    put(0xa0b30,rng.choice([0,0,1,65535]));put(0xa0ae4,rng.choice([0,0,1,65535]));put(0x94b9c,rng.choice([0,4,255]),1)
    put(0x9e0a8,rng.choice([0,1,2]),1);op=rng.choice([4,5,6,7]);compare(op,rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(32))
counts['armor_health_wrap_immunity_and_directional_hits']=cases-start
start=cases
for _ in range(600):
    setup()
    for address in [0xa0ae4,0xa0b30,0xa0adc,0xa0abc,0xa0ad6,0xa0ada,0xa0ad2]:put(address,rng.choice([0,1,2,32768,65535]))
    for address in [0x95f9c,0x9e0a8,0xa0cc7,0x9d53c]:put(address,rng.choice([0,1,2,255]),1)
    for address in [0xa0c90,0xa0c91,0xa0c92,0xa0bb4,0x3fa91f]:put(address,rng.randrange(256),1)
    put(0x3e88ed,rng.randrange(2),1);put(0x3e8909,rng.randrange(2),1);put(0x310800,rng.randrange(2),1)
    put(0x310804,rng.choice([0,1,100,101,320,0xffffffff,0x80000000]),4);compare(rng.choice([8,12]))
counts['status_timers_ui_and_provider_mutation']=cases-start
start=cases
for mode in [0,1]:
    for armor in [0,1,100]:
        setup();put(0x9e0a8,mode,1);put(0xa0aba,armor)
        compare(5,20,5)
        for tick in range(32):
            compare(12)
            if tick in [0,6,7,8,28,29,30]:compare(7,10,1024,3)
for kind in [0,5,10,11]:
    setup();position=get(POOL);put(position,2800);put(position+2,200);put(position+4,2800)
    put(0xa0a48,2800);put(0xa0a4a,128);put(0xa0a4c,2800);put(POOL+0x1e,kind,1)
    compare(9,POOL)
    assert get(0xa0ab8,2)==84
for mode in [0,1]:
    setup();source=GRID+(5*16+5)*16;put(0x9e0a8,mode,1)
    # All neighbors are player occupants: cooldown permits only the first hit.
    for i in range(256):put(GRID+i*16+12,255,1)
    compare(10,get(POOL),source);assert get(0xa0ab8,2)==50
    setup();put(0x9e0a8,mode,1);put(POOL+0x1e,13,1);put(0xa0a48,2800);put(0xa0a4a,128);put(0xa0a4c,2800)
    for _ in range(12):compare(11,TASK);compare(12)
counts['cooldown_recovery_projectile_and_blast_integration']=cases-start
start=cases
for mode in [0,1,2,255]:
    for tick in [0,1,2,255]:
        for blocked in [0,1,2,255]:
            for action in [0,1,2,3]:
                setup();put(0x9e0a8,mode,1);put(0x95f9c,tick,1);put(0xa0cc7,blocked,1)
                put(0xa0ad6,1);put(0xa0ada,1);put(0xa0ad2,1);put(0x9d53c,1,1);put(0xa0c92,2,1);put(0xa0bb4,32,1)
                put(0x3e88ed,action&1,1);put(0x3e8909,action>>1,1);put(0x310804,101 if action&1 else 0xffffffff,4);put(0x310808,1,1)
                compare(12)
counts['directed_status_expiry_and_network_ui_branches']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Actual UI drawing, text width, sound, player HUD subroutines and network/UI actions are controlled providers.',
    'Health/armor/cooldown, feedback/voice selection, directional impulse and status control flow are native C++.',
    'Whole-game playback and audiovisual equivalence are not established.'])
(ROOT/'decompilation/reports/player_damage_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
