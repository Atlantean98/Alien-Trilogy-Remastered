"""Compare isolated movement helpers and player callback orchestration to x86.

Collision, input, camera and weapon services are controlled stubs in the callback
test. Those services themselves are NOT verified by the orchestration comparison.
"""
import ctypes as C
import json
import random
import struct
import check_original_behavior as oracle
from unicorn import UC_HOOK_CODE

mu,dll,run,ROOT=oracle.mu,oracle.dll,oracle.run,oracle.ROOT
U16,U32,U8=C.c_uint16,C.c_uint32,C.c_uint8
dll.movement_probe.argtypes=[C.POINTER(U16),C.c_uint,C.c_uint]
dll.player_tick_probe.argtypes=[C.POINTER(U32),C.POINTER(U8),U32,C.POINTER(U32)]
dll.player_tick_probe.restype=C.c_uint
addresses=[0xa0a64,0xa0b46,0xa0b48,0xa0abc,0xa0a50,0xa0b38,0xa0b42]
rng=random.Random(0xC0111DE)
counts={}
boundaries=[0,1,7,8,9,0x78,0x90,0x7ff7,0x7fff,0x8000,0x8008,0xffff]
for operation,routine in enumerate([0x3cbf8,0x3cc80,0x3ce50]):
    cases=[]
    if operation<2:
        for terrain in [0,5,9,0xff]:
            for gate in [0,1,0x8000]:
                for speed in boundaries:
                    for maximum,accel in [(120,9),(144,12),(0x8000,0xffff),(0x7fff,0x7fff)]:
                        cases.append(([speed,maximum,accel,gate,48,16,48],terrain))
    else:
        for pitch in boundaries:
            for rest in boundaries: cases.append(([0,120,9,0,pitch,99,rest],0))
    cases += [([rng.getrandbits(16) for _ in range(7)],rng.randrange(256)) for _ in range(2000)]
    for words,terrain in cases:
        mu.mem_write(0xa0ab0,struct.pack('<I',0x320000))
        mu.mem_write(0x32000a,bytes([terrain]))
        for address,value in zip(addresses,words): mu.mem_write(address,struct.pack('<H',value))
        run(routine)
        expected=[struct.unpack('<H',mu.mem_read(a,2))[0] for a in addresses]
        native=(U16*7)(*words)
        dll.movement_probe(native,terrain,operation)
        assert list(native)==expected,(hex(routine),words,terrain,list(native),expected)
    counts[['forward_acceleration','other_direction_acceleration','pitch_recentering'][operation]]=len(cases)

service_addresses=[0x3e894,0x3d930,0x3c94c,0x3dfc0,0x3e228,0x3e714]
calls=[]
def service_hook(uc,address,size,_): calls.append(address)
for address in service_addresses:
    mu.hook_add(UC_HOOK_CODE,service_hook,begin=address,end=address)
    mu.mem_write(address,b'\xc3')
    mu.ctl_remove_cache(address,address+32)
# Death service advances the task cursor. Other services are no-ops except mover.
death=b'\x8b\x42\x18\xa3'+struct.pack('<I',0x22cec0)+b'\xc3'
mu.mem_write(0x3e714,death)
mu.ctl_remove_cache(0x3e714,0x3e734)
tick_cases=[]
for flags in [0,0x20,0xff]:
    for ticks in [0,1,2,255]:
        for old,new in [(0,0),(1,0),(0,1),(0x7fff,0x8000),(0x8000,0x7fff),(0xffff,0),(0,0xffff)]:
            tick_cases.append((flags,ticks,(old<<16)|0xabcd,(new<<16)|0x1234))
tick_cases += [(rng.randrange(256),rng.randrange(256),rng.getrandbits(32),rng.getrandbits(32)) for _ in range(1000)]
for flags,ticks,vertical,moved in tick_cases:
    state=[flags,vertical,rng.getrandbits(32),ticks,rng.getrandbits(16),rng.getrandbits(16),rng.getrandbits(16)]
    task=bytearray(rng.randbytes(32))
    struct.pack_into('<I',task,24,0x300020)
    mu.mem_write(0x300000,bytes(task))
    mu.mem_write(0xa0c90,bytes([flags]));mu.mem_write(0xa0a48,struct.pack('<I',vertical))
    mu.mem_write(0x22cec0,struct.pack('<I',state[2]));mu.mem_write(0x95f9c,bytes([ticks]))
    mu.mem_write(0xa0a60,struct.pack('<3H',*state[4:]))
    mover=b'\xc7\x05'+struct.pack('<II',0xa0a48,moved)+b'\xc3'
    mu.mem_write(0x3d930,mover);mu.ctl_remove_cache(0x3d930,0x3d950)
    calls.clear()
    run(0x3f908,0x300000)
    expected=[mu.mem_read(0xa0c90,1)[0],struct.unpack('<I',mu.mem_read(0xa0a48,4))[0],
              struct.unpack('<I',mu.mem_read(0x22cec0,4))[0],mu.mem_read(0x95f9c,1)[0]]
    expected+=list(struct.unpack('<3H',mu.mem_read(0xa0a60,6)))
    expected_task=bytes(mu.mem_read(0x300000,32))
    native=(U32*7)(*state);native_task=(U8*32).from_buffer_copy(task);log=(U32*8)()
    length=dll.player_tick_probe(native,native_task,moved,log)
    assert list(native)==expected,('state',state,moved,list(native),expected)
    assert bytes(native_task)==expected_task,('task',state,moved,bytes(native_task).hex(),expected_task.hex())
    assert list(log)[:length]==calls,('services',state,list(log)[:length],calls)
counts['player_callback_orchestration']=len(tick_cases)
report=dict(status='passed',reference_sha256=oracle.manifest['sha256'],counts=counts,
    player_comparisons=sum(counts.values()),
    limitations=['Device input, collision, camera, death and weapon services are controlled stubs in the callback comparison.',
                 'The three leaf movement helpers execute original unstubbed instructions.',
                 'Grounded callback naming describes the observed state machine; coordinate-axis interpretation remains under review.',
                 'This does not establish a complete movement or collision implementation.'])
(ROOT/'decompilation/reports/player_checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
