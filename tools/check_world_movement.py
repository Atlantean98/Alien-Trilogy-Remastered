"""Original-instruction comparisons for vectors, cell geometry and player movement."""
import ctypes as C
import json
import random
import struct
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EAX,UC_X86_REG_EDX

mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
S16,S32,U32,U8=C.c_int16,C.c_int32,C.c_uint32,C.c_uint8
ps=C.POINTER(S16);pu=C.POINTER(U8);pw=C.POINTER(U32)
dll.matrix_probe.argtypes=[ps,ps,ps]
dll.transform_probe.argtypes=[ps,ps,C.POINTER(S32)]
dll.rotate_probe.argtypes=[ps,ps,ps,ps,ps,C.c_uint]
dll.world_query_probe.argtypes=[pu,C.c_uint,C.c_uint,U32,U32,ps,C.c_uint,C.c_uint,U32]
dll.world_query_probe.restype=U32
dll.world_move_probe.argtypes=[pw,pu,C.c_uint,C.c_uint,ps,pw];dll.world_move_probe.restype=C.c_uint
trig=struct.unpack('<4096h',mu.mem_read(0x9763c,8192));native_trig=(S16*4096)(*trig)
rng=random.Random(0xC0111510)
A,V,M,OUT,GRID=0x720000,0x720100,0x720200,0x720300,0x330000
mu.mem_map(A,0x1000)
counts={}
angles=[(0,a,0) for a in range(0,4096,64)]+[tuple(rng.randrange(-32768,32768) for _ in range(3)) for _ in range(1000)]
for a in angles:
    mu.mem_write(A,struct.pack('<3h',*a));run(0x266bc,A,M)
    expected=struct.unpack('<9h',mu.mem_read(M,18));native=(S16*9)()
    dll.matrix_probe((S16*3)(*a),native_trig,native)
    assert tuple(native)==expected,('matrix',a,tuple(native),expected)
counts['rotation_matrices']=len(angles)
for i in range(1200):
    matrix=[rng.randrange(-32768,32768) for _ in range(9)]
    vector=[rng.randrange(-32768,32768) for _ in range(3)]
    mu.mem_write(M,struct.pack('<9h',*matrix));mu.mem_write(V,struct.pack('<3h',*vector));mu.reg_write(UC_X86_REG_EBX,OUT)
    run(0x47578,M,V);expected=struct.unpack('<3i',mu.mem_read(OUT,12));native=(S32*3)()
    dll.transform_probe((S16*9)(*matrix),(S16*3)(*vector),native)
    assert tuple(native)==expected,('transform',matrix,vector,tuple(native),expected)
counts['vector_transforms']=1200
for add in [0,1]:
    for _ in range(800):
        a=[rng.randrange(-32768,32768) for _ in range(3)];v=[rng.randrange(-32768,32768) for _ in range(3)];b=[rng.randrange(-32768,32768) for _ in range(3)]
        mu.mem_write(A,struct.pack('<3h',*a));mu.mem_write(V,struct.pack('<3h',*v));mu.mem_write(OUT,struct.pack('<3h',*b))
        mu.reg_write(UC_X86_REG_EBX,OUT);mu.reg_write(UC_X86_REG_ECX,OUT)
        run(0x26400 if add else 0x263c0,A,V)
        expected=struct.unpack('<3h',mu.mem_read(OUT,6));native=(S16*3)()
        dll.rotate_probe((S16*3)(*a),(S16*3)(*v),(S16*3)(*b),native_trig,native,add)
        assert tuple(native)==expected,('rotate',add,a,v,b,tuple(native),expected)
counts['rotate_and_add_wrappers']=1600

W=H=12
def setup_grid(data):
    mu.mem_write(GRID,bytes(data));mu.mem_write(0xa0c30,struct.pack('<HH',W,H));mu.mem_write(0xa0c40,struct.pack('<I',GRID))
def grid(randomized=False):
    data=bytearray(W*H*16)
    for i in range(W*H):
        p=i*16;data[p+13]=80
        if randomized:
            data[p+6]=rng.randrange(4);data[p+7]=rng.choice([0,0,0,128]);data[p+10]=rng.randrange(0x39)
            data[p+11]=rng.randrange(20);data[p+12]=rng.choice([0,0,0,255]);data[p+13]=rng.randrange(20,100)
    return data
query_addresses=[0x277c8,0x27950,0x279ac,0x27a7c,0x3d144,0x3d52c,0x3d19c,0x3d584]
query_count=0
def query(data,x,z,d,flags,op,tested=0):
    global query_count
    setup_grid(data);mu.mem_write(V,struct.pack('<3h',*d));mu.mem_write(0x94b9c,bytes([flags]))
    mu.reg_write(UC_X86_REG_EBX,tested if op in [4,5] else V)
    mu.reg_write(UC_X86_REG_ECX,flags if op in [4,5] else 0)
    result=run(query_addresses[op],x,z)
    expected=struct.unpack('<3h',mu.mem_read(V,6))
    native_d=(S16*3)(*d);native_cells=(U8*len(data)).from_buffer_copy(data)
    actual=dll.world_query_probe(native_cells,W,H,x,z,native_d,flags,op,tested)
    assert actual==result and tuple(native_d)==expected,('query',op,x,z,d,flags,tested,hex(actual),hex(result),tuple(native_d),expected)
    query_count+=1

# Exhaust slope/step attributes at seam/half-cell boundaries and flat height bytes.
for attr in list(range(0x2d,0x39))+[0,5,9]:
    for lx in [0,1,255,256,257,510,511]:
        for lz in [0,1,255,256,257,510,511]:
            data=grid();index=(4*W+4)*16;data[index+10]=attr;data[index+11]=rng.randrange(256)
            query(data,2048+lx,2048+lz,[0,0,0],0,0)
for x,z in [(0xffffffff,0),(0,0xffffffff),(W*512,0),(0,H*512)]:
    for op in [0,1]:query(grid(),x,z,[0,0,0],0,op)
for op in [1,2,3,4,5,6,7]:
    for _ in range(900):
        data=grid(True);x=rng.randrange(1500,4000);z=rng.randrange(1500,4000)
        d=[rng.randrange(-150,151),rng.randrange(0,1000),rng.randrange(-150,151)]
        flags=rng.choice([0,1]) if op in [4,5] else rng.choice([0,1,8,9])
        query(data,x,z,d,flags,op,(z if op==5 else x)+rng.randrange(-200,201))
counts['floor_ceiling_diagonal_axis_queries']=query_count

services=[0x27b40,0x27b6c,0x3dde8,0x3dd24,0x408b0];service_log=[]
def record(uc,address,size,_):
    a=uc.reg_read(UC_X86_REG_EAX);b=uc.reg_read(UC_X86_REG_EDX)
    if address==0x27b40:a=b=0
    elif address==0x27b6c:a=b;b=0
    elif address==0x408b0:b=0
    service_log.extend([address,a,b])
for address in services:
    mu.hook_add(UC_HOOK_CODE,record,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
fields=[(0xa0a48,2),(0xa0a4a,2),(0xa0a4c,2),(0xa0a58,2),(0xa0a5a,2),(0xa0a5c,2),
        (0xa0a60,2),(0xa0a62,2),(0xa0a64,2),(0xa0c34,2),(0xa0c36,2),(0xa0ab0,4),
        (0xa0ab4,1),(0x94b9c,1),(0xa0ab5,1),(0xa0ade,2),(0xa0ae0,2),(0xa0abc,2),
        (0xa0ae4,2),(0xa0b30,2),(0xa0ab8,2),(0xa0b44,2),(0xa0c94,4),(0x94b7a,2)]
def move_case(data,state):
    setup_grid(data)
    for i,(address,size) in enumerate(fields):
        value=GRID+state[i]*16 if i==11 else state[i]
        mu.mem_write(address,value.to_bytes(size,'little'))
    service_log.clear();run(0x3d930)
    expected=[int.from_bytes(mu.mem_read(a,n),'little') for a,n in fields];expected[11]=(expected[11]-GRID)//16
    expected_grid=bytes(mu.mem_read(GRID,len(data)))
    native=(U32*24)(*state);native_cells=(U8*len(data)).from_buffer_copy(data);log=(U32*30)()
    count=dll.world_move_probe(native,native_cells,W,H,native_trig,log)
    assert list(native)==expected,('move state',state,next((i,a,b) for i,(a,b) in enumerate(zip(native,expected)) if a!=b))
    assert bytes(native_cells)==expected_grid,('move cells',state)
    assert list(log)[:count]==service_log,('move services',state,list(log)[:count],service_log)
    return expected,expected_grid

for i in range(1200):
    data=grid(True);x=rng.randrange(2000,4000);z=rng.randrange(2000,4000)
    ox,oz=rng.choice([0,3000,5000]),rng.choice([0,3000,5000]);cell=(z>>9)*W+(x>>9)
    local=[rng.randrange(-70,71),0,rng.randrange(0,145)]
    state=[(x-ox)&0xffff,rng.randrange(800),(z-oz)&0xffff,0,rng.randrange(4096),0,*local]
    state=[v&0xffff for v in state]
    state += [ox,oz,cell,rng.choice([0,2]),rng.choice([0,1,8,9]),rng.randrange(256),rng.randrange(0,129),rng.randrange(4096),
              rng.choice([0,0,1]),rng.choice([0,0,1]),rng.randrange(31),rng.randrange(101),rng.randrange(4),rng.getrandbits(32),rng.getrandbits(16)]
    move_case(data,state)
counts['whole_movement_updates']=1200
# Exact floor-height contact ensures every hazard branch is exercised, including
# protected players and negative signed health passed to the damage service.
targeted=0
for terrain in [0,7,8,10,12,0x35,0x36,0x37,0x38]:
    for gate in [0,1]:
        for runFlag in [0,2]:
            for impulse in [0,1,8,128]:
                data=grid();cell=5*W+5
                data[cell*16+10]=terrain;data[cell*16+11]=4
                state=[2800,128,2800,0,1024,0,0,0,0,0,0,cell,runFlag,0,255,impulse,0,gate,gate,30,0xffff,0,0xffffffff,0xffff]
                move_case(data,state);targeted+=1
counts['targeted_hazards_conveyors_impulses']=targeted

# Connect the real mover to the real normal player callback. Only services
# outside this movement scope are stubs; both axis routines and floor sampling run.
dll.world_tick_probe.argtypes=[pw,pw,pu,pu,C.c_uint,C.c_uint,ps,pw];dll.world_tick_probe.restype=C.c_uint
other_addresses=[0x3e894,0x3c94c,0x3dfc0,0x3e228,0x3e714]
def other_hook(uc,address,size,_):service_log.extend([address,0,0])
for address in other_addresses:
    mu.hook_add(UC_HOOK_CODE,other_hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)
mu.mem_write(0x3e714,b'\x8b\x42\x18\xa3'+struct.pack('<I',0x22cec0)+b'\xc3')
mu.ctl_remove_cache(0x3e714,0x3e734)
for case in range(500):
    data=grid(True);x,z=2800,2800;cell=(z>>9)*W+(x>>9)
    state=[x,rng.randrange(800),z,0,rng.randrange(4096),0,0,0,rng.randrange(145),0,0,cell,0,0,0,0,0,0,0,0,100,0,0,0]
    metadata=[rng.choice([0,0,0,32]),rng.getrandbits(32),rng.choice([0,1,2,255])]
    task=bytearray(rng.randbytes(32));struct.pack_into('<I',task,24,0x300020)
    setup_grid(data)
    for i,(address,size) in enumerate(fields):
        value=GRID+state[i]*16 if i==11 else state[i]
        mu.mem_write(address,value.to_bytes(size,'little'))
    mu.mem_write(0x300000,bytes(task));mu.mem_write(0xa0c90,bytes([metadata[0]]))
    mu.mem_write(0x22cec0,struct.pack('<I',metadata[1]));mu.mem_write(0x95f9c,bytes([metadata[2]]))
    service_log.clear();run(0x3f908,0x300000)
    expected=[int.from_bytes(mu.mem_read(a,n),'little') for a,n in fields];expected[11]=(expected[11]-GRID)//16
    expected_meta=[mu.mem_read(0xa0c90,1)[0],int.from_bytes(mu.mem_read(0x22cec0,4),'little'),mu.mem_read(0x95f9c,1)[0]]
    expected_task=bytes(mu.mem_read(0x300000,32));expected_cells=bytes(mu.mem_read(GRID,len(data)))
    native=(U32*24)(*state);meta=(U32*3)(*metadata);ntask=(U8*32).from_buffer_copy(task)
    ncells=(U8*len(data)).from_buffer_copy(data);log=(U32*60)()
    length=dll.world_tick_probe(native,meta,ntask,ncells,W,H,native_trig,log)
    assert list(native)==expected,('integrated player',state,metadata,next((i,a,b) for i,(a,b) in enumerate(zip(native,expected)) if a!=b))
    assert list(meta)==expected_meta and bytes(ntask)==expected_task,('integrated task',metadata,list(meta),expected_meta)
    assert bytes(ncells)==expected_cells and list(log)[:length]==service_log,('integrated cells/services',list(log)[:length],service_log)
counts['integrated_player_movement_ticks']=500
report=dict(status='passed',reference_sha256=o.manifest['sha256'],counts=counts,new_comparisons=sum(counts.values()),
    limitations=['Grid access in collision requires in-bounds neighborhood coordinates, as does the original.',
                 'Visibility/region, damage and map interaction services are controlled no-op/logging stubs for whole-update tests.',
                 'Vector, floor, ceiling, diagonal and axis-collision comparisons execute the original dependent routines unstubbed.',
                 'No claim of a complete game or renderer.'])
(ROOT/'decompilation/reports/world_movement_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
