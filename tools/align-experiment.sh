#!/bin/bash
# Does the speed of the kernel's existing crc32_lsb_vpclmul_avx512() depend on
# where the function happens to land relative to a 64-byte boundary?
#
# Assembles the kernel's file unmodified four times, with the functions placed
# at 0, 16, 32 and 48 bytes past a 64-byte boundary, and times each copy.
#
# usage: tools/align-experiment.sh [kernel-dir]
set -e
KDIR=${1:-$HOME/linux}
KX86=$KDIR/lib/crc/x86
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/shim/linux"
cat > "$work/shim/linux/linkage.h" <<'EOF'
#define SYM_FUNC_START(name) .globl name; .type name, @function; .balign 64; .skip FUNC_PAD, 0x90; name:
#define SYM_FUNC_END(name) .size name, . - name
#define RET ret
EOF
cp "$here/shim/linux/objtool.h" "$work/shim/linux/"

objs=
for pad in 0 16 32 48; do
  gcc -c -I"$work/shim" -I"$KX86" -D__ASSEMBLY__ -Wa,--noexecstack -DFUNC_PAD=$pad \
      -Dcrc32_lsb_pclmul_sse=p${pad}_sse -Dcrc32_lsb_vpclmul_avx2=p${pad}_avx2 \
      -Dcrc32_lsb_vpclmul_avx512=p${pad}_avx512 \
      -o "$work/p$pad.o" "$KX86/crc32-pclmul.S"
  objs="$objs $work/p$pad.o"
done

cat > "$work/main.c" <<'EOF'
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
typedef uint8_t u8; typedef uint64_t u64;
#define ____cacheline_aligned __attribute__((aligned(64)))
#define __maybe_unused __attribute__((unused))
#include "crc-pclmul-consts.h"
typedef uint32_t (*fn_t)(uint32_t, const uint8_t *, size_t, const void *);
uint32_t p0_avx512(uint32_t, const uint8_t *, size_t, const void *);
uint32_t p16_avx512(uint32_t, const uint8_t *, size_t, const void *);
uint32_t p32_avx512(uint32_t, const uint8_t *, size_t, const void *);
uint32_t p48_avx512(uint32_t, const uint8_t *, size_t, const void *);
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static volatile uint32_t sink;
static double gbs(fn_t f, const uint8_t *buf, size_t len, int independent)
{
	const void *c = crc32_lsb_0x82f63b78_consts.fold_across_128_bits_consts;
	long iters = 400000000 / (len + 64);
	double best = 1e30;
	for (int rep = 0; rep < 9; rep++) {
		uint32_t crc = 1, acc = 0;
		double t = now();
		for (long i = 0; i < iters; i++) {
			if (independent) acc += f(1, buf, len, c); else crc = f(crc, buf, len, c);
		}
		t = now() - t;
		sink = crc + acc;
		if (t < best) best = t;
	}
	return (double)len * iters / best / 1e9;
}
int main(void)
{
	static const size_t lens[] = { 256, 384, 448, 512, 576, 640, 768, 1024, 4096 };
	fn_t f[] = { p0_avx512, p16_avx512, p32_avx512, p48_avx512 };
	uint8_t *buf = aligned_alloc(4096, 1 << 16);
	for (int i = 0; i < (1 << 16); i++) buf[i] = (uint8_t)(i * 31 + 7);
	for (int ind = 0; ind < 2; ind++) {
		printf("%s calls, GB/s by function start address mod 64\n", ind ? "independent" : "chained");
		printf("%6s %8s %8s %8s %8s   %s\n", "len", "0", "16", "32", "48", "spread");
		for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); li++) {
			double v[4], lo = 1e30, hi = 0;
			for (int k = 0; k < 4; k++) {
				v[k] = gbs(f[k], buf, lens[li], ind);
				if (v[k] < lo) lo = v[k];
				if (v[k] > hi) hi = v[k];
			}
			printf("%6zu %8.1f %8.1f %8.1f %8.1f   %.0f%%\n", lens[li], v[0], v[1], v[2], v[3], 100 * (hi / lo - 1));
		}
	}
	return 0;
}
EOF
gcc -O2 -I"$KX86" -o "$work/align" "$work/main.c" $objs
nm "$work/align" | grep -E 'p[0-9]+_avx512' | awk '{ printf "%s at ...%s\n", $3, substr($1, 13) }'
taskset -c 2 "$work/align"
