"""The alpha test reference as the pipeline key holds it (vk_pipeline.c alpha_ref_step).

The real function is cut out of the source and compiled alone. It must round inside (0, 1) to the nearest 1/255, keep
0, 1, values outside and NaN as they are, move no value by more than half a step, and give one key per 8-bit step of a
slow fade.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
@STEP@
static unsigned failures;
static void check(int ok, const char *what, float in, float out) {
    if (!ok) failures++, fprintf(stderr, "FAIL %s: %.9g -> %.9g\n", what, in, out);
}
int main(void) {
    float same[] = {0.0f, 1.0f, -0.5f, 1.5f, 1000.0f};
    for (unsigned i = 0; i < sizeof same / sizeof *same; i++)
        check(alpha_ref_step(same[i]) == same[i], "kept outside (0, 1)", same[i], alpha_ref_step(same[i]));
    float nan;
    uint32_t bits = 0x7fc00000u;
    memcpy(&nan, &bits, 4);
    float n = alpha_ref_step(nan);
    check(n != n, "NaN stays NaN", nan, n);
    for (int k = 1; k < 255; k++) {
        float exact = (float)k / 255.0f;
        check(alpha_ref_step(exact) == exact, "a step maps to itself", exact, alpha_ref_step(exact));
    }
    unsigned keys = 0;
    float last = -1.0f;
    for (int i = 1; i < 100000; i++) {   // a fade far slower than one step per frame
        float in = (float)i / 100000.0f, out = alpha_ref_step(in);
        float d = out > in ? out - in : in - out;
        check(d <= 0.5f / 255.0f + 1e-6f, "moves by at most half a step", in, out);
        check(out >= last, "never goes back", in, out);
        if (out != last) keys++, last = out;
    }
    check(keys <= 256, "one key per step of the fade", (float)keys, 256.0f);
    printf("alpha_ref_step: %u keys over the fade, %u failures\n", keys, failures);
    return failures != 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc'))
    args = parser.parse_args()
    if not args.cc:
        parser.error('pass --cc with a C compiler')
    source = (ROOT / 'runtime/vk_pipeline.c').read_text(encoding='utf-8')
    if 'alpha_ref = alpha_ref_step(s->alpha_ref)' not in function(source, 'static void gkey_fill('):
        print('gkey_fill does not key the rounded reference')
        return 1
    with tempfile.TemporaryDirectory() as temp:
        src = Path(temp) / 'fixture.c'
        src.write_text(FIXTURE.replace('@STEP@', function(source, 'static float alpha_ref_step(')), encoding='utf-8')
        for opt in ('-O0', '-O2'):
            exe = Path(temp) / ('fixture' + opt + ('.exe' if os.name == 'nt' else ''))
            built = subprocess.run([args.cc, '-std=gnu11', opt, '-Wall', '-Wextra', '-Werror', str(src), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            if built.returncode:
                print(built.stdout + built.stderr)
                return built.returncode
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            print(opt, run.stdout + run.stderr, end='')
            if run.returncode:
                return run.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
