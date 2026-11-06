"""Original portable GS body input/store regressions, without Vulkan or a GPU.

The real compiler consumes authored Maxwell. A deliberately small scalar SPIR-V
interpreter checks body stores and subsequent copies. Runtime admission uses the
existing full-TU cache fixture unchanged, with its disclosed fail-loud providers.
This does not implement passthrough linkage or GPU memory, and it checks only that a
single ViewportMask write translates, not how the primitive is culled.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest

import geometry_shader_fixture as geometry
import geometry_passthrough_fixture as portable
from xlate_geometry_test import GeometrySpirv

ROOT = Path(__file__).resolve().parents[1]
LOCATION = 6
CANARY = 0xa5a5a5a5


def pack_cache(cache):
    spec = importlib.util.spec_from_file_location('pack_shader_cache', ROOT / 'tools/pack_shader_cache.py')
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    packer.pack(cache, keep=True)
POSITIONS = [tuple(0x3e800001 + vertex * 32 + c for c in range(4)) for vertex in range(3)]
ATTRIBUTES = {LOCATION: [tuple(0x3f000001 + vertex * 32 + c for c in range(4)) for vertex in range(3)]}


def body_program(dynamic=False, location=LOCATION, omit_value=False):
    """Store original Position.x and user.z before the passthrough epilogue.

    Registers0..3/4..7 hold the two ALDs;10/11 are the independent c3 pointer.
    The dynamic index is c3 word2 AND1, hence valid for the three-vertex input.
    LOP32I's AND selector is0, SrcA bits8..15 and immediate bits20..51.
    """
    ops = [geometry.movc(10, 3, 0), geometry.movc(11, 3, 1)]
    if dynamic:
        ops += [geometry.movc(12, 3, 2), 0x0400000000000000 | (1 << 20) | geometry.PT | (12 << 8) | 12]
    else:
        ops += [geometry.movi(12, 2)]
    ops += [geometry.ald(0, 0x70, 12), geometry.ald(4, 0x80 + location * 16, 12)]
    if omit_value:
        ops += [geometry.movi(6, 0)]
    for register, offset in ((0, 0), (6, 4)):
        ops += [0xeed8000000000000 | (4 << 48) | (1 << 45) | (offset << 20) |
                (10 << 8) | geometry.PT | register]
    code = bytearray(portable.program(extra=ops))
    struct.pack_into('<I', code, 0, struct.unpack_from('<I', code)[0] | (1 << 16))
    return bytes(code)


class BodyModule(GeometrySpirv):
    """Execute only the original straight-line scalar fixtures in this file.

    Supported operations are scalar load/store/access-chain/copy/bitcast,
    32-bit integer AND, and geometry emission. Undefined memory, other opcodes, extra
    functions/blocks, unknown resources and out-of-range vertices fail loudly.
    This models raw words, not floating-point arithmetic or the Vulkan memory
    model. It clears Output memory after every emit and tracks pointer/value
    dependencies for the exact executed original inputs.
    """
    def __init__(self, data, reflection):
        super().__init__(data)
        bindings = {o[0]: o[2] for op, o in self.ops if op == 71 and o[1] == 33}
        resources = reflection['sbufs']
        if len(resources) != 1 or (resources[0]['cb_slot'], resources[0]['cb_offset']) != (3, 0):
            raise AssertionError('fixture requires one c3-pointer SSBO')
        storage = [var for var, binding in bindings.items() if binding == resources[0]['binding']]
        constants = reflection['cbufs']
        if any(resource['slot'] != 3 for resource in constants) or len(constants) > 1 or len(storage) != 1:
            raise AssertionError('unexpected fixture resource')
        uniform = [var for var, binding in bindings.items() if constants and binding == constants[0]['binding']]
        if len(uniform) != len(constants):
            raise AssertionError('missing c3 UBO')
        self.storage, self.uniform = storage[0], uniform[0] if uniform else None

    def run_body(self, positions, attributes, raw_index=0):
        values, memory, memory_deps = dict(self.constants), {}, {}
        dependencies = {key: frozenset() for key in values}
        pointers = {var: (var,) for var in self.variables}
        pointer_deps = {var: frozenset() for var in pointers}
        ip, im = self.position(1)
        oposition, om = self.position(3)
        out_locations = {loc: self.location(3, loc) for loc in attributes}
        for vertex, position in enumerate(positions):
            for component, value in enumerate(position):
                memory[ip, vertex, im, component] = value
        for location, vertices in attributes.items():
            var = self.location(1, location)
            if self.input_array_length(var) != 3:
                raise AssertionError('expected three input vertices')
            for vertex, vector in enumerate(vertices):
                for component, value in enumerate(vector):
                    memory[var, vertex, component] = value
        # Synthetic guest token only; this CPU dictionary never dereferences it.
        if self.uniform is not None:
            for word, value in enumerate((0, 2, raw_index, 0)):
                memory[self.uniform, 0, 0, word] = value
        for word in range(6):
            memory[self.storage, 0, word] = CANARY
        writes, emitted, cuts, events = [], [], [], []
        for opcode, operands in self.body:
            if opcode in (248, 253):
                continue
            if opcode == 59:
                if len(operands) != 3 or operands[2] != 7 or operands[1] not in pointers:
                    raise AssertionError('unsupported function variable')
            elif opcode == 65:
                base = operands[2]
                pointers[operands[1]] = pointers[base] + tuple(values[i] for i in operands[3:])
                pointer_deps[operands[1]] = pointer_deps[base] | frozenset().union(
                    *(dependencies[i] for i in operands[3:]))
            elif opcode == 61:
                pointer = pointers[operands[2]]
                if pointer not in memory:
                    raise AssertionError('undefined or out-of-range fixture load')
                values[operands[1]] = memory[pointer]
                dep = pointer_deps[operands[2]] | memory_deps.get(pointer, frozenset())
                if pointer[0] == ip:
                    dep |= frozenset({('position', pointer[1], pointer[-1])})
                elif self.variables[pointer[0]][0] == 1:
                    dep |= frozenset({('user', self.locations[pointer[0]], pointer[1], pointer[-1])})
                elif pointer[0] == self.uniform:
                    dep |= frozenset({('uniform', pointer[-1])})
                dependencies[operands[1]] = dep
            elif opcode == 62:
                pointer, value_id = pointers[operands[0]], operands[1]
                if pointer[0] == self.storage:
                    if pointer not in memory:
                        raise AssertionError('out-of-range fixture store')
                    writes.append((pointer[-1], values[value_id], dependencies[value_id]))
                    events.append('store')
                memory[pointer] = values[value_id]
                memory_deps[pointer] = dependencies[value_id] | pointer_deps[operands[0]]
            elif opcode in (83, 124):
                if self.types[operands[0]] not in ((21, (32, 0)), (21, (32, 1)), (22, (32,))):
                    raise AssertionError('unsupported non-scalar or non-32-bit copy')
                values[operands[1]] = values[operands[2]]
                dependencies[operands[1]] = dependencies[operands[2]]
            elif opcode == 199:  # OpBitwiseAnd has identical raw bits for int32/uint32.
                if self.types[operands[0]] not in ((21, (32, 0)), (21, (32, 1))):
                    raise AssertionError('unsupported non-32-bit integer AND')
                values[operands[1]] = values[operands[2]] & values[operands[3]]
                dependencies[operands[1]] = dependencies[operands[2]] | dependencies[operands[3]]
            elif opcode == 218:
                emitted.append((tuple(memory[oposition, om, c] for c in range(4)),
                                {loc: tuple(memory[var, c] for c in range(4)) for loc, var in out_locations.items()}))
                events.append('emit')
                memory = {p: value for p, value in memory.items() if self.variables[p[0]][0] != 3}
            elif opcode == 219:
                cuts.append(len(emitted))
            else:
                raise AssertionError('unsupported body fixture opcode ' + str(opcode))
        record = tuple(memory[self.storage, 0, word] for word in range(6))
        return dict(record=record, writes=writes, emitted=emitted, cuts=cuts, events=events)


def check_record(result, vertex, positions=POSITIONS, attributes=ATTRIBUTES):
    expected = (positions[vertex][0], attributes[LOCATION][vertex][2], *([CANARY] * 4))
    if result['record'] != expected:
        raise AssertionError('body record/canary oracle mismatch')


class BodyInputs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        args.out.mkdir(parents=True, exist_ok=False)
        cls.env = {k: v for k, v in os.environ.items() if k.upper() in {
            'SYSTEMROOT', 'WINDIR', 'PATH', 'TEMP', 'TMP', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA',
            'PROGRAMFILES', 'PROGRAMFILES(X86)', 'PROGRAMDATA', 'SYSTEMDRIVE'}}
        cls.env['DOTNET_PROCESSOR_COUNT'] = '2'
        cls.modules, cls.reports, cls.jobs = {}, {}, {}
        cls.validations = 0
        packets = {'static': body_program(), 'dynamic': body_program(dynamic=True),
                   'omitted_value': body_program(omit_value=True), 'outside_mask': body_program(location=7),
                   'viewport_mask': portable.program(extra=[geometry.ast(0, 0x3a0, words=1)]),
                   'reject_ast': portable.program(extra=[geometry.ast(0, 0x70)]),
                   'reject_viewport_two_words': portable.program(extra=[geometry.ast(0, 0x3a0, words=2)]),
                   'reject_out': portable.program(extra=[geometry.out_vertex()])}
        jobs = []
        for name, code in packets.items():
            path = args.out / (name + '.code')
            path.write_bytes(code)
            job = dict(key=name, stage='gs', code=str(path.resolve()), c1='', debug=False,
                       geometry_version=2, gs_input='triangles', gs_passthrough_skip=portable.skip_words([LOCATION]))
            jobs.append(job)
            cls.jobs[name] = job
        run = subprocess.run([str(args.xlate), '--serve'], env=cls.env,
            input=''.join(json.dumps(dict(job=job, out=str(args.out.resolve()))) + '\n' for job in jobs),
            capture_output=True, text=True, timeout=60)
        (args.out / 'translator.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        if run.returncode or len(run.stdout.splitlines()) != len(jobs):
            raise AssertionError(run.stdout + run.stderr)
        cls.replies = dict(zip(packets, run.stdout.splitlines()))
        for name in ('static', 'dynamic', 'omitted_value', 'outside_mask'):
            if cls.replies[name] != 'ok ' + name:
                raise AssertionError(cls.replies[name])
            spv = args.out / (name + '.spv')
            validation = subprocess.run([str(args.spirv_val), '--target-env', 'vulkan1.3', str(spv)],
                                        capture_output=True, text=True, timeout=15)
            (args.out / (name + '.val.log')).write_text(validation.stdout + validation.stderr, encoding='utf-8')
            if validation.returncode:
                raise AssertionError(validation.stdout + validation.stderr)
            cls.validations += 1
            cls.reports[name] = json.loads((args.out / (name + '.res.json')).read_text(encoding='utf-8'))
            cls.modules[name] = BodyModule(spv.read_bytes(), cls.reports[name])
        cls.build_runtime_fixture()

    @classmethod
    def build_runtime_fixture(cls):
        # Reuse the existing actual-TU fixture and its providers, without running
        # its unrelated topology/cache suites or rewriting production functions.
        source = (ROOT / 'tests/geometry_shader_cache.c').read_text(encoding='utf-8')
        source = source.replace('@SHADER_SOURCE@', (ROOT / 'runtime/gfx_shader.c').as_posix())
        source = source.replace('@HASH_CALL@', 'build_code_hash(stage, control, code, size, c1, c1_size, input, hash)')
        path = args.out / 'runtime-fixture.c'
        path.write_text('#define FIXTURE_GEOMETRY 1\n' + source, encoding='utf-8')
        cls.runtime = args.out / ('runtime-fixture.exe' if os.name == 'nt' else 'runtime-fixture')
        runtime = ROOT / 'runtime'
        platform = 'win32.c' if os.name == 'nt' else 'linux.c'
        libraries = ['-lbcrypt', '-lpdh', '-lpsapi', '-lrstrtmgr', '-lshell32', '-lwinmm'] if os.name == 'nt' else ['-ldl', '-pthread']
        command = [str(args.cc), '-std=gnu11', '-O2', '-ffunction-sections', '-fdata-sections', '-I', str(runtime),
                   str(path), str(runtime / 'sha256.c'), str(runtime / 'platform' / platform),
                   str(runtime / 'platform/common.c'), '-Wl,--gc-sections', *libraries, '-o', str(cls.runtime)]
        run = subprocess.run(command, capture_output=True, text=True, timeout=90)
        (args.out / 'runtime-compile.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)

    def copies(self, result):
        self.assertEqual(result['emitted'], [(POSITIONS[v], {LOCATION: ATTRIBUTES[LOCATION][v]}) for v in range(3)])
        self.assertEqual(result['cuts'], [3])
        self.assertEqual(result['events'], ['store', 'store', 'emit', 'emit', 'emit'])

    def test_static_original_input_values_and_copies(self):
        result = self.modules['static'].run_body(POSITIONS, ATTRIBUTES)
        check_record(result, 2)
        self.copies(result)

    def test_nonconstant_vertex_is_bounded_and_values_remain_original(self):
        module = self.modules['dynamic']
        self.assertEqual(sum(op == 199 for op, _ in module.body), 1)
        operation = next(o for op, o in module.body if op == 199)
        self.assertEqual([module.constants[i] for i in operation[2:] if i in module.constants], [1])
        for raw in (0, 1, 2, 3, 0xffffffff):
            with self.subTest(raw_index=raw):
                result = module.run_body(POSITIONS, ATTRIBUTES, raw)
                check_record(result, raw & 1)
                self.copies(result)
                self.assertIn(('uniform', 2), result['writes'][0][2])

    def test_storage_dependency_and_emission_order(self):
        for name, vertex in (('static', 2), ('dynamic', 1)):
            with self.subTest(packet=name):
                result = self.modules[name].run_body(POSITIONS, ATTRIBUTES, 1)
                self.assertEqual([word for word, _, _ in result['writes']], [0, 1])
                self.assertIn(('position', vertex, 0), result['writes'][0][2])
                self.assertIn(('user', LOCATION, vertex, 2), result['writes'][1][2])
                self.assertEqual(result['events'][:2], ['store', 'store'])
                resource = self.reports[name]['sbufs']
                self.assertEqual(len(resource), 1)
                self.assertEqual((resource[0]['cb_slot'], resource[0]['cb_offset'], resource[0]['flags']), (3, 0, 1))
                self.assertEqual(self.reports[name]['notes'], [])

    def test_omitted_dependency_is_a_nonvacuous_counterexample(self):
        result = self.modules['omitted_value'].run_body(POSITIONS, ATTRIBUTES)
        self.copies(result)
        self.assertNotIn(('user', LOCATION, 2, 2), result['writes'][1][2])
        with self.assertRaisesRegex(AssertionError, 'body record/canary oracle mismatch'):
            check_record(result, 2)

    def fixture(self, mode, cache, *parameters):
        run = subprocess.run([str(self.runtime.resolve()), mode, str(cache.resolve()), *map(str, parameters)],
                             env=self.env, capture_output=True, text=True, timeout=15)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return json.loads(run.stdout.splitlines()[-1])

    def test_runtime_accepts_body_input_outside_passthrough_output(self):
        outside = self.modules['outside_mask']
        self.assertEqual({(storage, outside.locations[var]) for var, (storage, _) in outside.variables.items()
                          if var in outside.locations and storage in (1, 3)}, {(1, LOCATION), (1, 7), (3, LOCATION)})
        for name in ('static', 'dynamic', 'outside_mask'):
            with self.subTest(packet=name):
                code = bytes(0x30) + Path(self.jobs[name]['code']).read_bytes()
                cp, raw, packed = (args.out / (name + suffix) for suffix in ('.fullcode', '.rawcontrol', '.control'))
                cp.write_bytes(code)
                control = bytearray(0x880)
                struct.pack_into('<II', control, 0x700, len(code), len(code))
                struct.pack_into('<I', control, 0x714, 2)
                raw.write_bytes(control)
                cache = args.out / ('runtime-' + name)
                result = self.fixture('pack-pass', cache, cp.resolve(), raw.resolve(),
                                      ','.join(map(hex, portable.skip_words([LOCATION]))), packed.resolve())
                self.assertTrue(result['accepted'])
                result = self.fixture('seed-pass', cache, cp.resolve(), packed.resolve(),
                                      (args.out / (name + '.spv')).resolve(), (args.out / (name + '.res.json')).resolve())
                self.assertTrue(result['accepted'])

    def test_raw_passthrough_may_read_an_input_it_does_not_forward(self):
        name = 'outside_mask'
        result = self.fixture('reflect-flags', args.out / 'runtime-flags', (args.out / (name + '.spv')).resolve(),
                              (args.out / (name + '.res.json')).resolve())
        self.assertEqual(result, dict(strict=True, lowered=True))
        result = self.fixture('reflect-flags', args.out / 'runtime-flags', (args.out / 'static.spv').resolve(),
                              (args.out / 'static.res.json').resolve())
        self.assertEqual(result, dict(strict=True, lowered=True))

    def test_lowered_body_loads_from_a_cold_input_cache(self):
        name = 'outside_mask'
        code = bytes(0x30) + Path(self.jobs[name]['code']).read_bytes()
        cp, raw, packed = (args.out / ('cold-lowered' + suffix) for suffix in ('.fullcode', '.rawcontrol', '.control'))
        cp.write_bytes(code)
        control = bytearray(0x880)
        struct.pack_into('<II', control, 0x700, len(code), len(code))
        struct.pack_into('<I', control, 0x714, 2)
        raw.write_bytes(control)
        cache = args.out / 'runtime-cold-lowered'
        result = self.fixture('pack-pass', cache, cp.resolve(), raw.resolve(),
                              ','.join(map(hex, portable.skip_words([LOCATION]))), packed.resolve())
        self.assertTrue(result['accepted'])
        result = self.fixture('seed-lowered', cache, cp.resolve(), packed.resolve(),
                              (args.out / (name + '.spv')).resolve(), (args.out / (name + '.res.json')).resolve())
        self.assertTrue(result['accepted'])
        self.assertEqual(struct.unpack_from('<I', packed.read_bytes(), 0x810)[0], 3)
        request = self.fixture('request-load', cache, 3, 4, cp.resolve(), packed.resolve())
        self.assertTrue(request['accepted'])
        self.assertEqual(request['iid'], result['iid'])
        for kind in ('input', 'private'):
            with self.subTest(kind=kind):
                loaded = self.fixture('load', cache, kind, result['key'], result['iid'])
                self.assertTrue(loaded['accepted'])
                self.assertEqual(loaded['iid'], result['iid'])
        stored = cache / 'in' / (result['iid'] + '.in')
        source = stored.read_bytes()
        control_at = 24 + struct.unpack_from('<I', source, 20)[0]
        unmarked = bytearray(source)
        struct.pack_into('<I', unmarked, control_at + 0x810, 1)
        mismatched = bytearray(source)
        mismatched[control_at] ^= 1
        for label, data in (('missing', None), ('unmarked', unmarked), ('different-source', mismatched)):
            if data is None:
                stored.unlink()
            else:
                stored.write_bytes(data)
            try:
                for kind in ('input', 'private'):
                    with self.subTest(source=label, kind=kind):
                        loaded = self.fixture('load', cache, kind, result['key'], result['iid'])
                        self.assertFalse(loaded['accepted'])
            finally:
                stored.write_bytes(source)
        pack_cache(cache)
        for path in (stored, cache / (result['key'] + '.spv'), cache / (result['key'] + '.res.json')):
            path.unlink()
        for kind in ('input-packed', 'private-packed'):
            with self.subTest(kind=kind):
                loaded = self.fixture('load', cache, kind, result['key'], result['iid'])
                self.assertTrue(loaded['accepted'])
                self.assertEqual(loaded['iid'], result['iid'])

    def test_a_matching_loose_source_allows_an_extra_input(self):
        code = bytes(0x30) + Path(self.jobs['static']['code']).read_bytes()
        cp, raw, packed = (args.out / ('cross-layer' + suffix) for suffix in ('.fullcode', '.rawcontrol', '.control'))
        cp.write_bytes(code)
        control = bytearray(0x880)
        struct.pack_into('<II', control, 0x700, len(code), len(code))
        struct.pack_into('<I', control, 0x714, 2)
        raw.write_bytes(control)
        cache = args.out / 'runtime-cross-layer'
        result = self.fixture('pack-pass', cache, cp.resolve(), raw.resolve(),
                              ','.join(map(hex, portable.skip_words([LOCATION]))), packed.resolve())
        self.assertTrue(result['accepted'])
        result = self.fixture('seed-pass', cache, cp.resolve(), packed.resolve(),
                              (args.out / 'static.spv').resolve(), (args.out / 'static.res.json').resolve())
        self.assertTrue(result['accepted'])
        stored = cache / 'in' / (result['iid'] + '.in')
        source = stored.read_bytes()
        control_at = 24 + struct.unpack_from('<I', source, 20)[0]
        wrong = bytearray(source)
        struct.pack_into('<I', wrong, control_at + 0x810, 3)
        wrong[control_at] ^= 1
        stored.write_bytes(wrong)
        pair = [cache / (result['key'] + suffix) for suffix in ('.spv', '.res.json')]
        for path, suffix in zip(pair, ('.spv', '.res.json')):
            path.write_bytes((args.out / ('outside_mask' + suffix)).read_bytes())
        pack_cache(cache)
        # The pack holds a marked source with the wrong identity. The loose input is the correct portable source.
        stored.write_bytes(source)
        for kind in ('input-packed', 'private-packed'):
            with self.subTest(kind=kind, loose='extra-io'):
                loaded = self.fixture('load', cache, kind, result['key'], result['iid'])
                self.assertTrue(loaded['accepted'])
        # A replacement with exactly the forwarded input remains usable too.
        for path, suffix in zip(pair, ('.spv', '.res.json')):
            path.write_bytes((args.out / ('static' + suffix)).read_bytes())
        for kind in ('input-packed', 'private-packed'):
            with self.subTest(kind=kind, loose='strict'):
                loaded = self.fixture('load', cache, kind, result['key'], result['iid'])
                self.assertTrue(loaded['accepted'])
                self.assertEqual(loaded['iid'], result['iid'])

    def test_viewport_mask_write_is_the_one_explicit_output(self):
        self.assertEqual(self.replies['viewport_mask'], 'ok viewport_mask')

    def test_explicit_output_and_emit_remain_unsupported(self):
        for name in ('reject_ast', 'reject_viewport_two_words', 'reject_out'):
            self.assertTrue(self.replies[name].startswith('fail ' + name + ' '))
            self.assertFalse((args.out / (name + '.spv')).exists())
            self.assertFalse((args.out / (name + '.res.json')).exists())

    def test_interpreter_rejects_unimplemented_behavior(self):
        module = BodyModule((args.out / 'static.spv').read_bytes(), self.reports['static'])
        module.body = [(0xffff, ())] + module.body
        with self.assertRaisesRegex(AssertionError, 'unsupported body fixture opcode'):
            module.run_body(POSITIONS, ATTRIBUTES)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xlate', type=Path, required=True)
    parser.add_argument('--spirv-val', type=Path, required=True)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args, rest = parser.parse_known_args()
    result = unittest.main(argv=[__file__] + rest, exit=False).result
    evidence = dict(tests=result.testsRun, failures=len(result.failures), errors=len(result.errors), skipped=len(result.skipped),
                    complete_spirv_validations=getattr(BodyInputs, 'validations', 0),
                    translator_sha256=hashlib.sha256(args.xlate.read_bytes()).hexdigest(),
                    test_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), gpu_executed=False,
                    providers='Existing geometry_shader_cache.c full-TU fixture: real hashes/files/locks; child/background work forbidden.')
    if args.out.is_dir():
        (args.out / 'result.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(not result.wasSuccessful())
