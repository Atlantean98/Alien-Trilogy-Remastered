"""Selection, animation and shot-wrapper comparisons against original x86."""
import ctypes as C
import json
import random
import struct
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX

mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32;SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
dll.weapon_probe.argtypes=[C.POINTER(U8),C.c_uint,C.c_uint,C.c_uint,C.POINTER(U32),C.POINTER(U32),C.POINTER(C.c_uint)]
dll.weapon_probe.restype=U32
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x234000,0x4000),(0x3f6000,0x5000),(0x300000,32),(0x330000,2304),(0x340000,128),(0x350000,0x5000)]
services=[0x59edd,0x403ec,0x51948,0x27f54,0x285f4,0x2ae64,0x2acac,0x51cc4,
    0x10a50,0x5de4c,0x4e6c8,0x34a1c,0x34a5c,0x2bb04,0x2b9d0,0x34a9c,0x2c160,0x40460,
    0x2be10,0x512d0,0x380c0,0x38170,0x4e130,0x27b6c,0x4e570,0x380f4,0x34a04,
    0x27b40,0x3dde8,0x3dd24,0x408b0,0x551ac,0x51808,0x55dfc,0x4e5a8,0x34acc,0x3dfc0]
effects=[];responses=[3,1];spawn_calls=0
def hook(uc,address,size,_):
    global spawn_calls
    a=uc.reg_read(UC_X86_REG_EAX);b=uc.reg_read(UC_X86_REG_EDX);c=uc.reg_read(UC_X86_REG_EBX)
    if address in [0x403ec,0x40460,0x51948,0x27f54,0x38170]:pass
    elif address in [0x285f4,0x2ae64,0x51cc4,0x27b6c,0x3dde8,0x3dd24]:c=0
    elif address in [0x2acac,0x512d0,0x408b0,0x4e5a8]:b=c=0
    else:a=b=c=0
    effects.extend([address,a,b,c])
    if address==0x2ae64:
        uc.reg_write(UC_X86_REG_EAX,int(spawn_calls<responses[0]));spawn_calls+=1
    elif address==0x27f54:uc.reg_write(UC_X86_REG_EAX,responses[1])
    elif address==0x38170:uc.reg_write(UC_X86_REG_ECX,25)
for address in services:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
rng=random.Random(0xA11A710)
def put(a,n,size=2):mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=2):return int.from_bytes(mu.mem_read(a,size),'little')
O=0xa0a68;DESC=0x352f00
TABLES=[0x9ce14,0x9ce04,0x9cde4,0x9ce44,0x9ce2c]
RESOURCES=[0x23441c,0x234414,0x234450,0x234404,0x234418]
reference_tables=bytes(mu.mem_read(0x9cde4,0x9ce5c-0x9cde4))
def setup():
    mu.mem_write(0xa0a00,rng.randbytes(0x300));mu.mem_write(0x350000,rng.randbytes(0x5000))
    for w,(table,resource,states) in enumerate(zip(TABLES,RESOURCES,[3,2,4,3,3])):
        frames=0x350000+w*0x100;sheet=0x352000+w*0x20;put(resource,sheet,4);put(sheet,100+w)
        for frame in range(5):mu.mem_write(frames+frame*12,struct.pack('<hhhhI',-7,-64,40+frame,68,100+w*10+frame))
        for state in range(states):
            seq=0x351000+w*0x100+state*0x20
            words=[2,3,0x8300,0,0x8200] if state==0 else [1,3,0x810b if w==3 else 0x810d,0,1,2,0x8600]
            mu.mem_write(seq,struct.pack('<'+'H'*len(words),*words));put(table+state*8,frames,4);put(table+state*8+4,seq,4)
    put(O+0x24,DESC,4);put(DESC,20);put(DESC+4,30,1);put(DESC+8,0,4)
    put(0xa0aa8,TABLES[0],4);put(O+0x20,0x352000,4);put(0xa0aac,0);put(0xa0aae,0)
    put(O+0x10,0x350000,4);put(O+0x14,0x350000,4);put(O+0x18,0x351006,4);put(O+0x1c,0x351004,4)
    put(O+0x30,2);put(O+0x32,2);put(O+0x36,0);put(O+0x3a,0)
    for i in range(5):put(0x2368f8+i*4,i);put(0x2368fa+i*4,0xffff)
    put(0x236a3a,0);put(0x2344a8,0x12345678,4)
    put(0xa0b58,0xffffffff,4);put(0xa0b5c,0);put(0xa0b50,0);put(0xa0b57,0,1)
    put(0xa0a48,2800);put(0xa0a4a,128);put(0xa0a4c,2800);put(0xa0a64,120)
    put(0xa0abe,15);put(0xa0ac2,8);put(0xa0ac4,100);put(0x94b9c,0,1);put(0x9e0a8,0,1)
    put(0xa0ba2,1024)
    for i in range(5):put(0x9ce5e + i*4,rng.randrange(-20,21));put(0x9ce60+i*4,rng.randrange(-20,21))
    grid=bytearray(2304)
    for i in range(144):grid[i*16+11]=4;grid[i*16+13]=80
    mu.mem_write(0x330000,bytes(grid));put(0xa0c30,12);put(0xa0c32,12);put(0xa0c40,0x330000,4)
    put(0xa0c34,0);put(0xa0c36,0);put(0xa0ab0,0x330000+(5*12+5)*16,4)

entries=[0x28280,0x2840c,0x3f9c4,0x3fa98,0x38628,0x386d4,0x38814,0x38958,0x389a8,0x3e228,0x2b4a4,0x2b4f0,0x2b548]
cases=0
def compare(op,arg=0):
    global cases,spawn_calls
    for address,length in windows:C.memmove(base+address,bytes(mu.mem_read(address,length)),length)
    effects.clear();spawn_calls=0
    entry=entries[op] if op<13 else get(arg+4,4)
    result=run(entry,arg,instruction_limit=400000)
    log=(U32*1024)();count=C.c_uint()
    value=dll.weapon_probe(native,SIZE,op,arg,(U32*2)(*responses),log,C.byref(count))
    if op in [4,5,6,8]:assert value==result,('return',op,arg,hex(value),hex(result))
    for address,length in windows:
        expected=bytes(mu.mem_read(address,length));actual=C.string_at(base+address,length)
        assert actual==expected,('memory',op,arg,[(hex(address+i),a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b][:15])
    assert list(log)[:count.value]==effects,('effects',op,arg,list(log)[:count.value],effects)
    cases+=1

counts={}
for op in [4,5,6,7,8]:
    for current in range(5):
        for owned in range(32):
            setup();put(0x236a3a,current)
            for i in range(5):put(0x2368fa+i*4,(owned>>i)&1)
            compare(op,(current+2)%5 if op==8 else 0)
counts['weapon_selection']=cases
start=cases
for _ in range(1000):
    setup();program=0x354000
    tokens=[rng.choice([0x810d,0x8300,0x8400,0x8500,0x8700,0x8900,0x8a03,0x8000]) for _ in range(rng.randrange(8))]
    tokens.append(rng.choice([0,1,2,0x8600,0x8800,0x8203]))
    put(O+0x18,program,4);put(O+0x1c,0x351006,4);put(O+0x36,rng.getrandbits(16));put(O+0x3a,rng.choice([0,1,2,0xffff]))
    mu.mem_write(program,struct.pack('<'+'H'*len(tokens),*tokens))
    compare(0,O)
for _ in range(500):
    setup();put(O+0x30,rng.choice([0,1,2,0xffff]));put(O+0x36,rng.getrandbits(16));compare(1,O)
for weapon in range(7):setup();compare(3,weapon)
for weapon,states in enumerate([3,2,4,3,3]):
    for state in range(states):
        setup();put(0xa0aa8,TABLES[weapon],4);put(0xa0aac,state);put(O+0x20,0x352000+weapon*0x20,4);compare(2)
counts['animation_command_start_and_advance']=cases-start
start=cases
for _ in range(700):
    setup();weapon=rng.randrange(5);compare(3,weapon)
    put(0xa0b58,rng.choice([0,0xffffffff]),4);put(0xa0b5c,rng.choice([0,1,2,0xffff]));put(0xa0b50,rng.getrandbits(16));put(0xa0b57,rng.randrange(256),1)
    responses[1]=rng.choice([0,1]);compare(9)
for weapon in range(5):
    setup();responses[1]=1;compare(3,weapon);put(0xa0aac,1);compare(2)
    for frame in range(20):compare(9)
counts['weapon_view_and_animation_sequences']=cases-start
start=cases
for op in [10,11,12]:
    for _ in range(500):
        setup();responses[0]=rng.randrange(4);put(0xa0a64,rng.getrandbits(16));put(0x9e0a8,rng.choice([0,1]),1);put(0x94b9c,rng.choice([0,0x20]),1)
        for address in [0xa0abe,0xa0ac2,0xa0ac4]:put(address,rng.choice([0,1,2,3,100,0xffff]))
        compare(op)
counts['shot_wrappers_and_ammunition']=cases-start
start=cases
# Use real initialization, input, movement, view, selection and animation together.
for sequence in range(16):
    setup();responses[:]=[3,1]
    put(0xa0c90,0,4);put(0xa0cc7,0,1);put(0xa0c24,5);put(0xa0c26,5);put(0xa0c28,4);put(0xa0c50,0x340000,4);put(0xa0bc0,0,4)
    mu.mem_write(0x340000,bytes([5,5,0,0,0,0,0,0])*16)
    put(0x9cde0,32);put(0x9e0a8,1,1);put(0x9cddd,0,1);put(0x9cddc,0,1);put(0x9d7fe,0);put(0x9ce5c,sequence%2)
    put(0xa0c7c,0,4);put(0xa0c94,0,4);put(0xa0c98,0,4);put(0x3fae1a,0,4);put(0x3f6e7f,0,1);put(0x95f9c,1,1)
    for address,value in [(0x94b8a,2),(0x94b8c,4),(0x94b88,8),(0x94b8e,1),(0x94b7a,16),(0x94b7c,64),(0x94b7e,128),(0x94b90,32)]:put(address,value)
    mu.mem_write(0x300000,bytes(32));put(0x300004,0x3fbc0,4);put(0x300018,0x300020,4)
    compare(13,0x300000)
    for frame in range(40):
        old=get(0xa0c98,4);held=0x1000 if frame<8 else 0
        if frame%10==0:held|=128
        put(0xa0c7c,old,4);put(0xa0c98,held,4);put(0xa0c94,held&~old,4)
        if frame==12:
            put(0xa0aac,4);put(0xa0b57,128,1);put(0xa0ab5,get(0xa0ab5,1)|2,1)
        if frame==26:
            put(0xa0ac4,3);put(0xa0ac6,1);put(0xa0aac,4);put(0xa0b57,128,1);put(0x3f6e6a,2)
        compare(13,0x300000)
counts['integrated_weapon_player_ticks']=cases-start
start=cases
# Retain synthetic sheet/renderer boundaries but use the executable's actual
# relocated frame tables and animation streams in its 0x80000 data object.
for weapon,states in enumerate([3,2,4,3,3]):
    setup();mu.mem_write(0x9cde4,reference_tables);responses[1]=1;compare(3,weapon)
    for state in range(states):
        put(0xa0aac,state);compare(2)
        for frame in range(48):compare(9)
counts['original_weapon_table_sequences']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],counts=counts,new_comparisons=cases,
    limitations=['Sprite submission, texture loading, audio and low-level projectile creation/effect providers are scripted stubs.',
                 'The game animation command interpreter is reconstructed; no original x86 code runs in the native library.',
                 'Selection tests use valid slot indices and animation programs with finite command processing.'])
(ROOT/'decompilation/reports/weapon_runtime_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
