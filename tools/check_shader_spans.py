"""Palette-shaded polygon modes compared with original pixels and scratch state."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE,UcError
from unicorn.x86_const import UC_X86_REG_EBX,UC_X86_REG_ECX
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
o.dll.polygon_probe.argtypes=[C.POINTER(U8),U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
o.dll.polygon_probe.restype=U32
windows=[(0x5b3fc,4),(0x9d400,0x200),(0x28c800,0x30000),(0x390000,0x100),(0x3f6cf0,0x1a0)]
rng=random.Random(0x4acb8);effects=[];cases=0;faults=0;counts={}
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
def snapshot(uc,address,size,_):
    effects.append(address);effects.extend(get(a) for a in range(0x3f6cf0,0x3f6e88,4))
o.mu.hook_add(UC_HOOK_CODE,snapshot,begin=0x5a000,end=0x5a000);o.mu.mem_write(0x5a000,b'\xc3');o.mu.ctl_remove_cache(0x5a000,0x5a020)
def setup(real=False):
    for a,n in windows:
        if a>=0x90000 and a not in [0x3e89d8,0x3d88d8,0x100000,0x300000]:o.mu.mem_write(a,rng.randbytes(n))
    put(0x9d544,0x28c888);put(0x9d504,319);put(0x9d508,239);put(0x3f6d78,320)
    put(0x3f6e44,320);put(0x3f6e4c,240);put(0x3f6e83,5,1)
    for i in range(7):put(0x9d510+i*4,0x5b428 if real else 0x5a000)
def compare(op,a=0):
    global cases,faults
    for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    effects.clear();o.mu.reg_write(UC_X86_REG_EBX,0);o.mu.reg_write(UC_X86_REG_ECX,0)
    try:o.run([0x57664,0x4a0e0,0x4acb8,0x5b428,0x4acb8,0x48e40,0x488cc,0x5c519,0x4abc0,0x5a186,0x4acb8,0x5a567,0x5a991,0x5ad18,0x5b0f7][op],a,instruction_limit=4000000)
    except UcError as error:
        # The original also traps on certain subpixel/degenerate divisions.
        # Do not send those invalid fixtures into the native division either.
        if 'EXCEPTION' not in str(error):raise
        faults+=1;return
    log=(U32*4096)();count=U32();o.dll.polygon_probe(native,SIZE,op,a,log,C.byref(count))
    assert list(log)[:count.value]==effects,('effects',op,cases,list(log)[:count.value],effects)
    for addr,n in windows:
        actual=C.string_at(base+addr,n);expected=bytes(o.mu.mem_read(addr,n))
        assert actual==expected,('memory',op,cases,[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y][:15])
    cases+=1
def triangle_fixture():
    for x,y,u,v in zip([0x3f6de0,0x3f6de4,0x3f6de8],[0x3f6df0,0x3f6df4,0x3f6d9c],[0x3f6dd0,0x3f6dd4,0x3f6dd8],[0x3f6d7c,0x3f6d80,0x3f6d84]):
        put(x,rng.randrange(-100,420)*65536);put(y,rng.randrange(-100,300));put(u,rng.randrange(-256,256)*65536);put(v,rng.randrange(-256,256)*65536)

windows += [(0x5a904,12),(0x5ab24,0x38),(0x5ac58,20),(0x5aecf,0x4a),(0x5b038,12),(0x5b294,0x51),(0x3d88d8,0x21000),(0x100000,0x10000)]
o.mu.mem_write(0x100000,rng.randbytes(0x10000));o.mu.mem_write(0x3d88d8,rng.randbytes(0x21000));o.mu.mem_write(0x3f88a0,bytes(rng.randrange(64) for _ in range(768)));o.run(0x4877c)
for mode,entry in [(2,0x5a991),(3,0x5ad18),(4,0x5b0f7)]:
    start=cases
    for i in range(450):
        setup();put(0x3f6d58,rng.randrange(1,20));put(0x3f6cf0,0x28c888+rng.randrange(80)*320)
        put(0x3f6db0,rng.randrange(-80,100)*65536+rng.randrange(65536));put(0x3f6d90,rng.randrange(160,380)*65536+rng.randrange(65536))
        for addr in [0x3f6dcc,0x3f6d44]:put(addr,rng.randrange(-8192,8193))
        for addr in [0x3f6d4c,0x3f6d48,0x3f6dc8,0x3f6dc4]:put(addr,rng.randrange(-131072,131073))
        for addr in [0x3f6da8,0x3f6dac,0x3f6d68,0x3f6d74]:put(addr,rng.getrandbits(32))
        for addr in [0x3f6da4,0x3f6dbc,0x3f6d8c]:put(addr,rng.randrange(64)*128)
        put(0x3f6d28,0x100000);compare(mode+10)
    counts[f'mode_{mode}_shaded_span_pixels_and_clipping']=cases-start
    start=cases
    for i in range(650):
        setup();triangle_fixture();put(0x9d510+mode*4,entry);put(0x3f6e83,mode,1)
        for addr in [0x3f6da4,0x3f6dbc,0x3f6d8c]:put(addr,rng.randrange(64)*128)
        compare(10,0x100000)
    counts[f'mode_{mode}_triangle_to_shaded_pixels']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,original_division_fault_fixtures_excluded=faults,
    recovered_original_entries=['0x5a991','0x5ad18','0x5b0f7'],limitations=['Valid separate texture and output buffers, valid shading tables and original non-faulting division inputs are required.', 'The remaining mode-six span routine and primitive packet/scene integration still require recovery.'])
(o.ROOT/'decompilation/reports/shader_spans_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
