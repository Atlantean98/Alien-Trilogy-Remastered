"""Compare complete palette/shading/blending buffers with original instructions."""
import ctypes as C
import json
import random
import check_original_behavior as o

U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000
native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
    data=(o.ROOT/'decompilation/image'/obj['file']).read_bytes()
    C.memmove(base+obj['base'],data,len(data))
o.dll.software_render_probe.argtypes=[C.POINTER(U8),U32,U32,U32,U32,C.POINTER(U32),C.POINTER(U32)]
o.dll.software_render_probe.restype=U32
rng=random.Random(0x486d4)
start=0x3d8800;length=0x21000
log=(U32*16)();count=U32();counts={}
for op,entry,name in [(9,0x4877c,'palette_components_and_shading'),(10,0x486e4,'indexed_blend_table'),(11,0x486d4,'combined_palette_rebuild')]:
    for case in range(36):
        # Include arbitrary byte inputs to preserve truncation and lookup behavior,
        # as well as the game's usual six-bit palette channel range.
        data=rng.randbytes(length)
        o.mu.mem_write(start,data)
        if case<4:
            value=[0,1,31,63][case]
            o.mu.mem_write(0x3f88a0,bytes([value])*768)
            for a in [0x3f29f0,0x3f2af0,0x3f2bf0]:o.mu.mem_write(a,bytes([value])*256)
        elif case<20:
            o.mu.mem_write(0x3f88a0,bytes(rng.randrange(64) for _ in range(768)))
            for a in [0x3f29f0,0x3f2af0,0x3f2bf0]:o.mu.mem_write(a,bytes(rng.randrange(128) for _ in range(256)))
        C.memmove(base+start,bytes(o.mu.mem_read(start,length)),length)
        o.run(entry,instruction_limit=15000000)
        o.dll.software_render_probe(native,SIZE,op,0,0,log,C.byref(count))
        actual=C.string_at(base+start,length);expected=bytes(o.mu.mem_read(start,length))
        assert actual==expected,(name,case,[(hex(start+i),a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b][:12])
        assert count.value==0
    counts[name]=36
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=sum(counts.values()),counts=counts,
    limitations=['Entire output and surrounding mapped buffers compared; palette loading, textured polygons and display presentation remain outside this test.'])
(o.ROOT/'decompilation/reports/palette_rendering_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
