"""Translate an owned Maxwell fragment shader with an unpredicated CAL followed by a BRA and run the emitted control flow.

The compiler's expansion of a square root tests its argument and either calls a slow routine (zero, denormals,
infinity, NaN) or runs a fast path, and a BRA after the call skips the fast path:

    FSETP.EQ P0, R4, RZ ; @!P0 BRA fast ; CAL slow ; BRA end ; fast: ... ; end: ... ; slow: ... ; RET

Ryujinx's decoder treated an unpredicated CAL as an unconditional branch without fall-through, so the BRA after it was
unreachable and the structurizer let the fast path run after the call as well, overwriting the slow path's result
(tools/xlate/patches/0019-calls-return-to-the-next-instruction.patch). Bananza's opening wall and the punch turned to
NaN through exactly that. The fixture marks the slow path (R0 = 1.0), the fast path (R0 = 2.0) and the code both reach
(R1 = 9.0); the test runs the emitted SPIR-V, including functions, branches and the call's variable arguments, for an
argument that takes each path. No game data or GPU is used.

Usage: python tests/call_fallthrough_test.py --translator build/xlate/totk_xlate.exe [--spirv-val PATH]
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PT = 7


def f32bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def bits32f(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


def fragment(instructions):
    header = [0] * 20
    header[0] = 5 << 10   # fragment stage
    header[18] = 15       # render target 0: R0, R1, R2, R3
    words = []
    for index in range(0, len(instructions), 3):
        words.extend([0, *instructions[index:index + 3]])   # Maxwell scheduling words
    return struct.pack('<20I', *header) + struct.pack('<' + 'Q' * len(words), *words) + bytes(256)


def address(k):
    """Instruction k sits after one scheduling word, plus one more for each complete group of three before it."""
    return 8 * (1 + k + k // 3)


def mov32i(dest, value):
    return 0x0100000000000000 | (PT << 16) | dest | (15 << 12) | (f32bits(value) << 20)


def fsetp_eq(pred, src_a, src_b):
    # FSETP.EQ.AND Pd, PT, Ra, Rb, PT as lower_to_maxwell.h emits it: Pd bits 3-5, the second result bits 0-2 (PT),
    # Ra bits 8-15, Rb bits 20-27, combining predicate bits 39-41 (PT), AND (0) at bits 45-46, compare code at 48.
    return (0x5bb0000000000000 | (PT << 16) | PT | (pred << 3) | (src_a << 8) | (src_b << 20) | (PT << 39) |
            (2 << 48))


def branch(kind, src, target, predicate_nibble):
    relative = address(target) - address(src) - 8
    return kind | (predicate_nibble << 16) | ((relative & 0xffffff) << 20) | 0xf


def shader():
    ldc = 0xef90000000000000 | (4 << 48) | (0 << 20) | (255 << 8) | (PT << 16) | 4   # LDC.32 R4, c[0][0]
    # k: 0 R2 = 0, 1 R3 = 0, 2 LDC, 3 FSETP P0 = (R4 == 0), 4 @!P0 BRA fast, 5 CAL slow, 6 BRA end,
    #    7 fast: R0 = 2, 8 end: R1 = 9, 9 EXIT, 10 slow: R0 = 1, 11 RET
    code = [mov32i(2, 0.0), mov32i(3, 0.0), ldc, fsetp_eq(0, 4, 255),
            branch(0xe240000000000000, 4, 7, 8),                      # @!P0 BRA fast
            0xe26000000007000f,                                      # CAL slow (offset below)
            branch(0xe240000000000000, 6, 8, PT),                     # BRA end
            mov32i(0, 2.0), mov32i(1, 9.0),
            0xe30000000007000f,                                      # EXIT
            mov32i(0, 1.0),
            0xe32000000007000f]                                      # RET
    code[5] = 0xe26000000007000f | (((address(10) - address(5) - 8) & 0xffffff) << 20)
    return fragment(code)


def module(path):
    data = path.read_bytes()
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    assert words[0] == 0x07230203
    out, i = [], 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 0xffff
        assert n and i + n <= len(words)
        out.append((op, list(words[i + 1:i + n])))
        i += n
    return out


class Cell:
    def __init__(self, value=0):
        self.value = value


def evaluate(code, c0):
    """Run the fragment entry point of a translated module and return the four output words (None if never stored).

    Handles what the fixture needs: functions with pointer parameters, labels and branches (merge instructions are
    ignored), Function variables, the uniform c[0][0] and the output, and the arithmetic the compare emits."""
    consts, types, var_class, chains = {}, {}, {}, {}
    functions, glsl, entry = {}, None, None
    current, blocks, label = None, None, None
    for op, a in code:
        if op == 11:
            if bytes(struct.pack('<%dI' % (len(a) - 1), *a[1:])).rstrip(b'\0') == b'GLSL.std.450':
                glsl = a[0]
        elif op == 15:
            entry = a[1]
        elif op in (43, 41, 42):   # OpConstant, OpConstantTrue, OpConstantFalse
            consts[a[1]] = a[2] if op == 43 else int(op == 41)
        elif op == 44:             # OpConstantComposite
            pass
        elif op == 59:
            var_class[a[1]] = a[2]
            if current is None:
                continue
        if op == 54:   # OpFunction
            current = {'id': a[1], 'params': [], 'blocks': {}, 'order': [], 'vars': []}
            functions[a[1]] = current
            label = None
        elif current is not None:
            if op == 55:    # OpFunctionParameter
                current['params'].append(a[1])
            elif op == 248:   # OpLabel
                label = a[0]
                current['blocks'][label] = []
                current['order'].append(label)
            elif op == 56:    # OpFunctionEnd
                current = None
            elif label is not None:
                current['blocks'][label].append((op, a))
    outputs = {}

    def run(fn, args):
        frame = {}
        for pid, cell in zip(fn['params'], args):
            frame[pid] = cell
        values = {}
        label = fn['order'][0]
        steps = 0

        def val(x):
            if x in values:
                return values[x]
            if x in consts:
                return consts[x]
            raise AssertionError('use of undefined id %d' % x)

        def cell_of(ptr):
            if ptr in chains:
                root, idx = chains[ptr]
                return ('chain', root, idx)
            if ptr in frame:
                return frame[ptr]
            if var_class.get(ptr) == 7:
                frame[ptr] = Cell(0)
                return frame[ptr]
            return ('global', ptr)

        while True:
            steps += 1
            assert steps < 100000, 'control flow does not terminate'
            next_label = None
            for op, a in fn['blocks'][label]:
                if op == 59:   # OpVariable (Function)
                    frame[a[1]] = Cell(consts.get(a[3], 0) if len(a) > 3 else 0)
                elif op == 65:   # OpAccessChain
                    chains[a[1]] = (a[2], [consts.get(x) for x in a[3:]])
                elif op == 61:   # OpLoad
                    c = cell_of(a[2])
                    if isinstance(c, Cell):
                        values[a[1]] = c.value
                    elif c[0] == 'chain' and var_class.get(c[1]) == 2:   # Uniform: c[0][0]
                        values[a[1]] = c0
                    else:
                        values[a[1]] = 0
                elif op == 62:   # OpStore
                    c = cell_of(a[0])
                    if isinstance(c, Cell):
                        c.value = val(a[1])
                    elif c[0] == 'chain' and var_class.get(c[1]) == 3:
                        outputs[c[2][-1] if c[2] else 0] = val(a[1])
                elif op == 57:   # OpFunctionCall
                    callee = functions[a[2]]
                    run(callee, [cell_of(p) for p in a[3:]])
                elif op == 124:  # OpBitcast
                    values[a[1]] = val(a[2])
                elif op == 156:  # OpIsNan
                    values[a[1]] = int(math.isnan(bits32f(val(a[2]))))
                elif op in (180, 182):   # OpFOrdEqual, OpFOrdNotEqual
                    x, y = bits32f(val(a[2])), bits32f(val(a[3]))
                    values[a[1]] = int((x == y) if op == 180 else (x != y and not math.isnan(x) and not math.isnan(y)))
                elif op in (181, 183):   # OpFUnordEqual, OpFUnordNotEqual
                    x, y = bits32f(val(a[2])), bits32f(val(a[3]))
                    values[a[1]] = int((x == y or math.isnan(x) or math.isnan(y)) if op == 181 else x != y)
                elif op in (164, 165, 166):   # OpLogicalEqual, OpLogicalNotEqual, OpLogicalOr
                    x, y = val(a[2]), val(a[3])
                    values[a[1]] = int(x == y) if op == 164 else int(x != y) if op == 165 else int(bool(x or y))
                elif op == 167:  # OpLogicalAnd
                    values[a[1]] = int(bool(val(a[2]) and val(a[3])))
                elif op == 168:  # OpLogicalNot
                    values[a[1]] = int(not val(a[2]))
                elif op == 169:  # OpSelect
                    values[a[1]] = val(a[3]) if val(a[2]) else val(a[4])
                elif op in (247, 246):   # OpSelectionMerge, OpLoopMerge
                    pass
                elif op == 249:  # OpBranch
                    next_label = a[0]
                elif op == 250:  # OpBranchConditional
                    next_label = a[1] if val(a[0]) else a[2]
                elif op in (253, 254):   # OpReturn, OpReturnValue
                    return
                else:
                    raise AssertionError('unsupported opcode %d in the fixture\'s control flow' % op)
            assert next_label is not None, 'block ends without a terminator'
            label = next_label

    run(functions[entry], [])
    return [outputs.get(i) for i in range(4)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translator', required=True, type=Path)
    parser.add_argument('--spirv-val', type=Path)
    args = parser.parse_args()
    out = ROOT / 'build/test/call-fallthrough'
    out.mkdir(parents=True, exist_ok=True)
    code = out / 'call_fallthrough.bin'
    code.write_bytes(shader())
    jobs = out / 'jobs.json'
    jobs.write_text(json.dumps([{'key': 'call_fallthrough', 'stage': 'ps', 'code': str(code.resolve()), 'debug': False}]))
    run = subprocess.run([str(args.translator.resolve()), str(jobs.resolve()), str(out.resolve())],
                         capture_output=True, encoding='utf-8')
    if run.returncode:
        raise AssertionError('translator failed: ' + run.stderr.encode('ascii', 'backslashreplace').decode())
    spirv = out / 'call_fallthrough.spv'
    if args.spirv_val:
        subprocess.run([str(args.spirv_val.resolve()), '--target-env', 'vulkan1.2', str(spirv)], check=True)
    module_code = module(spirv)
    failures = 0
    # 0.0 takes the call (slow path only); the others take the branch over it (fast path only).
    for x, want_r0 in ((0.0, 1.0), (3.0, 2.0), (-7.5, 2.0), (1e-30, 2.0)):
        got = evaluate(module_code, f32bits(x))
        want = [f32bits(want_r0), f32bits(9.0), 0, 0]
        if got != want:
            failures += 1
            print('FAIL x=%r: R0..R3 = %s, want R0 = %s (%s path), R1 = 9.0' % (
                x, [bits32f(g) if g is not None else None for g in got], want_r0, 'slow' if x == 0 else 'fast'))
    print('call fall-through: 4 inputs, %d failures' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
