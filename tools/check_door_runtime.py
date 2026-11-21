"""Original door prerequisites and battery-panel activation versus native C++."""
import ctypes as C
import json,random
import check_original_behavior as o
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_EBX
U8,U32=C.c_uint8,C.c_uint32
SIZE=0x400000;native=(U8*SIZE)();base=C.addressof(native)
for obj in o.manifest['objects']:
 d=(o.ROOT/'decompilation/image'/obj['file']).read_bytes();C.memmove(base+obj['base'],d,len(d))
o.dll.door_runtime_probe.argtypes=[C.POINTER(U8)]+[U32]*4+[C.POINTER(U32),C.POINTER(U32)];o.dll.door_runtime_probe.restype=U32
rng=random.Random(0x3692c);effects=[];cases=0
windows=[(0x94000,0xd000),(0x234800,0x100),(0x236700,0x200),(0x236c00,0x100),(0x320000,0x2000),(0x330000,0x1000),(0x3f9400,0x500)]
def put(a,n,s=4):o.mu.mem_write(a,(n&((1<<(s*8))-1)).to_bytes(s,'little'))
def norm(r,a,b,c):return [r,0 if r==0x51e68 else a,b if r==0x40460 else 0,c if r==0x40460 else 0]
def hook(uc,r,size,_):effects.extend(norm(r,uc.reg_read(UC_X86_REG_EAX),uc.reg_read(UC_X86_REG_EDX),uc.reg_read(UC_X86_REG_EBX)))
for r in [0x3ab6c,0x40460,0x560a4,0x51e68]:o.mu.hook_add(UC_HOOK_CODE,hook,begin=r,end=r);o.mu.mem_write(r,b'\xc3');o.mu.ctl_remove_cache(r,r+32)
for i in range(1200):
 for a,n in windows:o.mu.mem_write(a,rng.randbytes(n))
 put(0x2348d8,0x330000);put(0x2367fc,0x320000);put(0x9e0a8,rng.randrange(2),1);put(0xa0ad2,rng.choice([0,1,2,65535]),2)
 index=rng.randrange(64);delta=rng.randrange(256);p=0x330000+index*32
 put(p+24,rng.choice([0,0,0,8,16,128,64]),1);put(p+26,rng.choice([0,1,2,3,4,255]),1);put(p+27,rng.randrange(5),1)
 obj=0x320000+index*40;put(obj+24,rng.choice([20,22,24,26,27]),1);put(obj+26,rng.choice([0,0,0,1,2]),2);put(0x3f953b,rng.randrange(2),1);put(0x236c4e,rng.randrange(2),1)
 for op in range(3):
  for a,n in windows:C.memmove(base+a,bytes(o.mu.mem_read(a,n)),n)
  effects.clear();a,b=(delta,index) if op<2 else (index,delta);want=o.run([0x28e84,0x28f00,0x3692c][op],a,b)
  log=(U32*128)();count=U32();have=o.dll.door_runtime_probe(native,SIZE,op,a,b,log,C.byref(count));assert want==have,('return',i,op,want,have)
  calls=[]
  for k in range(0,count.value,4):calls.extend(norm(*list(log)[k:k+4]))
  assert calls==effects,('calls',i,op,calls,effects)
  for address,n in windows:
   have=C.string_at(base+address,n);want=bytes(o.mu.mem_read(address,n));assert have==want,('memory',i,op,[(hex(address+k),x,y) for k,(x,y) in enumerate(zip(have,want)) if x!=y][:10])
  cases+=1
report=dict(status='passed',reference_sha256=o.manifest['sha256'],new_comparisons=cases,recovered_original_entries=['0x28e84','0x28f00','0x3692c'],counts={'door_unlock_final_increment_and_battery_panel_state':cases},limitations=['Message, sound and network synchronization calls are observed providers. This verifies prerequisite/activation routines, not the whole door scheduler.'])
(o.ROOT/'decompilation/reports/door_runtime_checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
