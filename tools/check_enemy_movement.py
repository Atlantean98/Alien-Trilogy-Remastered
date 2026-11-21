"""Original enemy axis collision and movement versus native C++."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
o.dll.enemy_movement_probe.argtypes=[C.POINTER(U8)]+[U32]*7+[C.POINTER(U32),C.POINTER(U32)];o.dll.enemy_movement_probe.restype=U32
rng=random.Random(0x31950);cases=0;counts={};effects=[];E=0x320000;GRID=0x340000
windows=[(0x94000,0xd000),(E,0x1000),(GRID,0x10000)]
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
def provider(uc,address,size,_):effects.extend([address,uc.reg_read(UC_X86_REG_EAX)])
o.mu.hook_add(UC_HOOK_CODE,provider,begin=0x40b5c,end=0x40b5c);o.mu.mem_write(0x40b5c,b'\xc3');o.mu.ctl_remove_cache(0x40b5c,0x40b5d)
def setup():
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    put(0xa0c30,32,2);put(0xa0c32,32,2);put(0xa0c34,8192,2);put(0xa0c36,8192,2);put(0xa0c40,GRID)
    for i in range(1024):
        p=GRID+i*16;put(p+10,rng.choice([0,0,0,0,1,6,12,19,20,24,44,45,46,47,48,49,50,51,52,53,60]),1);put(p+12,rng.choice([0,0,0,0,0,2,255]),1);put(p+11,rng.randrange(50),1);put(p+13,rng.randrange(40,120),1)
    x=rng.randrange(1024,15000);z=rng.randrange(1024,15000)
    put(E,x-8192,2);put(E+4,z-8192,2);put(E+2,rng.randrange(-1000,6000),2);put(E+10,rng.choice([256,512,768,1536]),2)
    put(E+0x30,rng.randrange(-300,301),2);put(E+0x34,rng.randrange(-300,301),2);put(E+0x32,rng.choice([0,0,0,12,60,128,-12,-120]),2)
    put(E+0x6f,rng.randrange(10),1);put(E+0x78,GRID+((z>>9)*32+(x>>9))*16)
    put(E+0x5a,rng.randrange(2000),2);put(E+0x5c,rng.randrange(3000,8000),2)
    return x,z
def compare(op,x=0,z=0,y=0,negative=0,extra=()):
    global cases
    for a,n in windows+list(extra):C.memmove(base+a,bytes(o.mu.mem_read(a,n)),n)
    effects.clear()
    if op<2:
        o.mu.reg_write(UC_X86_REG_EBX,negative);o.mu.reg_write(UC_X86_REG_ECX,E);want=o.run([0x30fc8,0x314cc][op],x,z)
    elif op<4:
        o.mu.reg_write(UC_X86_REG_EBX,y&0xffffffff);o.mu.reg_write(UC_X86_REG_ECX,z);want=o.run([0x31004,0x31510][op-2],E,x,instruction_limit=1000000)
    else:want=o.run(0x31950,E,instruction_limit=1000000)
    log=(U32*128)();count=U32();have=o.dll.enemy_movement_probe(native,SIZE,op,E,x,z,y&0xffffffff,negative,log,C.byref(count))
    if op<4:assert have==want,('return',op,cases,x,z,y,have,want)
    assert list(log)[:count.value]==effects,('calls',op,cases,list(log)[:count.value],effects)
    for a,n in windows+list(extra):
        have=C.string_at(base+a,n);want=bytes(o.mu.mem_read(a,n))
        assert have==want,('memory',op,cases,[(hex(a+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
    cases+=1
start=cases
for i in range(800):
    setup();compare(i%2,rng.getrandbits(32),rng.getrandbits(32),negative=i%3!=0)
counts['axis_wall_fallback_unsigned_clamps_and_word_velocity']=cases-start
start=cases
for i in range(2500):
    x,z=setup();compare(2+i%2,x,z,rng.randrange(-3000,10000))
counts['axis_collision_cells_occupancy_heights_steps_clearance_and_drop_edges']=cases-start
start=cases
for i in range(1500):
    setup();compare(4)
counts['full_movement_gravity_occupancy_network_and_cell_change']=cases-start
start=cases
for item in json.loads((o.ROOT/'decompilation/reports/asset_inventory.json').read_text())['maps']:
    setup();raw=(o.ROOT/item['source']).read_bytes();payload=raw[item['offset']:item['offset']+item['size']]
    o.mu.mem_write(0x100000,payload);put(0xa0bc8+0xa4,0x100000)
    o.run(0x27530,0xa0bc8,instruction_limit=20000000);o.run(0x27c68,0xa0bc8,instruction_limit=20000000)
    width,height=get(0xa0c30,2),get(0xa0c32,2);grid=get(0xa0c40)
    for sample in range(12):
        if width<3 or height<3:continue
        x=rng.randrange(1,width-1)*512+256;z=rng.randrange(1,height-1)*512+256
        put(E,x-get(0xa0c34,2),2);put(E+4,z-get(0xa0c36,2),2);put(E+0x78,grid+((z>>9)*width+(x>>9))*16)
        put(E+0x30,rng.randrange(-100,101),2);put(E+0x34,rng.randrange(-100,101),2);put(E+0x6f,rng.randrange(9),1)
        for op in [2,3,4]:compare(op,x,z,rng.randrange(-1000,5000),extra=[(0x100000,len(payload))])
counts['all_46_original_level_grids_axis_and_full_movement_samples']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    recovered_original_entries=['0x30fc8','0x314cc','0x31004','0x31510','0x31950'],
    limitations=['Cell-change gameplay service 0x40b5c is an observed provider.', 'Original axis routines require in-bounds footprint samples; random fixtures respect this precondition.'])
(o.ROOT/'decompilation/reports/enemy_movement_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
