"""Drawing-command batch versus original x86, including rectangle pixels."""
import ctypes as C
import json
import random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBX,UC_X86_REG_ECX
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],data,len(data))
o.dll.render_commands_probe.argtypes=[C.POINTER(U8)]+[U32]*7+[C.POINTER(U32),C.POINTER(U32)]
o.dll.render_commands_probe.restype=U32
windows=[(0x9d400,0x300),(0x23a400,0x100),(0x28c800,0x26000),(0x310800,0x100),(0x390000,0x1000),(0x3f6d00,0x200)]
rng=random.Random(0x4bc88);effects=[];cases=0;counts={};PACKET=0x390000
entries=[0x4c510,0x4c554,0x4c58c,0x4c5d4,0x4c5e4,0x4c5fc,0x4cf28,0x4cf4c,0x4cf64,0x48a3c,0x4bc38,0x4c504,0x4bc6c,0x4bc88,0x48e40,0x48a28]
entries += [0x4d578,0x4de74,0x4dee0,0x4df0c,0x4dfc0,0x4cfe8,0x4d834]
def put(a,n,size=4):o.mu.mem_write(a,(n&((1<<(8*size))-1)).to_bytes(size,'little'))
def get(a,size=4):return int.from_bytes(o.mu.mem_read(a,size),'little')
def triangle(uc,address,size,_):
    effects.append(address)
    effects.extend(get(a) for a in [0x3f6de0,0x3f6de4,0x3f6de8,0x3f6df0,0x3f6df4,0x3f6d9c,0x9d544,0x9d504,0x9d508])
    if get(0x310820,1):put(PACKET+16,get(PACKET+16,2)+7,2)
o.mu.hook_add(UC_HOOK_CODE,triangle,begin=0x4acb8,end=0x4acb8)
o.mu.mem_write(0x4acb8,b'\xc3');o.mu.ctl_remove_cache(0x4acb8,0x4acd8)
def setup():
    for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
    put(0x9d544,0x28c888+80);put(0x9d504,319);put(0x9d508,239);put(0x3f6e44,320);put(0x3f6e4c,240);put(0x310820,0,1)
def compare(op,a=PACKET,b=0,c=0,d=0,e=0):
    global cases
    for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    put(o.STACK+4,e)
    o.mu.reg_write(UC_X86_REG_EBX,c);o.mu.reg_write(UC_X86_REG_ECX,d);effects.clear()
    expected_return=o.run(entries[op],a,b,instruction_limit=3000000)
    log=(U32*256)();count=U32();actual_return=o.dll.render_commands_probe(native,SIZE,op,a,b,c,d,e,log,C.byref(count))
    if op in [16,17,18,19]:assert actual_return==expected_return,('return',op,hex(actual_return),hex(expected_return))
    assert list(log)[:count.value]==effects,('effects',op,list(log)[:count.value],effects)
    for addr,n in windows:
        actual=C.string_at(base+addr,n);expected=bytes(o.mu.mem_read(addr,n))
        assert actual==expected,('memory',op,hex(addr),[(hex(addr+i),x,y) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y][:12])
    cases+=1
for op in range(13):
    for i in range(100 if op==12 else 240):
        setup();a=PACKET;b,c,d,e=[rng.getrandbits(32) for _ in range(4)]
        if op==1:b=PACKET+rng.choice([0,4,8,12,64])
        if op==6:
            a,b,c,d=[rng.choice([PACKET,PACKET+4,0x23a4a8,0x23a4b8,0x23a4c8,0x23a4d8]) for _ in range(4)]
        if op==7:c=PACKET
        compare(op,a,b,c,d,e)
counts['packet_construction_state_and_aliasing']=cases
start=cases
for i in range(650):
    setup()
    x,y=rng.randrange(-1000,600),rng.randrange(-1000,500)
    w,h=rng.randrange(0,1000),rng.randrange(0,1000)
    if i<8:x,y,w,h=[(-10,-10,20,20),(0,0,320,240),(319,239,1,1),(320,240,10,10),(-32768,0,32767,1),(0,0,0,0),(0,0,1,0),(0,0,0,1)][i]
    for offset,value in zip([12,14,16,18],[x,y,w,h]):put(PACKET+offset,value,2)
    compare(13)
counts['clipped_solid_rectangle_pixels']=cases-start
start=cases
for i in range(1100):
    setup();put(PACKET+11,i%2,1);put(0x310820,(i//2)%2,1)
    width,height=rng.choice([(320,240),(280,210),(160,120),(80,60)])
    if i>=900:width,height=rng.getrandbits(32),rng.getrandbits(32)
    put(0x9d504,width-1);put(0x9d508,height-1);put(0x3f6e44,width);put(0x3f6e4c,height)
    for j in range(4):
        put(PACKET+12+j*4,rng.getrandbits(16) if i>=900 else rng.randrange(-500,600),2);put(PACKET+14+j*4,rng.getrandbits(16) if i>=900 else rng.randrange(-400,500),2)
    compare(14)
counts['quad_scaling_culling_base_buffer_and_triangle_dispatch']=cases-start
start=cases
for callback in [0x48a3c,0x4bc38,0x4bc6c,0x4bc88,0x48e40]:
    for i in range(40):
        setup();put(PACKET+4,callback);put(PACKET+11,i%2,1)
        for offset,value in zip([12,14,16,18,20,22,24,26],[10,20,30,40,50,60,70,80]):put(PACKET+offset,value,2)
        compare(15)
counts['packet_callback_integration']=cases-start
start=cases
for op in range(16,22):
    for i in range(600):
        setup();a,b,c,d,e=[rng.getrandbits(32) for _ in range(5)]
        if op==16:a=i if i<100 else a
        if op in [20,21]:
            a=PACKET+64;b=PACKET+128
            output=rng.choice([PACKET+64,PACKET+68,PACKET+128,PACKET+132,PACKET+256])
            if op==20:d=output
            else:e=output
        compare(op,a,b,c,d,e)
counts['texture_page_area_depth_lighting_and_long_vector_blending']=cases-start
start=cases
for i in range(240):
    setup();x=rng.randrange(200);y=rng.randrange(180);w=rng.randrange(100);h=rng.randrange(60)
    for offset,value in zip([12,14,20,30],[x,y,x+w,y+h]):put(PACKET+offset,value,2)
    compare(22)
counts['gradient_rectangle_pixels']=cases-start
recovered_entries=[hex(n) for n in entries[:15]+entries[16:]]
assert len(recovered_entries)==len(set(recovered_entries))==22
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,recovered_original_entries=recovered_entries,
    limitations=['Flat triangle rasterization remains a provider; its input globals and dispatch order are compared.', 'Visible solid rectangles use nonnegative extents; malformed negative extents that can loop outside the original buffer are excluded.', 'Pointers and buffers are valid mapped fixtures; this is not full-game visual equivalence.'])
(o.ROOT/'decompilation/reports/render_commands_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
