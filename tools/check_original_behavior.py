"""Differential checks: original x86 instructions versus compiled recovered C++.

Requires workspace-local Unicorn 2.1.4 (pip --target tools/vendor/python).
Only device polling is stubbed. This does not run the complete DOS game.
"""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import sys
import shutil

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'vendor' / 'python'))
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EFLAGS
from unicorn.x86_const import UC_X86_REG_GDTR, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS

U32 = C.c_uint32
U16 = C.c_uint16
U8 = C.c_uint8
compiler = shutil.which('clang++')
runtime_directory = os.add_dll_directory(str(Path(compiler).parent)) if compiler else None
dll = C.CDLL(str(ROOT / 'decompilation/cpp/build/libbehavior_probe.dll'))
dll.input_probe.argtypes = [C.POINTER(U32), C.POINTER(U16), U32, C.c_int]
dll.be32_probe.argtypes = dll.decimal_probe.argtypes = [C.POINTER(U8)]
dll.be32_probe.restype = dll.decimal_probe.restype = U32
dll.runtime_probe.argtypes = [C.POINTER(U32), C.c_uint, C.c_uint, C.c_uint, C.c_int, C.POINTER(U32)]
dll.runtime_probe.restype = C.c_uint

manifest = json.loads((ROOT / 'decompilation/image/manifest.json').read_text())
assert hashlib.sha256((ROOT / 'GameData/TRILOGY.EXE').read_bytes()).hexdigest() == manifest['sha256']
mu = Uc(UC_ARCH_X86, UC_MODE_32)
mu.mem_map(0x10000, 0x400000)
for obj in manifest['objects']:
    data = (ROOT / 'decompilation/image' / obj['file']).read_bytes()
    assert hashlib.sha256(data).hexdigest() == obj['sha256']
    mu.mem_write(obj['base'], data)
mu.mem_map(0x600000, 0x10000)
mu.mem_map(0x700000, 0x10000)
# Explicit 32-bit flat descriptors are needed for PUSH/POP segment instructions
# in Watcom's runtime; Unicorn's default null SS can imply a 16-bit stack.
mu.mem_map(0x800000, 0x1000)
mu.mem_write(0x800000, struct.pack('<QQQ', 0, 0x00cf9a000000ffff, 0x00cf92000000ffff))
mu.reg_write(UC_X86_REG_GDTR, (0, 0x800000, 23, 0))
mu.reg_write(UC_X86_REG_CS, 8)
for register in [UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS]: mu.reg_write(register, 16)
STOP, STACK, BUFFER = 0x700000, 0x60fff0, 0x700800
calls = []


def record_callback(uc, address, size, _):
    calls.append(address)


mu.hook_add(UC_HOOK_CODE, record_callback, begin=0x700100, end=0x7001ff)
mu.mem_write(0x700100, b'\xc3' * 256)
def invalid_memory(uc, access, address, size, value, _):
    print(f'Invalid emulated access at EIP={uc.reg_read(UC_X86_REG_EIP):x}: address={address:x}, size={size}', flush=True)
    return False
mu.hook_add(UC_HOOK_MEM_INVALID, invalid_memory)


def run(address, eax=0, edx=0, instruction_limit=100000):
    mu.mem_write(STACK, struct.pack('<I', STOP))
    mu.reg_write(UC_X86_REG_ESP, STACK)
    mu.reg_write(UC_X86_REG_EFLAGS, 2)
    mu.reg_write(UC_X86_REG_EAX, eax)
    mu.reg_write(UC_X86_REG_EDX, edx)
    mu.emu_start(address, STOP, count=instruction_limit)
    assert mu.reg_read(UC_X86_REG_EIP) == STOP, 'Original routine did not return'
    return mu.reg_read(UC_X86_REG_EAX)


rng = random.Random(0xA11E)
counts = {}
for i in range(1000):
    data = rng.randbytes(4)
    mu.mem_write(BUFFER + 1, data)
    native = (U8 * 4).from_buffer_copy(data)
    assert run(0x18590, BUFFER + 1, 0x12345678) == dll.be32_probe(native)
    assert mu.reg_read(UC_X86_REG_EDX) == 0x12345678
counts['big_endian_cases'] = 1000

texts = [b'', b'4294967296', b'-1', b' 123', b'9999999999999999999999999999']
texts += [bytes(rng.randrange(1, 256) for _ in range(rng.randrange(40))) for _ in range(1000)]
for text in texts:
    data = text + b'\0'
    mu.mem_write(BUFFER, data)
    native = (U8 * len(data)).from_buffer_copy(data)
    assert run(0x185c0, BUFFER, 0x98765432) == dll.decimal_probe(native)
    assert mu.reg_read(UC_X86_REG_EDX) == 0x98765432
counts['decimal_cases'] = len(texts)

word_addresses = [0xa0bb8, 0xa0c7c, 0xa0c94, 0xa0c98]
counter_addresses = [0xa0cb6, 0xa0cb8]


def input_case(words, counters, sample, reset=False):
    for address, value in zip(word_addresses, words): mu.mem_write(address, struct.pack('<I', value))
    for address, value in zip(counter_addresses, counters): mu.mem_write(address, struct.pack('<H', value))
    # Mock only the hardware poll result, in the emulated image, never the file.
    mu.mem_write(0x4c77c, b'\xb8' + struct.pack('<I', sample) + b'\xc3')
    mu.ctl_remove_cache(0x4c77c, 0x4c77c + 6)
    run(0x10db4 if reset else 0x10d00)
    expected_words = [struct.unpack('<I', mu.mem_read(a, 4))[0] for a in word_addresses]
    expected_counters = [struct.unpack('<H', mu.mem_read(a, 2))[0] for a in counter_addresses]
    native_words, native_counters = (U32 * 4)(*words), (U16 * 2)(*counters)
    dll.input_probe(native_words, native_counters, sample, int(reset))
    assert list(native_words) == expected_words, (words, counters, sample, expected_words, list(native_words))
    assert list(native_counters) == expected_counters, (words, counters, sample, expected_counters, list(native_counters))
    return expected_words, expected_counters


boundary = [0, 1, 10, 11, 19, 20, 21, 0x7ffe, 0x7fff, 0x8000, 0xffff]
total = 0
for stable in boundary:
    for repeat in boundary:
        for same in [False, True]:
            words = [rng.getrandbits(32) for _ in range(4)]
            sample = words[3] if same else words[3] ^ 1
            input_case(words, [repeat, stable], sample)
            total += 1
for _ in range(2000):
    words = [rng.getrandbits(32) for _ in range(4)]
    counters = [rng.getrandbits(16), rng.getrandbits(16)]
    input_case(words, counters, rng.getrandbits(32))
    input_case(words, counters, 0, True)
    total += 2
words, counters = [0] * 4, [0] * 2
for sample in [0] * 5 + [1] * 120 + [3] * 60 + [2] * 25 + [0] * 20:
    words, counters = input_case(words, counters, sample)
    total += 1
counts['input_update_and_reset_cases'] = total

for finalizers in [False, True]:
    size = 2 if finalizers else 7
    start = 0xa09a4 if finalizers else 0xa097a
    for case in range(600):
        # Small priority alphabet deliberately exercises ties and boundaries.
        rows = [(rng.choice([0, 1, 2, 255]), rng.choice([0, 1, 10, 20, 32, 255]),
                 rng.choice([0, 0x700100, 0x700110, 0x700120])) for _ in range(size)]
        low, high = rng.choice([0, 10, 32, 255]), rng.choice([0, 10, 32, 255])
        mu.mem_write(start, b''.join(struct.pack('<BBI', *row) for row in rows))
        calls.clear()
        run(0x6bda1 if finalizers else 0x6bd56, low if finalizers else high, high)
        states = [mu.mem_read(start + i * 6, 1)[0] for i in range(size)]
        native = (U32 * (size * 3))(*(v for row in rows for v in row))
        log = (U32 * size)()
        length = dll.runtime_probe(native, size, low, high, int(finalizers), log)
        assert list(log)[:length] == calls, (rows, low, high, list(log)[:length], calls)
        assert [native[i * 3] for i in range(size)] == states
counts['runtime_callback_table_cases'] = 1200
counts['total_cases'] = sum(counts.values())
report = dict(status='passed', reference_sha256=manifest['sha256'], emulator='Unicorn 2.1.4',
              counts=counts, limitations=['Hardware input polling is stubbed to a supplied sample.',
              'Runtime callbacks log invocations; callback mutation/reentrancy is not covered.',
              'These are isolated-routine comparisons, not a full gameplay equivalence test.'])
out = ROOT / 'decompilation/reports'
out.mkdir(exist_ok=True)
(out / 'behavior_checks.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
