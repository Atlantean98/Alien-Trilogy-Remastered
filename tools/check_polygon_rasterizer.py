"""Polygon setup, sorting, span state and actual solid pixels versus x86."""
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
        if a>=0x90000 and a not in [0x3e89d8,0x100000,0x300000]:o.mu.mem_write(a,rng.randbytes(n))
    put(0x9d544,0x28c888);put(0x9d504,319);put(0x9d508,239);put(0x3f6d78,320)
    put(0x3f6e44,320);put(0x3f6e4c,240);put(0x3f6e83,5,1)
    for i in range(7):put(0x9d510+i*4,0x5b428 if real else 0x5a000)
def compare(op,a=0):
    global cases,faults
    for addr,n in windows:C.memmove(base+addr,bytes(o.mu.mem_read(addr,n)),n)
    effects.clear();o.mu.reg_write(UC_X86_REG_EBX,0);o.mu.reg_write(UC_X86_REG_ECX,0)
    try:o.run([0x57664,0x4a0e0,0x4acb8,0x5b428,0x4acb8,0x48e40,0x488cc,0x5c519,0x4abc0,0x5a186,0x4acb8,0x5a567][op],a,instruction_limit=4000000)
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
start=cases
for i in range(700):setup();triangle_fixture();compare(0)
counts['triangle_sort_all_vertex_channels']=cases-start
start=cases
for i in range(700):
    setup();x=rng.randrange(-100,350);y=rng.randrange(-100,250);w=rng.randrange(1,160);h=rng.randrange(1,140)
    for addr,value in [(0x3f6de0,x),(0x3f6de4,x+w),(0x3f6df0,y),(0x3f6d9c,y+h),(0x3f6dd0,3),(0x3f6dd4,120),(0x3f6d7c,10),(0x3f6d84,180)]:put(addr,value)
    compare(1,rng.getrandbits(32))
counts['textured_rectangle_span_setup']=cases-start
start=cases
for i in range(2000):setup();triangle_fixture();compare(2,rng.getrandbits(32))
counts['triangle_clipping_sorting_and_span_dispatch']=cases-start
start=cases
for i in range(900):
    setup(True);put(0x3f6d58,rng.choice([0,1,2,20,240,255,256,257,0xffffffff]));put(0x3f6cf0,0x28c888+rng.randrange(-1,240)*320)
    put(0x3f6db0,rng.randrange(-100,400)*65536);put(0x3f6d90,rng.randrange(-100,400)*65536)
    put(0x3f6dcc,rng.randrange(-3,4)*65536);put(0x3f6d44,rng.randrange(-3,4)*65536)
    compare(3)
counts['solid_span_pixels_counter_and_edge_state']=cases-start
start=cases
for i in range(1200):setup(True);triangle_fixture();compare(4,rng.getrandbits(32))
counts['triangle_setup_through_solid_pixels']=cases-start
start=cases
for i in range(500):
    setup(True);put(0x39000b,i%2,1)
    for j in range(4):put(0x39000c+j*4,rng.randrange(-100,420),2);put(0x39000e+j*4,rng.randrange(-100,300),2)
    compare(5,0x390000)
counts['quad_submission_through_solid_pixels']=cases-start
windows += [(0x5c587,4),(0x5c593,4),(0x5c599,4),(0x3e89d8,0x11000),(0x100000,0xf0000)]
o.mu.mem_write(0x100000,rng.randbytes(0xf0000));o.mu.mem_write(0x3e89d8,rng.randbytes(0x8000));o.mu.mem_write(0x3f88a0,bytes(rng.randrange(64) for _ in range(768)));o.run(0x4877c)
start=cases
for i in range(400):
    setup();put(0x3f6d28,0x100000+rng.randrange(65536));put(0x3f6cf0,0x28c888+rng.randrange(120)*320)
    put(0x3f6d70,rng.choice([0,1,2,255,256,257,513]));put(0x3f6d2c,rng.randrange(1,40))
    for addr in [0x3f6da4,0x3f6dbc,0x3f6d8c]:put(addr,rng.randrange(64)*128)
    compare(7)
counts['transparent_shaded_sprite_pixels_and_uv_byte_wrap']=cases-start
start=cases
for i in range(300):
    setup();put(0x9d540,0x100000)
    x,y=rng.randrange(60),rng.randrange(100);w,h=rng.randrange(1,255),rng.randrange(1,40)
    for offset,value in zip([12,14,20,30],[x,y,x+w,y+h]):put(0x390000+offset,value,2)
    put(0x39001a,rng.choice([0,2,3,7,10,11,13,16,17,20,255]),2)
    compare(8,0x390000)
counts['sprite_packet_to_palette_shaded_pixels']=cases-start
windows += [(0x3fa900,0x100)]
o.mu.hook_add(UC_HOOK_CODE,snapshot,begin=0x577c8,end=0x577c8);o.mu.mem_write(0x577c8,b'\xc3');o.mu.ctl_remove_cache(0x577c8,0x577e8)
start=cases
for i in range(100):
    setup(True);put(0x9d540,0x100000);put(0x9d50c,0);put(0x3f6e85,0,1);put(0x3f6e86,0,1)
    for j,callback in enumerate([0x48e40,0x4abc0,0x4bc88]):
        p=0x390000+j*64;put(p,p+64 if j<2 else 0);put(p+4,callback);put(p+11,0,1)
        for offset,value in zip([12,14,16,18,20,22,24,26,28,30],[10,10,60,10,10,60,60,60,20,30]):put(p+offset,value,2)
        if j==1:put(p+20,30,2);put(p+26,2,2)
        if j==2:put(p+16,20,2);put(p+18,30,2)
    compare(6,0x390000)
counts['complete_frame_solid_quad_shaded_sprite_and_rectangle']=cases-start
windows += [(0x5a0ec,12),(0x5a33f,0x9d),(0x300000,256)]
o.mu.mem_write(0x300000,rng.randbytes(256))
start=cases
for i in range(650):
    setup();put(0x3f6d58,rng.randrange(1,50));put(0x3f6cf0,0x28c888+rng.randrange(80)*320)
    put(0x3f6db0,rng.randrange(-100,220)*65536+rng.randrange(65536));put(0x3f6d90,rng.randrange(-20,320)*65536+rng.randrange(65536))
    for addr in [0x3f6dcc,0x3f6d44,0x3f6d4c,0x3f6d48,0x3f6dc8,0x3f6dc4]:put(addr,rng.randrange(-131072,131073))
    for addr in [0x3f6da8,0x3f6dac,0x3f6d68,0x3f6d74]:put(addr,rng.getrandbits(32))
    put(0x3f6d28,0x100000);put(0x3f6d3c,0x300000)
    compare(9)
counts['indexed_texture_span_pixels_uv_rounding_clipping_and_edge_correction']=cases-start
start=cases
for i in range(700):
    setup();triangle_fixture();put(0x9d510,0x5a186);put(0x3f6e83,0,1);put(0x3f6d3c,0x300000)
    compare(10,0x100000)
counts['triangle_to_indexed_texture_pixels']=cases-start
windows += [(0x5a4cc,20),(0x5a73a,0xb2)]
start=cases
for i in range(650):
    setup();put(0x3f6d58,rng.randrange(1,50));put(0x3f6cf0,0x28c888+rng.randrange(80)*320)
    put(0x3f6db0,rng.randrange(-100,220)*65536+rng.randrange(65536));put(0x3f6d90,rng.randrange(-20,320)*65536+rng.randrange(65536))
    for addr in [0x3f6dcc,0x3f6d44,0x3f6d4c,0x3f6d48,0x3f6dc8,0x3f6dc4]:put(addr,rng.randrange(-131072,131073))
    for addr in [0x3f6da8,0x3f6dac,0x3f6d68,0x3f6d74]:put(addr,rng.getrandbits(32))
    put(0x3f6d28,0x100000);put(0x3f6d3c,0x300000);compare(11)
counts['transparent_texture_span_pixels_long_rows_and_edge_correction']=cases-start
start=cases
for i in range(700):
    setup();triangle_fixture();put(0x9d514,0x5a567);put(0x3f6e83,1,1);put(0x3f6d3c,0x300000);compare(10,0x100000)
counts['triangle_to_transparent_texture_pixels']=cases-start
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,counts=counts,original_division_fault_fixtures_excluded=faults,
    recovered_original_entries=['0x57664','0x4a0e0','0x4acb8','0x5b428','0x5c519','0x4abc0','0x5a186','0x5a567'],limitations=['Other textured and shaded polygon scanline variants remain providers; opaque/transparent indexed texture spans and palette-shaded sprite pixels are native.', 'Original divide-fault fixtures are excluded, not counted as passing native comparisons.', 'Solid fill preserves byte-sized counts, inclusive row stepping and original clipping quirks; sprite heights must be positive and buffers valid. Texture and output buffers are separate. This is not whole-game visual equivalence.'])
(o.ROOT/'decompilation/reports/polygon_rasterizer_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
