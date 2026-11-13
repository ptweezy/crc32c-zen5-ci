#!/bin/bash
# Benchmark the kernel's current code (k-today) against the new routine
# (k-mixed) at every length in a range, and summarise new/current.
#
# usage: sweep.sh [lo] [hi] [step] [extra bench options, e.g. -i or "-o 4"]
lo=${1:-512}
hi=${2:-4608}
step=${3:-8}
shift 3 2>/dev/null
here=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp)
trap 'rm -f "$out"' EXIT

args=()
n=0
for len in $(seq "$lo" "$step" "$hi"); do
  args+=(-l "$len")
  n=$((n + 1))
  if [ "$n" = 60 ]; then
    taskset -c 2 "$here/bench" "$@" -f k-today -f k-mixed "${args[@]}" | grep -E '^ +[0-9]+ ' >> "$out"
    args=(); n=0
  fi
done
[ "$n" -gt 0 ] && taskset -c 2 "$here/bench" "$@" -f k-today -f k-mixed "${args[@]}" | grep -E '^ +[0-9]+ ' >> "$out"

echo "lengths $lo..$hi step $step, options: ${*:-chained, aligned}"
awk '{ r = $3 / $2; print r, $1, $2, $3 }' "$out" | sort -n > "$out.sorted"
awk '{ a[NR] = $1; len[NR] = $2; t[NR] = $3; m[NR] = $4 }
     END {
       printf "  lengths measured: %d\n", NR
       printf "  new/current: min %.2f (len %d: %.1f vs %.1f GB/s), 5th pct %.2f, median %.2f, max %.2f (len %d)\n",
              a[1], len[1], m[1], t[1], a[int(NR * 0.05) + 1], a[int((NR + 1) / 2)], a[NR], len[NR]
       slower = 0
       for (i = 1; i <= NR; i++) if (a[i] < 0.98) slower++
       printf "  lengths where the new routine is more than 2%% slower: %d\n", slower
       if (slower) { printf "  worst five:"; for (i = 1; i <= 5 && i <= NR; i++) printf " %d (%.2f)", len[i], a[i]; printf "\n" }
     }' "$out.sorted"
rm -f "$out.sorted"
