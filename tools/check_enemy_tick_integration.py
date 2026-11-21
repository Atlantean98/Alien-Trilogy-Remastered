"""Connected enemy construction with original scheduler and animation programs."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
o.dll.enemy_integration_probe.argtypes=[C.POINTER(U8),U32,U32,U32];o.dll.enemy_integration_probe.restype=U32
windows=[(0x94000,0xd000),(0x22ce00,0x100),(0x234000,0x1000),(0x236300,0x400),(0x310000,0x2000),(0x35a000,0x2000),(0x22d300,0x100),(0x3f9400,0x200),(0x320000,0x10000),(0x330000,0x2000),(0x340000,0x3000)]
rng=random.Random(0x2dfc8);effects=[];cases=0;counts={};POOL=0x320000;SPAWNS=0x330000;GRID=0x340000;TASKS=0x310100
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
animation_tables=bytes(o.mu.mem_read(0x9c000,0x1000));angle_lookup=bytes(o.mu.mem_read(0x97000,0x640));root_lookup=bytes(o.mu.mem_read(0x9d5e2,512));trig=bytes(o.mu.mem_read(0x9763c,8192))
def setup(kind=2,index=0):
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    put(0x2365e4,POOL);put(0x2365d8,POOL);put(0x2365d4,SPAWNS);put(0x2365e0,0 if rng.randrange(2) else POOL+208)
    for i in range(128):put(POOL+i*208+200,POOL+(i+1)*208 if i<127 else 0);put(POOL+i*208+0x70,i,1)
    put(0x310800,0,1);put(0x310804,TASKS);put(0xa0c92,0,1);put(0x96234,rng.randrange(4),2);put(0x9e0a8,rng.randrange(2),1)
    put(0xa0c30,16,2);put(0xa0c32,16,2);put(0xa0c34,4096,2);put(0xa0c36,4096,2);put(0xa0c40,GRID)
    for i in range(256):put(GRID+i*16+11,rng.randrange(30),1);put(GRID+i*16+13,rng.randrange(40,90),1)
    p=SPAWNS+index*20;put(p,kind,1);put(p+1,rng.randrange(16),1);put(p+2,rng.randrange(16),1)
    put(p+4,rng.randrange(12),1);put(p+5,rng.randrange(5),1)
    o.mu.mem_write(0x9c000,animation_tables)
    put(0x22ceb4,0x310000);o.run(0x12258)
    for resource in [0x234410,0x23440c,0x23442c,0x234408,0x234434,0x2343fc,0x234420,0x234428,0x234438,0x234430,0x234424,0x23443c]:put(resource,0x35a000)
    put(0x35a000,32,2)
    return p
def compare(index=0):
    global cases
    for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    effects.clear();expected=o.run(0x2dfc8,index,instruction_limit=1000000)
    log=(U32*256)();count=U32();actual=o.dll.enemy_integration_probe(native,SIZE,1,index)
    assert actual==expected,('return',cases,hex(actual),hex(expected))
    for addr,n in windows:
        have=C.string_at(base+addr,n);want=bytes(o.mu.mem_read(addr,n))
        assert have==want,('memory',cases,[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
    cases+=1
o.dll.enemy_tick_integration_probe.argtypes=[C.POINTER(U8),U32,U32,C.POINTER(U32),C.POINTER(U32)];o.dll.enemy_tick_integration_probe.restype=U32
providers=[0x31950,0x33208,0x329e0,0x32ddc,0x334e4,0x31d30,0x32500,0x2fd18,0x2fe00,0x40604,0x2ab70,0x2a8bc]
def provider(uc,address,size,_):
    a=uc.reg_read(UC_X86_REG_EAX);b=uc.reg_read(UC_X86_REG_EDX)
    from unicorn.x86_const import UC_X86_REG_EBX
    effects.extend([address,a,b if address in [0x31d30,0x32500] else 0,uc.reg_read(UC_X86_REG_EBX) if address in [0x31d30,0x32500] else 0])
    uc.reg_write(UC_X86_REG_EAX,get(0x35b000+((address>>2)&255)*4))
for address in providers:
    o.mu.hook_add(UC_HOOK_CODE,provider,begin=address,end=address);o.mu.mem_write(address,b'\xc3');o.mu.ctl_remove_cache(address,address+32)
for kind in [2,3,4,5,6,8,9,11,12,13,14]:
    for distance in [200,800,1200,2200,8000,18000]:
        setup(kind);o.mu.mem_write(0x97000,angle_lookup);o.mu.mem_write(0x9d5e2,root_lookup);o.mu.mem_write(0x9763c,trig)
        put(0x9e0a8,0,1);put(0x95f9c,1,1);put(0x94b9d,1,1);put(0xa0ab5,1,1);put(0xa0b30,30,2);put(0xa0ae4,0,2)
        put(0xa0cbe,0xffff0000);put(0xa0adc,1,2)
        e=o.run(0x2dfc8,0);assert e
        task=get(e+0xc0);put(e+0x52,0,2)
        put(0xa0a48,get(e,2),2);put(0xa0a4a,get(e+2,2),2);put(0xa0a4c,get(e+4,2)+distance,2)
        for tick in range(40):
            for address in providers:put(0x35b000+((address>>2)&255)*4,rng.randrange(2))
            for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
            effects.clear();o.run(0x33cc8,task,instruction_limit=1000000)
            log=(U32*1024)();count=U32();o.dll.enemy_tick_integration_probe(native,SIZE,task,log,C.byref(count))
            assert list(log)[:count.value]==effects,('calls',kind,distance,tick,list(log)[:count.value],effects)
            for addr,n in windows:
                have=C.string_at(base+addr,n);want=bytes(o.mu.mem_read(addr,n))
                assert have==want,('memory',kind,distance,tick,[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
            cases+=1
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,
    counts={'consecutive_main_ticks_native_distances_yaw_facing_attacks_animation_and_scheduler':cases},recovered_original_entries=[],
    limitations=['Movement/collision, navigation/line-of-sight, rendering/projection, projectile emission, attachment and enemy sound remain observed providers.', 'Fixed positions and a player damage cooldown are supplied; these are connected core checks, not live gameplay.'])
(o.ROOT/'decompilation/reports/enemy_tick_integration_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
