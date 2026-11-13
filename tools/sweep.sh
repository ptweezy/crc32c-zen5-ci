#!/bin/bash
# Benchmark the kernel's current code (k-today) against the new routine
# (k-mixed) at every length in a range, and summarise new/current.
#
# A single measurement can be ruined by whatever else the machine is doing, so
# any length where the new routine doesn't come out at least 2% ahead is
# measured three more times, and the best result for each side is kept.
#
# usage: sweep.sh [lo] [hi] [step] [extra bench options, e.g. -i or "-o 4"]
lo=${1:-512}
hi=${2:-4608}
step=${3:-8}
shift 3 2>/dev/null
here=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp)
trap 'rm -f "$out" "$out".*' EXIT

# Print "len current new" for each given length.
measure() {
  local args=() n=0 len
  for len in "$@"; do
    args+=(-l "$len"); n=$((n + 1))
    if [ "$n" = 60 ]; then
      taskset -c 2 "$here/bench" "${opts[@]}" -f k-today -f k-mixed "${args[@]}" | grep -E '^ +[0-9]+ '
      args=(); n=0
    fi
  done
  [ "$n" -gt 0 ] && taskset -c 2 "$here/bench" "${opts[@]}" -f k-today -f k-mixed "${args[@]}" | grep -E '^ +[0-9]+ '
}

opts=("$@")
measure $(seq "$lo" "$step" "$hi") > "$out"

doubtful=$(awk '$3 / $2 < 1.02 { print $1 }' "$out")
ndoubt=$(echo $doubtful | wc -w)
if [ "$ndoubt" -gt 0 ]; then
  for rep in 1 2 3; do measure $doubtful >> "$out.again"; done
  # Keep the best figure seen for each side at each length.
  cat "$out" "$out.again" \
    | awk '{ if ($2 > t[$1]) t[$1] = $2; if ($3 > m[$1]) m[$1] = $3 }
           END { for (l in t) print l, t[l], m[l] }' > "$out.best"
  mv "$out.best" "$out"
fi

echo "lengths $lo..$hi step $step, options: ${*:-chained, aligned}"
awk '{ print $3 / $2, $1, $2, $3 }' "$out" | sort -n > "$out.sorted"
awk -v nd="$ndoubt" '{ a[NR] = $1; len[NR] = $2; t[NR] = $3; m[NR] = $4 }
     END {
       printf "  lengths measured: %d (%d re-measured)\n", NR, nd
       printf "  new/current: min %.2f (len %d: %.1f vs %.1f GB/s), 5th pct %.2f, median %.2f, max %.2f (len %d)\n",
              a[1], len[1], m[1], t[1], a[int(NR * 0.05) + 1], a[int((NR + 1) / 2)], a[NR], len[NR]
       slower = 0
       for (i = 1; i <= NR; i++) if (a[i] < 1.0) slower++
       printf "  lengths where the new routine is slower: %d\n", slower
       printf "  five smallest ratios:"; for (i = 1; i <= 5 && i <= NR; i++) printf " %d (%.2f)", len[i], a[i]; printf "\n"
     }' "$out.sorted"
