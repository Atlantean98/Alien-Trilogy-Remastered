"""Camera and alternate player states against original instructions.

Real vector/collision/floor/view routines run together. Input/weapon intent,
audio, UI, region, damage-service and map effects are logged boundary stubs.
"""
import ctypes as C
import json
import random
import struct
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX

mu,dll,run,ROOT=o.mu,o.dll,o.run,o.ROOT
U32,U16,U8,S16=C.c_uint32,C.c_uint16,C.c_uint8,C.c_int16
dll.player_states_probe.argtypes=[C.POINTER(U32),C.POINTER(U16),C.POINTER(U32),C.POINTER(U8),C.POINTER(U8),
    C.c_uint,C.c_uint,C.POINTER(S16),C.c_uint,C.POINTER(U32)]
dll.player_states_probe.restype=C.c_uint
trig=(S16*4096)(*struct.unpack('<4096h',mu.mem_read(0x9763c,8192)))
W=H=12;GRID=0x330000;rng=random.Random(0xC4A3E4)
world_fields=[(0xa0a48,2),(0xa0a4a,2),(0xa0a4c,2),(0xa0a58,2),(0xa0a5a,2),(0xa0a5c,2),
    (0xa0a60,2),(0xa0a62,2),(0xa0a64,2),(0xa0c34,2),(0xa0c36,2),(0xa0ab0,4),
    (0xa0ab4,1),(0x94b9c,1),(0xa0ab5,1),(0xa0ade,2),(0xa0ae0,2),(0xa0abc,2),
    (0xa0ae4,2),(0xa0b30,2),(0xa0ab8,2),(0xa0b44,2),(0xa0c94,4),(0x94b7a,2)]
view_addresses=[0xa0a50,0xa0a52,0xa0a54,0xa0b54,0xa0b4c,0x9ce5c,0xa0b38,0xa0b42,0xa0b34,
    0xa0b4e,0xa0b50,0xa0a20,0xa0a22,0xa0a24,0xa0ba2,0xa0ba4,0xa0ba8,0xa0bac]
meta_fields=[(0xa0c90,4),(0x9e0a8,1),(0x9cddc,1),(0x9d7fe,2),(0x22cec0,4),(0x95f9c,1)]
services=[0x40460,0x51948,0x551ac,0x512d0,0x51808,0x55dfc,0x4e5a8,0x34acc,
          0x3e894,0x3dfc0,0x3e228,0x27b40,0x27b6c,0x3dde8,0x3dd24,0x408b0]
effect_log=[]
def record(uc,address,size,_):
    a=uc.reg_read(UC_X86_REG_EAX);b=uc.reg_read(UC_X86_REG_EDX);c=uc.reg_read(UC_X86_REG_EBX)
    if address in [0x40460,0x51948]:pass
    elif address in [0x512d0,0x4e5a8,0x408b0]:b=c=0
    elif address==0x27b6c:a=b;b=c=0
    elif address in [0x3dde8,0x3dd24]:c=0
    else:a=b=c=0
    effect_log.extend([address,a,b,c])
for address in services:
    mu.hook_add(UC_HOOK_CODE,record,begin=address,end=address)
    mu.mem_write(address,b'\xc3');mu.ctl_remove_cache(address,address+32)

def make_grid(terrain=0,base=4):
    data=bytearray(W*H*16)
    for i in range(W*H):data[i*16+10]=terrain;data[i*16+11]=base;data[i*16+13]=80
    return data
def initial():
    world=[2800,128,2800,0,0,0,0,0,0,0,0,5*W+5,0,0,0,0,0,0,0,0,100,0,0,0]
    view=[48,0,2048,0,768,1,0,48,0,0,0,0,0,0,0,0,0,0]
    meta=[0,0,0,0,0,1]
    task=bytearray(32);struct.pack_into('<I',task,4,0x3f908);struct.pack_into('<I',task,24,0x300020)
    return world,view,meta,task

def compare(world,view,meta,task,data,camera=False):
    mu.mem_write(GRID,bytes(data));mu.mem_write(0xa0c30,struct.pack('<HH',W,H));mu.mem_write(0xa0c40,struct.pack('<I',GRID))
    for i,(a,size) in enumerate(world_fields):mu.mem_write(a,(GRID+world[i]*16 if i==11 else world[i]).to_bytes(size,'little'))
    for a,n in zip(view_addresses,view):mu.mem_write(a,struct.pack('<H',n))
    for (a,size),n in zip(meta_fields,meta):mu.mem_write(a,n.to_bytes(size,'little'))
    mu.mem_write(0x300000,bytes(task));effect_log.clear()
    entry=0x3c94c if camera else struct.unpack_from('<I',task,4)[0]
    run(entry,0x300000)
    expected_world=[int.from_bytes(mu.mem_read(a,n),'little') for a,n in world_fields];expected_world[11]=(expected_world[11]-GRID)//16
    expected_view=[int.from_bytes(mu.mem_read(a,2),'little') for a in view_addresses]
    expected_meta=[int.from_bytes(mu.mem_read(a,n),'little') for a,n in meta_fields]
    expected_task=bytes(mu.mem_read(0x300000,32));expected_grid=bytes(mu.mem_read(GRID,len(data)))
    nw=(U32*24)(*world);nv=(U16*18)(*view);nm=(U32*6)(*meta);nt=(U8*32).from_buffer_copy(task)
    ng=(U8*len(data)).from_buffer_copy(data);log=(U32*128)()
    length=dll.player_states_probe(nw,nv,nm,nt,ng,W,H,trig,int(camera),log)
    for group,actual,expected in [('world',list(nw),expected_world),('view',list(nv),expected_view),('meta',list(nm),expected_meta)]:
        assert actual==expected,(hex(entry),group,world,view,meta,task.hex(),next((i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b))
    assert bytes(nt)==expected_task,('task',hex(entry),task.hex(),bytes(nt).hex(),expected_task.hex())
    assert bytes(ng)==expected_grid,('grid',hex(entry))
    assert list(log)[:length]==effect_log,('effects',hex(entry),world,view,meta,list(log)[:length],effect_log)
    return expected_world,expected_view,expected_meta,bytearray(expected_task),bytearray(expected_grid)

counts={}
camera_cases=0
for phase in [0,1,1023,1024,2046,2047,2048,4094,4095,0x7fff,0x8000,0xffff]:
    for speed in [0,1,2,3,120,144,0x7fff,0x8000,0xffff]:
        for bob in [0,1]:
            w,v,m,t=initial();w[8]=speed;v[3]=phase;v[5]=bob
            compare(w,v,m,t,make_grid(),True);camera_cases+=1
for _ in range(1600):
    w,v,m,t=initial();w[0]=rng.getrandbits(16);w[1]=rng.getrandbits(16);w[2]=rng.getrandbits(16)
    w[8]=rng.getrandbits(16);w[14]=rng.getrandbits(8);v=[rng.getrandbits(16) for _ in range(18)]
    compare(w,v,m,t,make_grid(rng.choice([0,8,9])),True);camera_cases+=1
counts['camera_view_and_footstep_cases']=camera_cases

state_cases=0
# Exact landing thresholds, invulnerability, protection, mode and contact audio.
for distance in [0,256,1024,1025,1536,1537,2048,2049,2560,2561,32767,32768,65535]:
    for flags in [0,4]:
        for protected in [0,1]:
            for mode in [0,1]:
                w,v,m,t=initial();w[13]=flags;w[18]=protected;m[1]=mode
                struct.pack_into('<I',t,4,0x3e3c4);struct.pack_into('<I',t,8,128);struct.pack_into('<I',t,12,(128+distance)&0xffff)
                compare(w,v,m,t,make_grid(8));state_cases+=1
# Eye-height, pitch wraparound and respawn control boundaries.
for eye in [0,383,384,385,395,396,397,768,0x7fff,0x8000,0xffff]:
    for mode in [0,1]:
        for latch in [0,1,255]:
            for respawn in [0,1]:
                w,v,m,t=initial();v[4]=eye;v[0]=rng.getrandbits(16);m[1]=mode;m[2]=latch;m[3]=respawn;m[0]=0x80020
                struct.pack_into('<I',t,4,0x3e714);compare(w,v,m,t,make_grid());state_cases+=1
for callback in [0x3f908,0x3e3c4,0x3e638,0x3e714]:
    for _ in range(700):
        w,v,m,t=initial();w[1]=rng.randrange(0,3000);w[4]=rng.randrange(4096);w[8]=rng.randrange(145)
        w[13]=rng.choice([0,1,4,8]);w[14]=rng.randrange(256);w[15]=rng.randrange(129);w[16]=rng.randrange(4096)
        w[17]=rng.choice([0,1]);w[18]=rng.choice([0,1]);w[20]=rng.choice([0,1,5,15,25,100,0xffff])
        v[0]=rng.getrandbits(16);v[1]=rng.getrandbits(16);v[3]=rng.getrandbits(16);v[4]=rng.randrange(0,1500);v[5]=rng.choice([0,1]);v[6]=rng.getrandbits(16)
        m[0]=rng.choice([0,0,0,0x20,0x80000]);m[1]=rng.choice([0,1]);m[2]=rng.choice([0,1]);m[3]=rng.choice([0,1]);m[5]=rng.choice([0,1,2,255])
        struct.pack_into('<I',t,4,callback);struct.pack_into('<I',t,8,rng.randrange(3000));struct.pack_into('<I',t,12,rng.randrange(4000))
        compare(w,v,m,t,make_grid(rng.choice([0,7,8,9,10,12,0x31,0x35])));state_cases+=1
counts['alternate_and_normal_state_cases']=state_cases

sequence_cases=0
for callback,y,target,original in [(0x3e3c4,3000,128,3000),(0x3e638,0,128,0),(0x3e714,128,128,128),(0x3f908,128,128,128)]:
    w,v,m,t=initial();w[1]=y;v[3]=1900;w[8]=0
    struct.pack_into('<I',t,4,callback);struct.pack_into('<I',t,8,target);struct.pack_into('<I',t,12,original)
    data=make_grid()
    for frame in range(90):
        w,v,m,t,data=compare(w,v,m,t,data);sequence_cases+=1
        if struct.unpack_from('<I',t,4)[0]==0x3fbc0:break
counts['consecutive_player_ticks']=sequence_cases
report=dict(status='passed',reference_sha256=o.manifest['sha256'],counts=counts,
    new_comparisons=sum(counts.values()),reconstructed_routines=['0x3c94c','0x3e3c4','0x3e638','0x3e714'],
    limitations=['Input/weapon intent and audiovisual/UI/region/damage/map services are controlled logging stubs.',
                 'Original movement, collision, floor, pitch recentering and view routines execute unstubbed.',
                 'Respawn initialization itself remains unrecovered.',
                 'Tests establish sampled state and effect-order agreement, not a full game playthrough.'])
(ROOT/'decompilation/reports/player_states_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
