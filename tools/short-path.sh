#!/bin/bash
# Time the kernel's current short CRC32C path against the proposed one (see
# short.h) with fixed lengths, random lengths, and the lengths of real file
# names.  File names are what btrfs passes to crc32c() on every directory
# operation; they are taken from a source tree in the order a directory walk
# visits them, and again shuffled.
#
# usage: short-path.sh [directory-to-take-names-from]
here=$(cd "$(dirname "$0")/.." && pwd)
tree=${1:-$HOME/linux}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

make -s -C "$here" shortbench || exit 1

find "$tree" -path "$tree/.git" -prune -o -printf '%f\n' 2>/dev/null \
  | awk '{ print length($0) }' | grep -v '^0$' > "$work/walk.txt"
shuf --random-source=<(yes) "$work/walk.txt" > "$work/shuffled.txt"

echo "file names: $(wc -l < "$work/walk.txt") from $tree"
awk '{ n++; s += $1; h[$1 % 8]++; if ($1 < 8) small++ }
     END { printf "  mean length %.1f, shorter than 8 bytes: %.0f%%, len %% 8:", s / n, 100 * small / n
           for (k = 0; k < 8; k++) printf " %d=%.0f%%", k, 100 * h[k] / n; printf "\n" }' "$work/walk.txt"
echo "first -L row is directory-walk order, second is shuffled"

args="-L $work/walk.txt -L $work/shuffled.txt
      -r 8,40 -r 1,40 -r 1,255 -r 8,511 -r 8,504,8 -r 16,64,4
      -l 4 -l 8 -l 12 -l 15 -l 16 -l 20 -l 31 -l 48 -l 64 -l 100 -l 124 -l 127 -l 128 -l 255 -l 256 -l 511"
if command -v taskset > /dev/null; then pin="taskset -c 2"; else pin=; fi
$pin "$here/shortbench" $args -i $args
