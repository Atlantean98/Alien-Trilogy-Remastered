"""Compare native scheduler state and callback order with original x86.

Imports the common oracle, which first reruns the existing six-routine checks.
"""
import ctypes as C
import json
import random
import struct
import check_original_behavior as oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX

mu, dll, run, ROOT = oracle.mu, oracle.dll, oracle.run, oracle.ROOT
U32 = C.c_uint32
dll.scheduler_probe.argtypes=[C.POINTER(U32),C.c_uint,U32,U32,C.POINTER(U32),C.POINTER(C.c_uint)]
dll.scheduler_probe.restype=U32
BASE=0x300000
GLOBALS=[0x22ceb4,0x22ceb8,0x22cebc,0x22cec0]
mu.mem_map(0x710000,0x1000)
log=[]
def hook(uc,address,size,_):
    log.append(uc.reg_read(UC_X86_REG_EAX))
for a in [0x710000,0x710020,0x710040,0x710060]:
    mu.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
def branch(opcode,at,target): return bytes([opcode])+struct.pack('<I',(target-at-5)&0xffffffff)
mu.mem_write(0x710000,branch(0xe9,0x710000,0x13f34))
mu.mem_write(0x710020,branch(0xe9,0x710020,0x12394))
code=b'\xc6\x05'+struct.pack('<I',0x95f9c)+b'\x01'
mu.mem_write(0x710040,code+branch(0xe9,0x710040+len(code),0x13f34))
code=b'\x50\x8b\x00\xba'+struct.pack('<I',0x710000)
code+=branch(0xe8,0x710060+len(code),0x12364)+b'\x58'
code+=branch(0xe9,0x710060+len(code),0x13f34)
mu.mem_write(0x710060,code)

addresses=[0x12258,0x12364,0x12394,0x13f34,0x123e0,0x12400,0x122c0]
checks=0
def step(snapshot,op,first=0,second=0):
    global checks
    for a,v in zip(GLOBALS,snapshot[:4]): mu.mem_write(a,struct.pack('<I',v))
    mu.mem_write(0x95f9c,bytes([snapshot[4]]))
    mu.mem_write(BASE,struct.pack('<2048I',*snapshot[5:]))
    if op==5:
        mu.mem_write(0x9d588,struct.pack('<I',first))
        mu.mem_write(0xa0cc2,bytes([bool(second)]))
    log.clear()
    result=run(addresses[op],first,second,instruction_limit=3000000)
    expected=[struct.unpack('<I',mu.mem_read(a,4))[0] for a in GLOBALS]
    expected += [mu.mem_read(0x95f9c,1)[0]]
    expected += list(struct.unpack('<2048I',mu.mem_read(BASE,8192)))
    native=(U32*len(snapshot))(*snapshot)
    nativeLog=(U32*65536)()
    count=C.c_uint()
    actualResult=dll.scheduler_probe(native,op,first,second,nativeLog,C.byref(count))
    if op==1: assert actualResult==result, ('return',op,first,second,actualResult,result)
    assert list(native)==expected, ('state',op,first,second,next((i,a,b) for i,(a,b) in enumerate(zip(native,expected)) if a!=b))
    assert list(nativeLog)[:count.value]==log, ('callbacks',op,first,second)
    checks+=1
    return expected,result

rng=random.Random(0x5C4ED)
def reset():
    # Poisoned payloads and previous links detect accidental extra clearing.
    s=[BASE,rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(32),173]
    s += [rng.getrandbits(32) for _ in range(2048)]
    return step(s,0)[0]
def active(s):
    result=[]; a=s[1]
    while a:
        assert a not in result and len(result)<256
        result.append(a); a=s[5+(a-BASE)//4+6]
    return result

# All 256 entries, exhaustion, equal/signed priorities and deterministic reuse.
s=reset()
priorities=[0,0,0x80000000,0x7fffffff,0xffffffff,1]
for i in range(256): s,_=step(s,1,priorities[i%len(priorities)],0x710000)
assert s[2]==0
s,result=step(s,1,0,0x710000)
assert result==0
s,_=step(s,4)
for a in active(s)[::2]: s,_=step(s,2,a)
for i in range(128): s,_=step(s,1,i,0x710000)

# Stateful traces exercise ordering and removal at head/tail/middle.
for trace in range(24):
    s=reset()
    for i in range(120):
        nodes=active(s)
        choice=rng.randrange(5)
        if not nodes or (choice<2 and s[2]):
            s,_=step(s,1,rng.choice(priorities+[rng.getrandbits(32)]),rng.choice([0x710000,0x710020]))
        elif choice==2: s,_=step(s,2,rng.choice(nodes))
        elif choice==3: s,_=step(s,3,rng.choice(nodes))
        else: s,_=step(s,4)

# Callback insertion/removal and callback writes to the shared countdown byte.
for callbacks in [[0x710020]*8,[0x710060]*6,[0x710000,0x710020,0x710060,0x710000],
                  [0x710040,0x710000],[0x710000]*3,[]]:
    for timing in [0,1,2,3,20,509,510,511,512,0x7fffffff,0x80000000,0xffffffff]:
        for enabled in [0,1]:
            s=reset()
            for i,callback in enumerate(callbacks): s,_=step(s,1,i%3,callback)
            s,_=step(s,5,timing,enabled)

# Direct insertion of valid detached nodes, including all equal priorities.
s=reset()
for i in range(16):
    # Build detached node without using allocator, preserving free-list separation.
    a=BASE+i*32
    s[2]=0
    s[5+i*8]=i%3
    s[5+i*8+1]=0x710000
    s,_=step(s,6,a)
s,_=step(s,4)

report=dict(status='passed',reference_sha256=oracle.manifest['sha256'],
    scheduler_state_comparisons=checks,previous_routine_comparisons=7677,
    total_comparisons=checks+7677,
    compared=['all 256 node records including payload and stale fields','active/free heads and cursor',
              'create return handle','callback invocation order','shared tick countdown'],
    scenarios=['full pool and exhaustion','signed/equal priorities','node reuse','callback self-removal',
               'callback insertion','callback countdown mutation','timing wraparound','empty dispatch'],
    limitations=['Valid task handles and structurally valid lists only.',
                 'Controlled callback stubs exercise mutations; actual gameplay callbacks remain unrecovered.',
                 'Allocator wrapper and recursive/nested scheduler dispatch are not covered.'])
(ROOT/'decompilation/reports/scheduler_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
