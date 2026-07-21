#!/usr/bin/env python3
"""Report per-function stack-frame sizes for the DOS build, to hunt stack hogs.

Usage:
    # generate listings first (one wdis per object):
    for o in obj/dos32test/*.obj; do dos-toolchain wdis "$o" -l="${o%.obj}.lst"; done
    python3 scripts/dos-stack-report.py obj/dos32test/*.lst

Parses Watcom `wdis` listings and, for every routine, records the stack frame it
allocates (`enter 0xNN` / `sub esp,0xNN`). Big single frames are usually a local
buffer that should be static or heap; a tight stack budget (see Makefile-dos
STACK_SIZE) turns any such hog into a crash, so keep this list boring.
"""
import re, sys

# A routine header line in a wdis listing ends in "):" (a C++ signature) or is a
# bare "name:" label at column 0 after the address/bytes columns.
SIG = re.compile(r'^[0-9A-Fa-f]{4}\s+([A-Za-z_].*\)):\s*$')
NAME = re.compile(r'^[0-9A-Fa-f]{4}\s+([A-Za-z_][\w:]*):\s*$')
ENTER = re.compile(r'\benter\s+0x([0-9A-Fa-f]+)')
SUBESP = re.compile(r'\bsub\s+esp,0x([0-9A-Fa-f]+)')

def parse(path):
    cur = None
    frame = 0
    out = []
    for line in open(path, encoding='latin-1', errors='replace'):
        m = SIG.match(line) or NAME.match(line)
        if m:
            if cur is not None:
                out.append((frame, cur, path))
            cur = m.group(1).strip()
            frame = 0
            continue
        if cur is not None:
            e = ENTER.search(line) or SUBESP.search(line)
            if e:
                frame = max(frame, int(e.group(1), 16))
    if cur is not None:
        out.append((frame, cur, path))
    return out

def main(paths):
    rows = []
    for p in paths:
        rows.extend(parse(p))
    rows.sort(reverse=True, key=lambda r: r[0])
    total_big = sum(f for f, _, _ in rows if f >= 256)
    print(f"{'FRAME':>7}  FUNCTION  (module)")
    print(f"{'-'*7}  {'-'*60}")
    for frame, name, path in rows:
        if frame < 64:
            continue
        mod = path.split('/')[-1].replace('.lst', '')
        flag = '  <== BIG' if frame >= 512 else ('  <-- watch' if frame >= 256 else '')
        print(f"{frame:>7}  {name[:64]}  ({mod}){flag}")
    print(f"\nFrames >=256B sum to {total_big} bytes across "
          f"{sum(1 for f,_,_ in rows if f>=256)} functions "
          f"(a single deep call chain only touches a subset).")

if __name__ == '__main__':
    main(sys.argv[1:] or __import__('glob').glob('obj/dos32test/*.lst'))
