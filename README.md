# Mixed crc32q + VPCLMULQDQ CRC32C for Zen 5 — userspace prototype

A design experiment for the "future work" noted in the kernel's
`lib/crc/x86/crc32.h`: on AMD Zen 3–5 the fastest CRC32C runs `crc32q` chains
and VPCLMULQDQ folding at the same time. This prototype measures how much that
buys on a Ryzen 7 9800X3D (Zen 5) and which shape of it works best.

It is not kernel code. It uses compiler intrinsics and builds its constants at
startup. Its job is to produce numbers and a design before any kernel assembly
is written.

## Result

Throughput in GB/s, chained calls (the method `crc_kunit` uses), 64-byte
aligned buffer, gcc 13.3, measured 2026-11-12 under WSL2.

| Length | Kernel today | Mixed (`v2s4r3d`) | Change |
|-------:|-------------:|------------------:|-------:|
| 512    | 38.9  | 51.3  | +32% |
| 1024   | 72.2  | 74.7  | +3%  |
| 2048   | 76.5  | 99.4  | +30% |
| 4096   | 79.7  | 114.6 | +44% |
| 8192   | 81.0  | 120.1 | +48% |
| 16384  | 82.0  | 125.9 | +54% |
| 65536  | 82.2  | 123.2 | +50% |
| 1 MiB  | 82.9  | 116.7 | +41% |

"Kernel today" is the kernel's own `crc32_lsb_vpclmul_avx512()` from
`crc-pclmul-template.S`, assembled unmodified from the kernel tree and called
the way `crc32c_arch()` calls it on this CPU (512 bytes and up).

Other measurements:

- **clang 18** generates faster code for the prototype: 121.5 GB/s at 4096,
  82.7 at 1024, 59.6 at 512.
- **Misaligned buffer** (offset 4): 109.0 vs 80.2 at 4096.
- **Lengths near 4 KiB** (3800 to 4160, including 4092 and 4095): 110 to 116,
  no slow lengths.
- **Independent calls** instead of chained: same picture from 1024 up. At 512
  the kernel code is ahead, 63.4 vs 51.0.
- **Chunk limit**: capping chunks at 4096 bytes gives 108 GB/s at 1 MiB instead
  of 117, and shrinks the constants table to about 1 KiB.
- **No vector lanes at all** (`v0s6r1d`, six `crc32q` chains merged with
  PCLMULQDQ): 75.8 GB/s at 4096, about twice the kernel's `crc32c_x86_3way()`
  at 38.7.

## Kernel assembly version

`lib/crc/x86/crc32c-mixed.S`, on branch `b4/crc32c-mixed` of the kernel tree,
is the hand-written version of the best shape found here. When that file
exists, `make` in this folder also builds it as `k-mixed`, through C glue that
mirrors `crc32c_mixed()` in the kernel's `lib/crc/x86/crc32.h`, so the same
test and benchmark cover it.

In the kernel itself (`crc_kunit` benchmark, median of 5 boots under
virtme-ng with `mitigations=off`, MB/s):

| Length | crc-next | With the patch | Change |
|-------:|---------:|---------------:|-------:|
| 512    | 38499 | 40953  | +6%  |
| 1024   | 47788 | 65191  | +36% |
| 3173   | 69128 | 101875 | +47% |
| 4096   | 73004 | 109858 | +50% |
| 16384  | 74426 | 121013 | +63% |

`make ALT_S=path/to/other.S` adds a second copy of the assembly as `k-alt`
for side-by-side comparison. Run `make clean` when adding or dropping it.

## Design

One chunk is laid out as

    [ V ][ lane 0 ][ lane 1 ] ... [ lane S-1 ][ F: 16 bytes ][ tail: len % 8 ]

- **V** is folded by K zmm accumulators, K 64-byte blocks per loop iteration.
- Each **lane** is one `crc32q` dependency chain, R qwords per iteration.
- Lane 0 takes the leftover qwords; its length only affects V's constants.
- At the end, V's 4·K folding states and the CRCs of lanes 0..S-2 are each
  multiplied by the power of x that moves them to F, XORed into F, and the
  last lane's chain consumes F.

The best shape found is K=2, S=4, R=3: 224 bytes per iteration.

Every constant has the form g(d) = x^(64·d + 31) mod P for a qword distance d,
so one table serves the loop, the reduction and the merge. Stored in
descending order, the eight entries one accumulator needs for the merge are
consecutive, so they load with a single zero-extending load ("direct" merge,
the `d` suffix). That removes one carryless-multiply stage from the end of
every call and is worth 6–9% at 4096 with gcc.

## What this does not show

- **One CPU only.** Nothing here says how the same code behaves on Zen 4,
  Zen 3 or Intel. The kernel maintainer's stated concern is exactly that each
  microarchitecture wants different lane counts.
- **Userspace only.** The kernel pays for a kernel-FPU section around the
  vector code. Both sides of the comparison use zmm registers, so that cost
  should be the same for both, but it is not measured here.
- **Not assembly.** A kernel version would be hand-written assembly with
  build-time constants. clang beating gcc by 6% suggests there is room left.
- **Measured in a VM** (WSL2). Baseline and prototype ran in the same
  environment; absolute numbers on bare metal may differ.
- **Run-to-run noise.** Absolute figures moved by up to about 7% between runs
  (a later run gave 73.9 vs 106.5 at 4096). The ratio between kernel code and
  prototype stayed put.

## Files

| File | Purpose |
|---|---|
| `crc32c_mixed.h` | The prototype, parameterised by K, S, R and merge style |
| `crc32c_mixed.c` | Startup generation of the constants |
| `variants.h` | Which (K, S, R) combinations get built |
| `impls.c` | Kernel baselines and prototype variants behind one interface |
| `test.c` | Correctness test against a bit-at-a-time reference |
| `bench.c` | Throughput benchmark |
| `shim/` | Stand-in headers so the kernel's `.S` files assemble in userspace |
| `tools/kunit-crcbench.sh` | Runs the in-kernel `crc_kunit` benchmark under virtme-ng |
| `tools/crc-kunit.config` | Kernel config fragment that enables that benchmark |

## Use

    make                 # needs the kernel tree at ~/linux, or pass KDIR=
    ./test               # every variant, 13001 inputs per chunk size
    taskset -c 2 ./bench -f k-today -f v2s4r3d
    ./bench -i -o 4 -m 4096 -l 4096   # independent calls, offset, chunk limit, length

Variant names read `v<K>s<S>r<R>`, with a trailing `d` for the direct merge.
`k-today`, `k-avx512`, `k-3way` and `one-chain` are the kernel's code paths.
