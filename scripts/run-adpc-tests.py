#!/usr/bin/env python3
"""Build and verify the redistributable Original256 toolchain fixture."""

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "games" / "original256"
RESULTS = FIXTURE / "release"
TRANSLATIONS = ("U+00DA=35", "U+00C1=36", "U+00D3=24", "U+00CD=37", "U+00C9=38")

EXPECTED = {
    "windows/part1.ddb": (14446, "603b91a49549028172cfd9f555e89040d957d21d7826090ae18c917b4486a425"),
    "windows/part2.ddb": (20332, "7fe9bb15cdf292e280458fc23e9456114bd5fedc0fb98de564b24df8ac4bd04c"),
    "amiga-ham/part1.ddb": (14448, "c3d03eaecf148d66e864a53268b4c0c3841575d64a42c5a213b5b1d985862621"),
    "amiga-ham/part2.ddb": (20412, "b47abd1881eb055b818369a680b21ba8731a39f28348d75a70de97eefafca902"),
    "windows/part1.dat": (2029565, "b5875c23f0b89c3b708aa66b0ce51e046e118183e616cf0719a29ee2859aecf0"),
    "windows/part2.dat": (2655209, "05c170b01bc7cf921bf996bffd5193973d672ef4934dd055494c22b6c2c61b64"),
    "amiga-ham/part1.dat": (430508, "6fff7fe93ff2ce0b14d3bc281f80a0025ca778fd21fc2892611fd7600dbf0b61"),
    "amiga-ham/part2.dat": (630781, "62246b59b61b683aaa66a149a37fefd03bdca6b090052f74d0d3dca23e3ee84c"),
}


def run(command: list[str]) -> bool:
    result = subprocess.run(command, cwd=FIXTURE, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if result.returncode != 0:
        print(result.stdout, end="")
        return False
    return True


def compile_ddb(adpc: Path, target: str, source: str, output: Path) -> bool:
    command = [str(adpc), "--target", target]
    for translation in TRANSLATIONS:
        command.extend(("--tr", translation))
    command.extend((str(FIXTURE / source), str(output)))
    return run(command)


def create_dat(dmg: Path, output: Path, source: str, mode: str) -> bool:
    command = [str(dmg), "create", str(output), str(FIXTURE / source),
               "-format=dat5", f"-mode={mode}"]
    if mode == "planar8":
        if source.startswith("part1-"):
            command.extend(("-width=480", "-height=192"))
        command.extend(("-remap=16-255", "-screen=640x400", "-2x=1"))
    else:
        if source.startswith("part1-"):
            command.extend(("-width=240", "-height=96"))
        command.append("-screen=320x200")
    return run(command)


def verify(path: Path, expected_size: int, expected_hash: str) -> bool:
    actual_size = path.stat().st_size if path.is_file() else -1
    if actual_size != expected_size:
        print(f"FAIL {path.relative_to(RESULTS)}: expected {expected_size} bytes, got {actual_size}")
        return False
    actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual_hash != expected_hash:
        print(f"FAIL {path.relative_to(RESULTS)}: SHA-256 differs")
        print(f"  expected {expected_hash}\n  actual   {actual_hash}")
        return False
    print(f"PASS {path.relative_to(RESULTS)} ({actual_size} bytes)")
    return True


def main() -> int:
    global RESULTS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adpc", required=True, type=Path)
    parser.add_argument("--dmg", required=True, type=Path)
    parser.add_argument("--dsk", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=RESULTS)
    args = parser.parse_args()
    adpc, dmg, dsk = args.adpc.resolve(), args.dmg.resolve(), args.dsk.resolve()
    RESULTS = args.output.resolve()
    if not adpc.is_file() or not dmg.is_file() or not dsk.is_file():
        parser.error("ADPC, DMG and DSK executables must exist")

    windows, amiga = RESULTS / "windows", RESULTS / "amiga-ham"
    windows.mkdir(parents=True, exist_ok=True)
    amiga.mkdir(parents=True, exist_ok=True)

    builds = (
        compile_ddb(adpc, "ibmpc", "part1.sce", windows / "part1.ddb"),
        compile_ddb(adpc, "ibmpc", "part2.sce", windows / "part2.ddb"),
        compile_ddb(adpc, "amiga", "part1.sce", amiga / "part1.ddb"),
        compile_ddb(adpc, "amiga", "part2.sce", amiga / "part2.ddb"),
        create_dat(dmg, windows / "part1.dat", "part1-svga", "planar8"),
        create_dat(dmg, windows / "part2.dat", "part2-svga", "planar8"),
        create_dat(dmg, amiga / "part1.dat", "part1-ham", "ham6"),
        create_dat(dmg, amiga / "part2.dat", "part2-ham", "ham6"),
    )
    if not all(builds):
        return 1

    for destination in (windows, amiga):
        shutil.copy2(FIXTURE / "part1.chr", destination / "part1.chr")
        shutil.copy2(FIXTURE / "part2.chr", destination / "part2.chr")
    shutil.copy2(FIXTURE / "part1-svga" / "part1.scr", windows / "part1.scr")
    shutil.copy2(FIXTURE / "windows.cfg", windows / "part1.cfg")
    shutil.copy2(FIXTURE / "amiga.cfg", amiga / "part1.cfg")
    shutil.copy2(ROOT / "daad-ready" / "dist" / "ASSETS" / "WINDOWS_EXPERIMENTAL" / "adp-player.exe", windows / "ad.exe")
    shutil.copy2(ROOT / "daad-ready" / "dist" / "ASSETS" / "AMIGA_EXPERIMENTAL" / "ADP.EXE", amiga / "ADP.EXE")
    startup = amiga / "s" / "startup-sequence"
    startup.parent.mkdir(parents=True, exist_ok=True)
    startup.write_text("CD :\nADP.EXE\n", encoding="ascii")
    adf = RESULTS / "original256-amiga-ham.adf"
    if not run([str(dsk), "create", "-b", "-r", str(adf), "hd", str(amiga)]):
        return 1

    valid = all(verify(RESULTS / relative, *expected) for relative, expected in EXPECTED.items())
    for dat in (windows / "part1.dat", windows / "part2.dat", amiga / "part1.dat", amiga / "part2.dat"):
        if not run([str(dmg), "test", str(dat)]):
            print(f"FAIL DMG validation: {dat.relative_to(RESULTS)}")
            valid = False
    if valid:
        print(f"Distributables written to {RESULTS}")
    return 0 if valid else 1


if __name__ == "__main__":
    sys.exit(main())
