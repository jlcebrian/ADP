#!/usr/bin/env bash
set -euo pipefail

# Install a consistent CrossMiNT toolchain snapshot on Linux by:
# 1) selecting latest matching RPMs from tho-otto indexes
# 2) downloading them
# 3) extracting them into /opt/cross-mint (default)

X86_URL="https://tho-otto.de/download/rpm/RPMS/x86_64/index.php"
NOARCH_URL="https://tho-otto.de/download/rpm/RPMS/noarch/index.php"
X86_BASE="https://tho-otto.de/download/rpm/RPMS/x86_64"
NOARCH_BASE="https://tho-otto.de/download/rpm/RPMS/noarch"

PREFIX="/opt/cross-mint"
WORKDIR="${TMPDIR:-/tmp}/crossmint-rpms.$$"
GCC_MAJOR=""
NO_INSTALL=0
KEEP_WORKDIR=0

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --prefix <path>      Install prefix (default: /opt/cross-mint)
  --gcc-major <N>      Force gcc major (e.g. 13, 14). Default: latest common.
  --no-install         Only download/select packages, do not extract.
  --keep-workdir       Keep download directory after completion.
  -h, --help           Show this help.

Examples:
  $0
  $0 --gcc-major 14
  $0 --prefix /opt/cross-mint --keep-workdir
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      PREFIX="${2:?missing value for --prefix}"
      shift 2
      ;;
    --gcc-major)
      GCC_MAJOR="${2:?missing value for --gcc-major}"
      shift 2
      ;;
    --no-install)
      NO_INSTALL=1
      shift
      ;;
    --keep-workdir)
      KEEP_WORKDIR=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing required command: $1" >&2
    exit 1
  }
}

need_cmd curl
need_cmd grep
need_cmd sed
need_cmd sort
need_cmd rpm2cpio
need_cmd cpio

mkdir -p "$WORKDIR"
trap '[[ $KEEP_WORKDIR -eq 1 ]] || rm -rf "$WORKDIR"' EXIT

echo "Fetching package indexes..."
curl -fsSL "$X86_URL" -o "$WORKDIR/x86.html"
curl -fsSL "$NOARCH_URL" -o "$WORKDIR/noarch.html"

extract_pkgs() {
  local file="$1"
  grep -Eo 'cross-mint-[^"<> ]+\.rpm' "$file" | sort -u
}

X86_PKGS="$(extract_pkgs "$WORKDIR/x86.html")"
NOARCH_PKGS="$(extract_pkgs "$WORKDIR/noarch.html")"

select_latest() {
  local pattern="$1"
  local list="$2"
  printf '%s\n' "$list" | grep -E "$pattern" | sort -V | tail -n1
}

if [[ -z "$GCC_MAJOR" ]]; then
  mapfile -t gcc_c < <(printf '%s\n' "$X86_PKGS" | sed -nE 's/^cross-mint-gcc([0-9]+)-[0-9].*\.x86_64\.rpm$/\1/p' | sort -Vu)
  mapfile -t gcc_cpp < <(printf '%s\n' "$X86_PKGS" | sed -nE 's/^cross-mint-gcc([0-9]+)-c\+\+-.*\.x86_64\.rpm$/\1/p' | sort -Vu)
  for n in "${gcc_c[@]}"; do
    if printf '%s\n' "${gcc_cpp[@]}" | grep -qx "$n"; then
      GCC_MAJOR="$n"
    fi
  done
fi

if [[ -z "$GCC_MAJOR" ]]; then
  echo "Could not determine a matching gcc/g++ major version from index." >&2
  exit 1
fi

echo "Using gcc major: $GCC_MAJOR"

BINUTILS_RPM="$(select_latest '^cross-mint-binutils-.*\.x86_64\.rpm$' "$X86_PKGS")"
GCC_RPM="$(select_latest "^cross-mint-gcc${GCC_MAJOR}-[0-9].*\.x86_64\.rpm$" "$X86_PKGS")"
GXX_RPM="$(select_latest "^cross-mint-gcc${GCC_MAJOR}-c\\+\\+-.*\.x86_64\.rpm$" "$X86_PKGS")"
FDLIBM_RPM="$(select_latest '^cross-mint-fdlibm-.*\.noarch\.rpm$' "$NOARCH_PKGS")"
GEMLIB_RPM="$(select_latest '^cross-mint-gemlib-.*\.noarch\.rpm$' "$NOARCH_PKGS")"
MINTLIB_RPM="$(select_latest '^cross-mint-mintlib-[0-9].*\.noarch\.rpm$' "$NOARCH_PKGS")"
MINTLIB_HEADERS_RPM="$(select_latest '^cross-mint-mintlib-headers-.*\.noarch\.rpm$' "$NOARCH_PKGS")"

required=(
  "$BINUTILS_RPM"
  "$GCC_RPM"
  "$GXX_RPM"
  "$FDLIBM_RPM"
  "$GEMLIB_RPM"
  "$MINTLIB_RPM"
  "$MINTLIB_HEADERS_RPM"
)

for rpm in "${required[@]}"; do
  if [[ -z "$rpm" ]]; then
    echo "Failed to resolve one or more required RPMs from the index." >&2
    exit 1
  fi
done

echo "Selected packages:"
printf '  %s\n' "${required[@]}"

cd "$WORKDIR"
for rpm in "${required[@]}"; do
  case "$rpm" in
    *.x86_64.rpm) base="$X86_BASE" ;;
    *.noarch.rpm) base="$NOARCH_BASE" ;;
    *) echo "Unknown rpm arch in filename: $rpm" >&2; exit 1 ;;
  esac
  echo "Downloading $rpm"
  curl -fL --retry 3 --retry-delay 2 -o "$rpm" "$base/$rpm"
done

if [[ "$NO_INSTALL" -eq 1 ]]; then
  echo "Download complete in $WORKDIR (install skipped by --no-install)."
  exit 0
fi

echo "Extracting RPMs into $PREFIX (sudo may prompt)..."
sudo mkdir -p "$PREFIX"
for rpm in "${required[@]}"; do
  echo "Installing $rpm"
  rpm2cpio "$rpm" | sudo cpio -idmu -D "$PREFIX" --quiet
done

echo "Normalizing sysroot header layout..."
TARGET_ROOT="$PREFIX/usr/m68k-atari-mint"
SYS_INCLUDE="$TARGET_ROOT/sys-include"
SYSROOT_INCLUDE="$TARGET_ROOT/sys-root/usr/include"

sudo mkdir -p "$SYSROOT_INCLUDE"
if [[ -d "$SYS_INCLUDE" ]]; then
  while IFS= read -r -d '' src; do
    rel="${src#$SYS_INCLUDE/}"
    dst="$SYSROOT_INCLUDE/$rel"
    sudo mkdir -p "$(dirname "$dst")"
    if [[ ! -e "$dst" ]]; then
      sudo ln -s "$src" "$dst"
    fi
  done < <(find "$SYS_INCLUDE" -type f -print0)
fi

if [[ ! -f "$SYSROOT_INCLUDE/gem.h" ]]; then
  echo "warning: gem.h still missing from $SYSROOT_INCLUDE" >&2
  echo "warning: check downloaded gemlib RPM version set" >&2
fi

echo "Normalizing GEM library layout..."
SYSROOT_LIB="$TARGET_ROOT/sys-root/usr/lib"
sudo mkdir -p "$SYSROOT_LIB"

if [[ ! -f "$SYSROOT_LIB/libgem.a" ]]; then
  # Some CrossMiNT sets place libgem.a in legacy gcc-lib paths.
  LEGACY_GEM="$(find "$PREFIX/usr/lib/gcc-lib/m68k-atari-mint" -type f -name libgem.a 2>/dev/null | sort -V | head -n1 || true)"
  if [[ -n "$LEGACY_GEM" ]]; then
    sudo ln -s "$LEGACY_GEM" "$SYSROOT_LIB/libgem.a"
  fi
fi

if [[ ! -f "$SYSROOT_LIB/libgem.a" ]]; then
  echo "warning: libgem.a still missing from $SYSROOT_LIB" >&2
  echo "warning: install the CrossMiNT package that provides libgem.a for your target" >&2
fi

cat <<EOF
Done.

Add to your PATH (bash):
  export PATH="$PREFIX/usr/bin:\$PATH"

To persist:
  echo 'export PATH="$PREFIX/usr/bin:\$PATH"' >> ~/.bashrc
EOF
