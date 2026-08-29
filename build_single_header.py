"""
Build single-header stb_avif.h for distribution.

Reads stb_avif.h and replaces each #include "stb_av1_*.h" with the actual file
content, recursively expanding all internal includes.
"""

import os
import re
import sys
import shutil
import subprocess
import tempfile

SRC_DIR = r'.'
DIST_DIR = os.path.join(SRC_DIR, 'dist')

INCLUDE_RE = re.compile(r'^\s*#include\s+"(stb_av1_\w+\.h)"\s*$', re.MULTILINE)
GUARD_RE = re.compile(r'^\s*#ifndef\s+(STB_AV1_\w+_H)\s*$')


def extract_guard(content):
    for line in content.splitlines():
        m = GUARD_RE.match(line)
        if m:
            return m.group(1)
    return None


def expand_includes(fname, seen_guards, cache):
    """Recursively read a sub-header, strip its #include lines, and return content."""
    fpath = os.path.join(SRC_DIR, fname)
    if not os.path.exists(fpath):
        return f'/* WARNING: {fname} not found */\n'

    with open(fpath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # Check guard first (before cache) - if already included, return empty
    guard = extract_guard(content)
    if guard and guard in seen_guards:
        return ''
    if fname in cache:
        return cache[fname]
    if guard:
        seen_guards.add(guard)

    # Process each line, expanding includes recursively
    result = []
    result.append(f'\n/* ===== {fname} ===== */\n')
    for line in content.splitlines(keepends=True):
        m = re.match(r'\s*#include\s+"(stb_av1_\w+\.h)"', line)
        if m:
            dep = m.group(1)
            dep_content = expand_includes(dep, seen_guards, cache)
            if dep_content:
                result.append(dep_content)
        else:
            result.append(line)

    expanded = ''.join(result)
    if not expanded.endswith('\n'):
        expanded += '\n'
    cache[fname] = expanded
    return expanded


def build_single_header():
    os.makedirs(DIST_DIR, exist_ok=True)

    # --- Read main header ---
    main_path = os.path.join(SRC_DIR, 'stb_avif.h')
    with open(main_path, 'r', encoding='utf-8', errors='replace') as f:
        main_content = f.read()

    seen_guards = set()
    cache = {}

    # --- Remove #ifndef STB_AVIF_USE_DAV1D wrapper blocks ---
    lines = main_content.splitlines(keepends=True)
    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if stripped == '#ifndef STB_AVIF_USE_DAV1D':
            j = i + 1
            only_includes = True
            depth = 1
            block_end = -1
            include_lines = []
            while j < len(lines) and depth > 0:
                s = lines[j].strip()
                if s.startswith('#ifndef') or s.startswith('#ifdef'):
                    depth += 1
                elif s == '#endif':
                    depth -= 1
                    if depth == 0:
                        block_end = j
                elif re.match(r'\s*#include\s+"stb_av1_\w+\.h"', s):
                    include_lines.append(lines[j])
                elif s == '' or s.startswith('/*') or s.startswith('*') or s.startswith('//'):
                    pass
                elif s.startswith('#else'):
                    pass
                else:
                    only_includes = False
                j += 1

            if only_includes and block_end >= 0:
                for inc_line in include_lines:
                    m = re.match(r'\s*#include\s+"(stb_av1_\w+\.h)"', inc_line)
                    if m:
                        content = expand_includes(m.group(1), seen_guards, cache)
                        if content:
                            result.append(content)
                i = block_end + 1
                continue

        # Handle standalone #include "stb_av1_*.h" lines
        m = re.match(r'\s*#include\s+"(stb_av1_\w+\.h)"', stripped)
        if m:
            content = expand_includes(m.group(1), seen_guards, cache)
            if content:
                result.append(content)
            i += 1
            continue

        result.append(line)
        i += 1

    combined = ''.join(result)

    if not combined.endswith('\n'):
        combined += '\n'

    out_path = os.path.join(DIST_DIR, 'stb_avif.h')
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(combined)

    print(f"Combined: {combined.count(chr(10))} lines -> {out_path}")
    print(f"Sub-headers expanded: {len(cache)}")

    # --- Compilation check ---
    print("\nCompilation check...")
    tmpdir = tempfile.mkdtemp(prefix='stb_avif_build_')
    test_c = os.path.join(tmpdir, '_build_test.c')
    exe_out = os.path.join(tmpdir, '_build_test.exe')
    with open(test_c, 'w', encoding='ascii', newline='\n') as f:
        f.write('#define STB_AVIF_IMPLEMENTATION\n')
        f.write('#include "stb_avif.h"\n')
        f.write('int main(void){return 0;}\n')
    tmp_hdr = os.path.join(tmpdir, 'stb_avif.h')
    shutil.copy2(out_path, tmp_hdr)

    gcc = r'gcc'
    result = subprocess.run(
        [gcc, '-std=c89', '-O1', '-DSTB_AVIF_DEBLOCK', '-o', exe_out, test_c, '-lm'],
        capture_output=True, text=True, cwd=tmpdir
    )
    print(f"Compilation rc={result.returncode}")
    if result.stderr:
        errors = [l for l in result.stderr.splitlines() if 'error:' in l.lower()]
        if errors:
            print(f"Errors ({len(errors)}):")
            for e in errors[:15]:
                print(f"  {e}")
        else:
            warnings = [l for l in result.stderr.splitlines() if 'warning:' in l.lower()]
            print(f"OK ({len(warnings)} warnings, pre-existing)")

    shutil.rmtree(tmpdir, ignore_errors=True)
    return out_path


if __name__ == '__main__':
    build_single_header()
