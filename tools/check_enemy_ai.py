"""Enemy distance, unfolding, spawn triggers and attack selection against x86."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
o.dll.enemy_ai_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)];o.dll.enemy_ai_probe.restype=U32
rng=random.Random(0x336d0);effects=[];cases=0;counts={};E=0x320000;T=0x321000;SPAWNS=0x330000;CELL=0x340000
windows=[(0x94000,0xd000),(0x236300,0x400),(0x310000,0x1000),(E,0x2000),(SPAWNS,0x2000),(CELL,0x1000)]
lookup=bytes(o.mu.mem_read(0x9d5e2,512));angle_lookup=bytes(o.mu.mem_read(0x97000,0x640))
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
def normalize(address,a,b,c):
    if address==0x2ee40:return [address,a,b&65535,0]
    if address==0x3298c:return [address,a,b&65535,c&65535]
    if address==0x3dee8:return [address,a&65535,b&65535,0]
    if address==0x3df14:return [address,a&65535,b&65535,c&65535]
    if address==0x2dfc8:return [address,a&65535,0,0]
    if address==0x2fe00:return [address,a,0,0]
    return [address,a,b,c]
def hook(uc,address,size,_):
    effects.extend(normalize(address,uc.reg_read(UC_X86_REG_EAX),uc.reg_read(UC_X86_REG_EDX),uc.reg_read(UC_X86_REG_EBX)))
    uc.reg_write(UC_X86_REG_EAX,get(0x310000))
for address in [0x2ee40,0x3298c,0x3dee8,0x3df14,0x2dfc8,0x2fe00,0x31d30]:
    o.mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address);o.mu.mem_write(address,b'\xc3');o.mu.ctl_remove_cache(address,address+32)
def setup():
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    o.mu.mem_write(0x97000,angle_lookup);o.mu.mem_write(0x9d5e2,lookup);put(E+0x78,CELL);put(T+8,E);put(0x2365d4,SPAWNS)
    put(0x310000,rng.choice([0,1,65536,0xffffffff]));put(E+0x74,rng.randrange(22),1);put(E+0x73,rng.randrange(6),1)
def compare(routine,a=E,b=0,returns=False):
    global cases
    for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    effects.clear();want=o.run(routine,a,b,instruction_limit=1000000)
    log=(U32*256)();count=U32();have=o.dll.enemy_ai_probe(native,SIZE,routine,a,b,log,C.byref(count))
    if returns:assert have==want,('return',hex(routine),cases,have,want)
    actual=[]
    for i in range(0,count.value,4):actual.extend(normalize(*list(log)[i:i+4]))
    assert actual==effects,('calls',hex(routine),cases,actual,effects)
    for addr,n in windows:
        have=C.string_at(base+addr,n);want=bytes(o.mu.mem_read(addr,n))
        assert have==want,('memory',hex(routine),cases,[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(have,want)) if x!=y][:12])
    cases+=1
start=cases
for n in list(range(256))+[255,256,1023,1024,65535,65536,0x7fffffff]+[rng.randrange(0x80000000) for _ in range(500)]:
    setup();compare(0x4d15c,n,returns=True)
for i in range(500):
    setup();compare(0x31b0c,E,E+0x38,True);compare(0x31b68);compare(0x31c18)
counts['original_lookup_root_and_player_target_distance_categories']=cases-start
start=cases
for i in range(1500):
    setup();compare(0x26504,E,E+0x38,True)
for dx in [-16385,-16384,-1024,-1023,-1,0,1,1023,1024,16384,16385]:
    for dz in [-16385,-16384,-1024,-1023,-1,0,1,1023,1024,16384,16385]:
        setup();put(E,dx,2);put(E+4,dz,2);put(E+0x38,0,2);put(E+0x3c,0,2);compare(0x26504,E,E+0x38,True)
counts['yaw_lookup_scaling_quadrants_and_boundaries']=cases-start
start=cases
for i in range(600):
    setup();compare(0x2eb40,E,rng.choice([0xffffffff,0xffff,0,1,8,9,65534]));compare(0x2ebb4,rng.randrange(256),rng.randrange(256))
counts['folded_species_conversion_and_spawn_trigger_saturation']=cases-start
start=cases
for kind in range(22):
    for category in range(6):
        for i in range(15):
            setup();put(E+0x74,kind,1);put(E+0x73,category,1);put(E+0x14,rng.choice([0,1536,1537,0xffffffff]));compare(0x336d0,T,E)
            # Bias the view gate to exercise hits and line-of-sight provider results.
            if i%2:put(CELL+6,0xffff0000);put(0xa0cbe,0xffff0000);put(E+0x40,2048,2)
            compare(0x33a0c)
counts['all_species_attack_selection_close_long_ranges_view_and_hit_gates']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    recovered_original_entries=['0x26504','0x4d15c','0x31b0c','0x31b68','0x31c18','0x2eb40','0x2ebb4','0x336d0','0x33a0c'],
    limitations=['Attack animation, knockback, face-hug attachment test, line of sight, damage and constructor calls are observed providers here.', 'Lookup-root inputs exclude negative signed values, for which the original does not terminate.'])
(o.ROOT/'decompilation/reports/enemy_ai_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
