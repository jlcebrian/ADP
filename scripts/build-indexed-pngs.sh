#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/build-indexed-pngs.sh [16|32] [source_dir] [output_dir]
  scripts/build-indexed-pngs.sh both [source_dir] [output_dir_16] [output_dir_32]

Defaults:
  mode       both
  source_dir .
  output_dir  ./<mode>

Examples:
  scripts/build-indexed-pngs.sh
  scripts/build-indexed-pngs.sh 32 ./VGA ./PNG32
  scripts/build-indexed-pngs.sh both ./assets/vga ./assets/16 ./assets/32
EOF
}

mode="${1:-both}"
source_dir="${2:-.}"
output_dir_16="${3:-}"
output_dir_32="${4:-}"

case "$mode" in
  16|32|both) ;;
  -h|--help|help)
    usage
    exit 0
    ;;
  *)
    echo "Error: invalid mode \"$mode\"" >&2
    usage >&2
    exit 1
    ;;
esac

if [ "$mode" = "16" ]; then
  output_dir_16="${output_dir_16:-./16}"
fi

if [ "$mode" = "32" ]; then
  output_dir_32="${output_dir_16:-./32}"
fi

if [ "$mode" = "both" ]; then
  output_dir_16="${output_dir_16:-./16}"
  output_dir_32="${output_dir_32:-./32}"
fi

if ! command -v magick >/dev/null 2>&1; then
  echo "Error: ImageMagick 'magick' command not found" >&2
  exit 1
fi

if [ ! -d "$source_dir" ]; then
  echo "Error: source directory not found: $source_dir" >&2
  exit 1
fi

build_set() {
  local colors="$1"
  local dest_dir="$2"
  local pal_file
  local found=0

  mkdir -p "$dest_dir"
  pal_file="$(mktemp /tmp/adp-pal-XXXXXX.png)"
  trap 'rm -f "$pal_file"' RETURN

  for f in "$source_dir"/*; do
    [ -e "$f" ] || continue
    case "${f##*.}" in
      VGA|vga|PCX|pcx|PNG|png) ;;
      *) continue ;;
    esac
    found=1
    local base
    base="$(basename "$f")"
    base="${base%.*}"
    magick "$f" -alpha off +dither -colors "$colors" -unique-colors "$pal_file"
    magick "$f" -alpha off +dither -remap "$pal_file" "PNG8:$dest_dir/$base.png"
    echo "Wrote $dest_dir/$base.png"
  done

  if [ "$found" -eq 0 ]; then
    echo "Error: no supported image files found in $source_dir" >&2
    exit 1
  fi

  rm -f "$pal_file"
  trap - RETURN
}

case "$mode" in
  16)
    build_set 16 "$output_dir_16"
    ;;
  32)
    build_set 32 "$output_dir_32"
    ;;
  both)
    build_set 16 "$output_dir_16"
    build_set 32 "$output_dir_32"
    ;;
esac
