"""Byte-level checks of input/weapon control, inventory and initialization.

Mapped data is a transitional C++ state representation, never an x86 interpreter.
The original oracle executes real movement/view code. Selection, animation,
projectile, device, UI, region and other effects use explicit scripted services.
"""
import ctypes as C
import json
import random
import struct
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ECX

mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U8,U32=C.c_uint8,C.c_uint32
dll.control_probe.argtypes=[C.POINTER(U8),C.c_uint,C.c_uint,C.c_uint,C.POINTER(U32),C.POINTER(U32)]
dll.control_probe.restype=C.c_uint
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
windows=[(0x94000,0xd000),(0x236000,0x1000),(0x3f6000,0x5000),(0x300000,32),(0x330000,12*12*16),(0x340000,128)]
service_addresses=[0x10a50,0x5de4c,0x38958,0x38814,0x386d4,0x389a8,0x4e6c8,0x34a1c,0x34a5c,0x3f9c4,
    0x2b4a4,0x2b4f0,0x2b548,0x2bb04,0x2b9d0,0x34a9c,0x2c160,0x40460,0x51948,0x2be10,0x59edd,
    0x38628,0x512d0,0x380c0,0x38170,0x4e130,0x27b6c,0x3fa98,0x4e570,0x380f4,0x34a04,
    0x27b40,0x3dde8,0x3dd24,0x408b0,0x551ac,0x51808,0x55dfc,0x4e5a8,0x34acc,0x3dfc0,0x3e228]
effects=[];responses=[0,0,0,0]
def hook(uc,address,size,_):
    a=uc.reg_read(UC_X86_REG_EAX);b=uc.reg_read(UC_X86_REG_EDX);c=uc.reg_read(UC_X86_REG_EBX)
    if address in [0x40460,0x51948,0x38170]:pass
    elif address in [0x389a8,0x512d0,0x3fa98,0x408b0,0x4e5a8]:b=c=0
    elif address in [0x27b6c,0x3dde8,0x3dd24]:c=0
    else:a=b=c=0
    effects.extend([address,a,b,c])
    mutations=responses[3]
    if mutations&1 and address in [0x2b4a4,0x2b4f0,0x2b548,0x2bb04,0x2b9d0]:
        weapon=get(0xa0aae)
        if weapon<5:
            ammo=[0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca][weapon]
            if get(ammo):put(ammo,get(ammo)-1)
        put(0xa0aac,0)
    if mutations&2 and address==0x59edd:put(0xa0c94,0x20,4)
    if mutations&4 and address==0x38958:put(0xa0aae,(get(0xa0aae)+1)%5)
    if mutations&8 and address==0x5de4c:put(0x3fae1a,0x00080010,4)
    if address==0x38628:uc.reg_write(UC_X86_REG_EAX,responses[0])
    if address==0x10a50:uc.reg_write(UC_X86_REG_EAX,responses[1])
    if address==0x38170:uc.reg_write(UC_X86_REG_ECX,responses[2])
for address in service_addresses:
    mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
rng=random.Random(0xC017201)
def put(address,value,size=2):mu.mem_write(address,(value&((1<<(size*8))-1)).to_bytes(size,'little'))
def get(address,size=2):return int.from_bytes(mu.mem_read(address,size),'little')
def setup(terrain=0):
    # Poison the whole mutable player neighborhood, then establish valid geometry.
    mu.mem_write(0xa0a00,rng.randbytes(0x300))
    grid=bytearray(12*12*16)
    for i in range(12*12):grid[i*16+10]=terrain;grid[i*16+11]=4;grid[i*16+13]=80
    mu.mem_write(0x330000,bytes(grid));put(0xa0c30,12);put(0xa0c32,12);put(0xa0c40,0x330000,4)
    put(0xa0c34,0);put(0xa0c36,0);put(0xa0ab0,0x330000+(5*12+5)*16,4)
    put(0xa0a48,2800);put(0xa0a4a,128);put(0xa0a4c,2800)
    put(0xa0a58,0);put(0xa0a5a,0);put(0xa0a5c,2048);put(0xa0a60,0);put(0xa0a62,0);put(0xa0a64,0)
    put(0xa0a50,48);put(0xa0a52,0);put(0xa0a54,2048)
    put(0xa0c90,0,4);put(0xa0cc7,0,1);put(0xa0c98,0,4);put(0xa0c94,0,4);put(0xa0c7c,0,4)
    for address,value in [(0x94b8a,2),(0x94b8c,4),(0x94b88,8),(0x94b8e,1),(0x94b7a,16),(0x94b7c,64),(0x94b7e,128),(0x94b90,32)]:put(address,value)
    put(0x94b9c,0,1);put(0x9cde0,32);put(0x3fae1a,0,4);put(0x3f6e7f,0,1)
    put(0xa0ab4,0);put(0xa0abc,0);put(0xa0ae4,0);put(0xa0ab8,100);put(0xa0b44,0)
    for address,value in [(0xa0b36,16),(0xa0b38,0),(0xa0b3a,0xff80),(0xa0b3c,128),(0xa0b42,48),(0xa0b40,0),(0xa0b34,0),(0xa0b4c,768),(0xa0b54,2)]:put(address,value)
    put(0xa0aac,0);put(0xa0aae,0);put(0xa0abe,15);put(0xa0ac0,6)
    put(0x95f9c,1,1);put(0x9e0a8,0,1);put(0x9cddd,0,1);put(0x9cddc,0,1);put(0x9d7fe,0)
    put(0xa0c24,5);put(0xa0c26,5);put(0xa0c28,4);put(0xa0c50,0x340000,4);put(0xa0bc0,0,4)
    for i in range(16):mu.mem_write(0x340000+i*8,bytes([5+i%2,5+i%2,0,0,0,0,0,0]))
    task=bytearray(32);struct.pack_into('<I',task,4,0x3fbc0);struct.pack_into('<I',task,24,0x300020);mu.mem_write(0x300000,bytes(task))

entries=[0x3e894,0x3cef0,0x3d0d4,0x40098,0x3fbc0]
cases=0
def compare(operation):
    global cases
    debug={hex(a):hex(get(a,n)) for a,n in [(0xa0c98,4),(0xa0c7c,4),(0xa0a52,2),(0xa0b34,2),(0xa0ab4,2),(0xa0b3e,2),(0xa0b40,2),(0x3fae1a,4)]}
    for address,length in windows:
        before=bytes(mu.mem_read(address,length));C.memmove(base+address,before,length)
    effects.clear()
    entry=entries[operation] if operation<5 else get(0x300004,4)
    run(entry,0x300000,instruction_limit=300000)
    log=(U32*256)();response=(U32*4)(*responses)
    n=dll.control_probe(native,SIZE,operation,0x300000,response,log)
    for address,length in windows:
        expected=bytes(mu.mem_read(address,length));actual=C.string_at(base+address,length)
        assert actual==expected,('memory',operation,hex(entry),debug,hex(address+next(i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b)),
                                [(hex(address+i),a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b][:12])
    assert list(log)[:n]==effects,('services',operation,hex(entry),list(log)[:n],effects)
    cases+=1

counts={}
# Control paths including chord priority, signed masks, slopes, mouse and pause.
for terrain in [0,5,9,*range(0x2d,0x39)]:
    for held in [0,0x1000,0x4000,0x1004,0x1008,0x4004,0x4008,4,8,0x8000,0x2000,0x200,0x1010,0x1041,0x80,0x20,0xffff]:
        setup(terrain);put(0xa0c98,held,4);put(0xa0c94,held,4);put(0xa0a52,rng.choice([0,512,1536,2048,2560,3584,4095]));compare(0)
for _ in range(1800):
    setup(rng.choice([0,5,9,0x2d,0x30,0x35]));put(0xa0c98,rng.getrandbits(32),4);put(0xa0c94,rng.getrandbits(32),4);put(0xa0c7c,rng.getrandbits(32),4)
    put(0xa0aac,rng.randrange(7));put(0xa0aae,rng.randrange(7));put(0xa0b57,rng.randrange(256),1)
    put(0xa0a52,rng.getrandbits(16));put(0xa0a50,rng.getrandbits(16));put(0xa0a64,rng.getrandbits(16));put(0xa0b38,rng.getrandbits(16));put(0xa0b34,rng.getrandbits(16))
    put(0xa0b40,rng.choice([0,1]));put(0xa0b3e,rng.getrandbits(16));put(0x3fae1a,rng.getrandbits(32),4)
    put(0x3f6e7f,rng.choice([0,1]),1);put(0xa0c92,rng.choice([0,2]),1);put(0xa0cc7,rng.choice([0,0,0,1]),1)
    put(0xa0ab4,rng.getrandbits(16));put(0x94b9c,rng.getrandbits(8),1);put(0x95f9c,rng.choice([0,1,2]),1)
    for a in [0xa0abe,0xa0ac0,0xa0ac2,0xa0ac4,0xa0ac6,0xa0ac8,0xa0aca,0xa0acc,0xa0ace,0xa0ad0]:put(a,rng.choice([0,1,2,100,0xffff]))
    responses[:]=[rng.choice([0,1]),rng.choice([0,0xffff,rng.getrandbits(32)]),rng.getrandbits(16),0]
    compare(0)
counts['input_and_weapon_intent']=cases
start=cases
for operation in [1,2,3]:
    for _ in range(400):
        setup();put(0xa0aae,rng.randrange(7));put(0xa0aac,rng.randrange(6));put(0x95f9c,rng.choice([0,1,2]),1);put(0x94b9c,rng.getrandbits(8),1)
        for a in [0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca,0xa0ad0]:put(a,rng.choice([0,1,0xffff]))
        responses[2]=rng.getrandbits(16);compare(operation)
counts['fire_and_inventory_helpers']=cases-start
start=cases
for direction in range(10):
    for mode in [0,1]:
        for preserve in [0,1]:
            for cheat in [0,0x20]:
                setup(rng.choice([0,9,0x31]));put(0xa0c28,direction);put(0x9e0a8,mode,1);put(0x9cddd,preserve,1);put(0x94b9c,cheat,1)
                put(0xa0c90,rng.choice([0,0x80000,0xffffffff]),4);put(0xa0ab8,rng.getrandbits(16));put(0xa0a52,rng.getrandbits(16));put(0xa0bc0,rng.randrange(4),4)
                responses[2]=rng.getrandbits(16);compare(4)
counts['player_initialization']=cases-start
start=cases
# Deterministic empty-weapon/reload/switch branches for each weapon and provider result.
for weapon in range(5):
    for choice in [0,1]:
        for reserve in [0,1]:
            for grenade in [0,1]:
                setup();put(0xa0aae,weapon)
                for a in [0xa0abe,0xa0ac0,0xa0ac2,0xa0ac4,0xa0ac6,0xa0ac8,0xa0aca,0xa0acc,0xa0ace]:put(a,0)
                put(0xa0ac0,reserve);put(0xa0ac6,reserve);put(0xa0ace,reserve);put(0xa0ac8,grenade)
                responses[0]=choice;compare(0)
for _ in range(300):
    setup(rng.choice([0,8,9,0x32]));put(0x9e0a8,rng.choice([0,1]),1);put(0x9cddd,rng.choice([0,1]),1)
    put(0xa0c34,rng.choice([0,3000,5000]));put(0xa0c36,rng.choice([0,3000,5000]));put(0xa0c28,rng.randrange(10))
    put(0xa0bc0,rng.randrange(4),4);put(0xa0c90,rng.getrandbits(32),4);put(0x9ce5c,rng.choice([0,1]));compare(4)
counts['targeted_reload_and_origin_cases']=cases-start
start=cases
# Real initialization -> input -> movement -> camera -> death -> respawn handoff,
# including the scratch matrix, with original helpers executing unstubbed.
for sequence in range(16):
    setup();put(0x9e0a8,1,1);put(0x9ce5c,sequence%2);responses[:]=[0,0,25,0]
    compare(5)
    for frame in range(32):
        previous=get(0xa0c98,4)
        held=0x1000 if frame<8 else 0
        if sequence&1 and frame<6:held|=4
        put(0xa0c7c,previous,4);put(0xa0c98,held,4);put(0xa0c94,~previous&held,4)
        if frame==16:put(0xa0c90,get(0xa0c90,4)|0x20,4)
        if frame==23:put(0xa0c90,get(0xa0c90,4)|0x80000,4)
        compare(5)
counts['integrated_control_and_respawn_ticks']=cases-start
start=cases
for binding in [0x94b8a,0x94b8c,0x94b88,0x94b8e,0x94b7a,0x94b7c,0x94b7e,0x94b90]:
    for mask in [1,0x8000,0xffff]:
        setup();put(binding,mask);put(0xa0c98,0x80000000,4);put(0xa0c94,0x80000000,4);compare(0)
for weapon in range(5):
    for mutation in [1,2,4,8,15]:
        setup();responses[3]=mutation;put(0xa0aae,weapon);put(0xa0c98,0x80,4);put(0xa0c94,0xa0,4)
        for a in [0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca]:put(a,1)
        put(0xa0ac6,1);put(0xa0ace,1);put(0x3f6e7f,1,1)
        if mutation==4:put(0xa0aac,4);put(0xa0b57,128,1);put(0xa0ab5,4,1)
        compare(0)
responses[3]=0
counts['signed_bindings_and_mutating_services']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],counts=counts,new_comparisons=cases,
    limitations=['Animation, selection, projectile/device/audio/UI/region and miscellaneous effect providers are scripted stubs.',
                 'Primary and secondary fire control dispatch is recovered, not projectile simulation or ammo consumption inside those providers.',
                 'Mapped overlapping globals are a transitional representation; original data addresses are never executed.'])
(ROOT/'decompilation/reports/player_control_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
