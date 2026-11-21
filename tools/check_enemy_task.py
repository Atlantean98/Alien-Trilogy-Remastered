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
windows=[(0x94000,0xd000),(0x22ce00,0x100),(0x236300,0x400),(0x310000,0x1000),(E,0x2000),(SPAWNS,0x2000),(CELL,0x1000)]
lookup=bytes(o.mu.mem_read(0x9d5e2,512))
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
single=[0x31950,0x31b68,0x33208,0x329e0,0x32ddc,0x334e4,0x33484,0x33430,0x30ecc,0x31c18,0x31cbc,0x2ab70,0x2a8bc,0x33a0c,0x12394,0x2fd18,0x2debc,0x2840c]
providers=single+[0x26504,0x328d8,0x31d30,0x32500,0x3298c,0x336d0,0x2ee40,0x12ed4,0x40604]
def normalize(address,a,b,c):
    if address in single:return [address,a,0,0]
    if address==0x12ed4:return [address,0,0,0]
    if address==0x40604:return [address,a&65535,b,0]
    if address in [0x26504,0x336d0]:return [address,a,b,0]
    if address in [0x328d8,0x2ee40]:return [address,a,b&65535,0]
    if address==0x3298c:return [address,a,b&65535,c&65535]
    return [address,a,b,c]
def hook(uc,address,size,_):
    effects.extend(normalize(address,uc.reg_read(UC_X86_REG_EAX),uc.reg_read(UC_X86_REG_EDX),uc.reg_read(UC_X86_REG_EBX)))
    uc.reg_write(UC_X86_REG_EAX,get(0x310100+((address>>2)&255)*4))
for address in providers:
    o.mu.hook_add(UC_HOOK_CODE,hook,begin=address,end=address);o.mu.mem_write(address,b'\xc3');o.mu.ctl_remove_cache(address,address+32)
def setup():
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    o.mu.mem_write(0x9d5e2,lookup);put(E+0x78,CELL);put(T+8,E);put(0x2365d4,SPAWNS)
    put(E+200,E+208);put(E+204,rng.choice([0,E+416]));put(0x310000,rng.choice([0,1,65536,0xffffffff]));put(E+0x74,rng.randrange(22),1);put(E+0x73,rng.randrange(6),1)
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
for state in range(11):
    for kind in [1,2,3,4,5,6,7,8,9,10,11,12,13,14,19,255]:
        for i in range(100):
            setup();put(E+0x6f,state,1);put(E+0x74,kind,1);put(E+0x73,i%6,1)
            put(E+0x14,rng.choice([0,1024,1536,1537,4096,4097,0xffffffff]))
            put(E+0x68,rng.choice([0,1,2,60]));put(E+0x52,rng.choice([0,1,2,65535]),2)
            put(E+0x42,rng.randrange(4096),2);put(E+0x44,get(E+0x42,2) if i%2 else rng.randrange(4096),2)
            put(CELL+10,rng.choice([0,6,12,20,44,45,46,47,48,49,50,51,52,53]),1)
            for address in providers:put(0x310100+((address>>2)&255)*4,rng.choice([0,1,65536,0xffffffff]))
            compare(0x33cc8,T)
counts['all_main_task_states_species_tick_gates_decisions_death_and_task_cursor']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,recovered_original_entries=['0x33cc8'],
    limitations=['Movement, distance, yaw, navigation/line-of-sight, attack, sound, animation and scheduler helpers are observed providers.', 'These verify the main state-machine body, not full enemy gameplay integration.'])
(o.ROOT/'decompilation/reports/enemy_task_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
