#!/bin/bash
# A/B the in-kernel CRC KUnit benchmark between two kernel build directories,
# alternating boots so that drift over time affects both equally.
#
# usage: kunit-ab.sh <build-dir-A> <build-dir-B> [boots-each] [variant-regex] [kernel cmdline]
A=$1
B=$2
boots=${3:-6}
filter=${4:-crc32c|crc32_le}
cmdline=${5:-mitigations=off}
out=/tmp/kunit-ab
rm -rf "$out"; mkdir -p "$out"

for i in $(seq 1 "$boots"); do
  for side in A B; do
    dir=$A; [ "$side" = B ] && dir=$B
    (cd "$dir" && timeout 300 vng -r "$dir/arch/x86/boot/bzImage" -a "$cmdline" -- dmesg \
        > "$out/$side-$i.txt" 2>&1 < /dev/null)
    grep -q '# crc: pass:16 fail:0' "$out/$side-$i.txt" || echo "boot $side-$i: tests did not all pass"
  done
done

# Print "variant length-or-range median best" per benchmark line.  Lengths are
# either a number or a range like 1..32; ranges sort after the fixed lengths.
summarise() {
  grep -hE '_benchmark: len=' "$out"/"$1"-*.txt \
    | sed -E 's/^\[ *[0-9.]+\] +# ([a-z0-9_]+)_benchmark: len=([0-9.]+): ([0-9]+) MB\/s.*/\1 \2 \3/' \
    | grep -E "^($filter) " \
    | awk '{ key = $2; if (index($2, "..")) { split($2, r, "."); key = 1000000000 + r[3] } print $1, key, $2, $3 }' \
    | sort -k1,1 -k2,2n -k4,4n \
    | awk '{ k = $1 " " $2 " " $3; if (k != p) { if (n) print p, a[int((n+1)/2)], a[n]; n = 0; p = k } a[++n] = $4 }
           END { if (n) print p, a[int((n+1)/2)], a[n] }'
}

echo "A = $A"
echo "B = $B"
echo "boots each: $boots, cmdline: $cmdline"
# Each length is timed for well under a millisecond per boot, so a single
# interruption ruins a sample.  The best of several boots is the estimate of
# what the code can do; the medians show how noisy the boots were.
printf '%-10s %8s  %9s %9s %7s   %9s %9s\n' variant len A-best B-best change A-median B-median
join -j1 <(summarise A | awk '{ print $1 "/" $2 "/" $3, $4, $5 }' | sort) \
         <(summarise B | awk '{ print $1 "/" $2 "/" $3, $4, $5 }' | sort) \
  | tr '/' ' ' | sort -k1,1 -k2,2n \
  | awk '{ printf "%-10s %8s  %9d %9d %+6.1f%%   %9d %9d\n", $1, $3, $5, $7, 100.0 * $7 / $5 - 100, $4, $6 }'
