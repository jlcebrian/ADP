#!/bin/bash
# Debug the Amiga player from the command line, without VSCode.
#
# Builds the debug configuration (make -f Makefile-amiga CONFIG=debug) with
# the project toolchain pod, boots it under the gdb-capable FS-UAE fork
# bundled with the vscode-amiga-debug extension, and attaches the pod's
# m68k-amiga-elf-gdb interactively. The emulator halts at the program entry
# point; use gdb as usual (b <symbol>, c, bt, ...). Quitting gdb ends the
# session and shuts the emulator down.
#
# Usage:
#   scripts/amiga-debug.sh [GAMEDIR]             mount GAMEDIR as DH1: and run
#                                                the player from it (default:
#                                                games/original256/release/amiga-ham)
#   scripts/amiga-debug.sh --adf DISK1 [DISK2]   boot from floppy images
#                                                instead (DISK2 goes to DF1:).
#                                                Master debug disks with e.g.:
#                                                make -C games/original256 amiga \
#                                                  AMIGA_PLAYER=$PWD/out/amiga/adp-debug.exe
#
# Environment overrides:
#   ADP_UAE        emulator binary (default: the newest installed
#                  bartmanabyss.amiga-debug extension's fs-uae)
#   ADP_GDB        gdb command to run instead of the pod's m68k-amiga-elf-gdb
#   ADP_KICKSTART  kickstart ROM (default: ~/Documents/FS-UAE/Kickstarts/kick13.rom)
#   ADP_MODEL      fs-uae amiga_model (default: A500)
#   ADP_GDB_PORT   gdbserver TCP port (default: 2345)
#   ADP_SERIAL=1   expose the emulated serial port on TCP 51299; capture the
#                  player's DebugPrintf output with:
#                    stdbuf -o0 nc 127.0.0.1 51299
#   ADP_NO_BUILD=1 skip the build step
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PORT=${ADP_GDB_PORT:-2345}
MODEL=${ADP_MODEL:-A500}
KICK=${ADP_KICKSTART:-$HOME/Documents/FS-UAE/Kickstarts/kick13.rom}
ELF=$ROOT/out/amiga/adp-debug.elf
EXE=$ROOT/out/amiga/adp-debug.exe

# Locate the gdb-capable FS-UAE fork (standard fs-uae has no gdb stub).
EXTBIN=$(ls -d "$HOME"/.vscode/extensions/bartmanabyss.amiga-debug-*/bin/linux 2>/dev/null | sort -V | tail -1 || true)
UAE=${ADP_UAE:-$EXTBIN/fs-uae/fs-uae}
if [ ! -x "$UAE" ]; then
    echo "error: gdb-capable fs-uae not found (looked for the vscode-amiga-debug" >&2
    echo "extension under ~/.vscode/extensions; set ADP_UAE to override)" >&2
    exit 1
fi
if [ ! -f "$KICK" ]; then
    echo "error: kickstart ROM not found at $KICK (set ADP_KICKSTART)" >&2
    exit 1
fi

# Parse arguments.
MODE=dir
GAMEDIR=$ROOT/games/original256/release/amiga-ham
DISK1= ; DISK2=
if [ "${1:-}" = "--adf" ]; then
    MODE=adf
    DISK1=${2:?usage: amiga-debug.sh --adf DISK1 [DISK2]}
    DISK2=${3:-}
elif [ -n "${1:-}" ]; then
    GAMEDIR=$(cd "$1" && pwd)
fi

# Build the debug configuration with the pod toolchain.
if [ "${ADP_NO_BUILD:-0}" != "1" ]; then
    if command -v amiga-toolchain >/dev/null 2>&1; then
        (cd "$ROOT" && amiga-toolchain make -f Makefile-amiga -j8 CONFIG=debug)
    else
        echo "warning: amiga-toolchain not found; skipping build" >&2
    fi
fi
if [ ! -f "$ELF" ]; then
    echo "error: $ELF not found (build with: amiga-toolchain make -f Makefile-amiga CONFIG=debug)" >&2
    exit 1
fi

EMUARGS=(
    --amiga_model="$MODEL"
    --kickstart_file="$KICK"
    --remote_debugger=200
    --remote_debugger_port="$PORT"
    --floppy_drive_speed=0
    --automatic_input_grab=0
)

if [ "$MODE" = dir ]; then
    # Boot environment: DH0: carries a minimal CLI (from the extension's
    # template) whose startup-sequence runs the player from DH1: (the game
    # data directory). The debugger halts when the player's executable loads.
    STAGE=$ROOT/out/amiga/debug-dh0
    rm -rf "$STAGE"
    cp -r "$EXTBIN/../dh0" "$STAGE"
    printf 'cd dh1:\n:adp-debug.exe\n' > "$STAGE/s/startup-sequence"
    cp "$EXE" "$GAMEDIR/adp-debug.exe"
    EMUARGS+=(
        --hard_drive_0="$STAGE"
        --hard_drive_1="$GAMEDIR"
        --remote_debugger_trigger=:adp-debug.exe
    )
    echo "Game directory: $GAMEDIR (mounted as DH1:)"
else
    EMUARGS+=(
        --floppy_drive_0="$DISK1"
        --remote_debugger_trigger=ADP.EXE
        # Floppy-only configs never map the UAE boot ROM (directory hard
        # drives do it implicitly), and without it the 0xf0ff60 debug trap
        # does not exist, so KPrintF output would silently vanish.
        --uae_boot_rom_uae=min
    )
    [ -n "$DISK2" ] && EMUARGS+=(--floppy_drive_1="$DISK2")
    echo "Booting floppy: $DISK1 ${DISK2:+(+ $DISK2 in DF1:)}"
fi

if [ "${ADP_SERIAL:-0}" = "1" ]; then
    EMUARGS+=(--serial_port=tcp://127.0.0.1:51299)
    echo "Serial DebugPrintf capture: stdbuf -o0 nc 127.0.0.1 51299"
fi

"$UAE" "${EMUARGS[@]}" >"$ROOT/out/amiga/debug-emulator.log" 2>&1 &
UAEPID=$!
trap 'kill $UAEPID 2>/dev/null || true' EXIT
echo "Emulator started (pid $UAEPID, log: out/amiga/debug-emulator.log)"

# Give the gdbserver a moment to come up, then attach.
for i in $(seq 1 20); do
    if ! kill -0 $UAEPID 2>/dev/null; then
        echo "error: emulator exited early; see out/amiga/debug-emulator.log" >&2
        exit 1
    fi
    (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break
    sleep 0.5
done

GDB_ARGS=("$ELF" -ex 'set pagination off' -ex "target remote 127.0.0.1:$PORT")
if [ -n "${ADP_GDB:-}" ]; then
    exec_gdb() { "$ADP_GDB" "${GDB_ARGS[@]}"; }
else
    # Run the pod's m68k-amiga-elf-gdb with host networking so it can reach
    # the emulator's gdbserver port.
    IMAGE=${AMIGA_TOOLCHAIN_IMAGE:-amiga-toolchain:1.8.2}
    TTYFLAG=-i; [ -t 0 ] && TTYFLAG=-it
    MOUNTSUFFIX=""
    if command -v getenforce >/dev/null 2>&1 && [ "$(getenforce 2>/dev/null || true)" != "Disabled" ]; then
        MOUNTSUFFIX=":Z"
    fi
    exec_gdb() {
        podman run --rm $TTYFLAG --userns keep-id --network host \
            -v "$ROOT:$ROOT$MOUNTSUFFIX" -w "$ROOT" "$IMAGE" \
            m68k-amiga-elf-gdb "${GDB_ARGS[@]}"
    }
fi
exec_gdb
