"""Viewport, clear and complete frame traversal against original instructions."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
o.dll.software_frame_probe.argtypes=[C.POINTER(U8),U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
o.dll.software_frame_probe.restype=None
rng=random.Random(0x488cc);effects=[];cases=0;counts={}
windows=[(0x9d400,0x200),(0x28c800,0x26000),(0x310800,0x100),(0x390000,0x1000),(0x3f6d00,0x200),(0x3fa900,0x100)]
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(size*8))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
def provider(uc,address,size,_):
    packet=0 if address==0x577c8 else uc.reg_read(UC_X86_REG_EAX)
    effects.extend([address,packet,get(0x9d544),get(0x3f6e48),get(0x3fa91f,1)])
    if address==0x577c8:put(0x3fa91f,get(0x310820,1),1)
    else:
        put(0x3fa91f,get(0x3fa91f,1)+get(0x310821,1),1)
        if get(0x310822,1):put(packet,get(packet+8))
for addr in [0x577c8,0x5a000]:
    o.mu.hook_add(UC_HOOK_CODE,provider,begin=addr,end=addr);o.mu.mem_write(addr,b'\xc3');o.mu.ctl_remove_cache(addr,addr+32)
def tick(uc,address,size,_):put(0x3fa91f,get(0x3fa91f,1)+1,1)
o.mu.hook_add(UC_HOOK_CODE,tick,begin=0x489b1,end=0x489b1)
def compare(op,head=0):
    global cases
    for a,n in windows:C.memmove(base+a,bytes(o.mu.mem_read(a,n)),n)
    effects.clear();o.run([0x467f4,0x5c4fe,0x488cc][op],head,instruction_limit=2000000)
    log=(U32*1024)();count=U32();o.dll.software_frame_probe(native,SIZE,op,head,log,C.byref(count))
    assert list(log)[:count.value]==effects,('calls',op,list(log)[:count.value],effects)
    for a,n in windows:
        actual=C.string_at(base+a,n);expected=bytes(o.mu.mem_read(a,n))
        assert actual==expected,('memory',op,[(hex(a+i),x,y) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y][:12])
    cases+=1
for i in range(240):
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    put(0x9d50c,[0,1,2,3,4,5,0xffffffff][i%7]);put(0x3f6d78,rng.getrandbits(32));compare(0)
counts['viewport_modes_and_invalid_mode_preservation']=cases
start=cases
for i in range(20):
    o.mu.mem_write(0x28c800,rng.randbytes(0x26000));put(0x9d544,0x28c888+i*4);compare(1)
counts['current_framebuffer_clear']=cases-start
start=cases
for i in range(500):
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    put(0x9d544,0x28c888);put(0x9d50c,i%7);put(0x3f6d78,320)
    put(0x9d504,319);put(0x9d508,239);put(0x3f6e44,320);put(0x3f6e4c,240)
    put(0x3f6e85,i%2,1);put(0x3f6e86,(i//2)%2,1)
    put(0x310820,i%5,1);put(0x310821,i%9,1);put(0x310822,i%2,1)
    n=i%12
    for j in range(n):
        packet=0x390000+j*32
        put(packet,packet+32 if j+1<n else 0);put(packet+8,packet+64 if j+2<n else 0)
        callbacks=[0,0x5a000,0x48bac] if i<300 else [0,0x5a000,0x48bac,0x4bc88,0x48a3c,0x4bc38]
        put(packet+4,callbacks[(i+j)%len(callbacks)])
        put(packet+16,10,2);put(packet+18,8,2)
        for offset,v in zip([12,14,20,22],[j*2,j*3,j*2+15,j*3+10]):put(packet+offset,v,2)
    compare(2,0x390000 if n else 0)
counts['frame_traversal_pixels_callback_mutation_presentation_and_timer']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,
    limitations=['Hardware presentation remains a provider; host polling replaces interrupt timer advancement.', 'Packet callbacks must preserve EDX, the original traversal cursor register. Native line callbacks and controlled next-link mutation are covered.'])
(o.ROOT/'decompilation/reports/software_frame_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
