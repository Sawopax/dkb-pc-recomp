"""Compile and run owned TXD register-routing fixtures against the real lowering header.

This checks component destinations, preserved registers, and predicates, not hardware texture filtering.
Use --source-root for an unmodified checkout to verify that the RGB cases fail before the change.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=shutil.which('clang++'))
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/test/native-txd-mask')
    args = parser.parse_args()
    if not args.cxx:
        parser.error('pass --cxx or put the UCRT64 compiler on PATH')
    args.out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cxx).resolve().parent) + os.pathsep + env.get('PATH', '')
    for opt in ('0', '2'):
        exe = args.out.resolve() / ('txd-O' + opt + ('.exe' if os.name == 'nt' else ''))
        command = [args.cxx, '-std=c++20', '-O' + opt, '-Wall', '-Wextra', '-Werror',
                   '-I', str(args.source_root / 'runtime/third_party/eglantine'),
                   str(ROOT / 'tests/native_txd_mask_test.cpp'), '-o', str(exe)]
        build = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
        (args.out / ('compile-O' + opt + '.log')).write_text(build.stdout + build.stderr)
        if build.returncode:
            print(build.stdout + build.stderr)
            return build.returncode
        run = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=30)
        (args.out / ('run-O' + opt + '.log')).write_text(run.stdout + run.stderr)
        print('O' + opt, run.stdout.strip())
        if run.returncode:
            print(run.stderr)
            return run.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
