#!/bin/bash
# Boot the kernel built in $KDIR several times under virtme-ng and summarise the
# in-kernel CRC KUnit benchmark as min / median / max MB/s per variant and length.
#
# The kernel must be built with the options in crc-kunit.config, e.g.:
#   cd $KDIR && ./scripts/kconfig/merge_config.sh -m .config <this dir>/crc-kunit.config
#   make olddefconfig && make -j$(nproc)
#
# usage: kunit-crcbench.sh <label> [runs] [variant-regex] [extra kernel cmdline]
#
# Under WSL2 pass mitigations=off as the 4th argument: WSL makes the kernel
# apply Safe RET and retpolines, which add roughly 48 ns to every kernel-FPU
# call on this machine and badly distort short-length results.
KDIR=${KDIR:-$HOME/linux}
label=${1:-run}
runs=${2:-5}
filter=${3:-crc32c|crc32_le|crc64_nvme|crc_t10dif}
cmdline=$4

cd "$KDIR" || exit 1
out=/tmp/crcbench-$label
rm -rf "$out"; mkdir -p "$out"
echo "tree: $(git describe --always --dirty) ($(git rev-parse --abbrev-ref HEAD))  cmdline: ${cmdline:-default}"
for i in $(seq 1 "$runs"); do
  # vng reads stdin, so keep it away from ours.
  if [ -n "$cmdline" ]; then
    timeout 300 vng -a "$cmdline" -- dmesg > "$out/dmesg-$i.txt" 2>&1 < /dev/null
  else
    timeout 300 vng -- dmesg > "$out/dmesg-$i.txt" 2>&1 < /dev/null
  fi
  echo "boot $i: all-16-tests-pass=$(grep -c '# crc: pass:16 fail:0' "$out/dmesg-$i.txt")"
done
grep -hE '_benchmark: len=' "$out"/dmesg-*.txt \
  | sed -E 's/^\[ *[0-9.]+\] +# ([a-z0-9_]+)_benchmark: len=([0-9]+): ([0-9]+) MB\/s.*/\1 \2 \3/' \
  | grep -E "^($filter) " \
  | sort -k1,1 -k2,2n -k3,3n \
  | awk '
    function flush() {
      if (n) printf "%-12s %6d  %7d %7d %7d\n", v, l, a[1], a[int((n+1)/2)], a[n]
      n = 0
    }
    { if ($1 != v || $2 != l) { flush(); v = $1; l = $2 } a[++n] = $3 }
    BEGIN { printf "%-12s %6s  %7s %7s %7s\n", "variant", "len", "min", "median", "max" }
    END { flush() }'
