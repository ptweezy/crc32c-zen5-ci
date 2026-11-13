# Mixed crc32q + VPCLMULQDQ CRC32C for Zen 5

Work toward a Linux kernel patch for the "future work" noted in
`lib/crc/x86/crc32.h`: on AMD Zen CPUs the fastest CRC32C runs `crc32q`
chains and VPCLMULQDQ folding at the same time.

This repository holds the userspace prototype that chose the design, and a
harness that tests and benchmarks the kernel's own assembly files unmodified.
The kernel changes are in `kernel/0002-crc32c-mixed.patch`, which applies on
top of the `crc-next` commit in `kernel/0001-crc-next-base.patch`, itself on
top of v7.4-rc2.

## Where things stand

- The kernel routine, `crc32c_mixed_avx512()`, is written and enabled on
  Zen 5 only.
- The series has not been sent to the mailing list yet.
- It was developed with an AI coding assistant (Claude), which wrote the
  prototype, the assembly, the harness and this README.

## Results

In the kernel (`crc_kunit` benchmark, MB/s, best of 8 alternating boots under
virtme-ng with `mitigations=off`, Ryzen 7 9800X3D):

| Length | crc-next | With the patch | Change |
|-------:|---------:|---------------:|-------:|
| 512    | 39437 | 42545  | +8%  |
| 1024   | 53646 | 67146  | +25% |
| 3173   | 69262 | 98280  | +42% |
| 4096   | 73542 | 112210 | +53% |
| 16384  | 80961 | 122101 | +51% |

In userspace, the same two assembly functions, GB/s at 4096 bytes:

| CPU | Existing AVX-512 | New | Change |
|---|---:|---:|---:|
| AMD Ryzen 7 9800X3D (Zen 5) | 79.3 | 123.3 | +55% |
| AMD EPYC 9V45 (Zen 5) | 65.6 | 103.0 | +57% |
| AMD EPYC 9V74 (Zen 4) | 53.5 | 61.4 | +15% |
| Intel Xeon 6975P-C (Granite Rapids) | 114.9 | 65.4 | −43% |
| Intel Xeon 8370C (Ice Lake) | 45.2 | 56.5 | +25% |

The cloud rows are single runs on GitHub's shared runners. They are why the
routine is limited to Zen 5.

On the 9800X3D, across every multiple of 8 from 512 to 4608 and a sample of
other lengths up to 16384, the new routine is faster at every length, by a
median of 50%. With a different random length on every call it is +52% for
multiples of 8 and +40% for arbitrary lengths.

## Design

One piece of up to 4096 bytes is laid out as

    [ V ][ lane 0 ][ lane 1 ][ lane 2 ][ lane 3 ][ F: 16 bytes ]

- **V** is folded by two zmm registers, 128 bytes per loop iteration.
- Each **lane** is one `crc32q` dependency chain, 3 qwords per iteration.
- Lane 0 also takes up to 3 leftover qwords at its front.
- At the end, V's eight folding states and the CRCs of lanes 0–2 are each
  multiplied by the power of x that moves them to F and added into F, and
  lane 3's chain finishes over F.

Every multiplier has the form M(d) = x^(64·d + 31) mod G for a qword
distance d, so one 1 KiB table serves the loop and the merge. Stored in
descending order, the eight entries one zmm register needs are adjacent and
load with a single zero-extending load.

A C helper splits longer inputs into pieces and folds in the first
`len % 8` bytes without branching on how many there are.

The prototype in `crc32c_mixed.h` is the same design with the stream counts
as parameters. It tried about twenty shapes; two zmm accumulators with four
chains at 3 qwords per iteration was the best at 4 KiB on Zen 5.

## What this does not show

- **No bare-metal numbers.** The machine runs Windows; everything here ran
  under WSL2, and the in-kernel numbers in a nested guest.
- **No mobile Zen 5 or Zen 5c.** Those have a 256-bit vector datapath and
  may want a different balance.
- **No real workload.** Only `crc_kunit` and microbenchmarks.

## Files

| Path | Purpose |
|---|---|
| `kernel/` | The kernel changes as patches against v7.4-rc2 |
| `crc32c_mixed.h`, `crc32c_mixed.c` | Parameterised userspace prototype |
| `variants.h` | Which prototype shapes get built |
| `impls.c` | Kernel baselines, the kernel routine, and prototype variants behind one interface |
| `test.c` | Correctness test against a bit-at-a-time reference |
| `bench.c` | Throughput benchmark: fixed or random lengths, chained or independent |
| `shim/` | Stand-in headers so the kernel's `.S` files assemble in userspace |
| `tools/verify-layout.py` | Checks the routine's arithmetic for every legal length |
| `tools/sweep.sh` | New versus current at every length in a range |
| `tools/kunit-ab.sh` | A/B of the in-kernel benchmark between two kernel builds |
| `tools/compare-codegen.sh` | Instruction-level diff of one function between two kernels |
| `tools/align-experiment.sh` | Tests whether the existing routine is alignment-sensitive |
| `tools/kunit-crcbench.sh`, `tools/crc-kunit.config` | In-kernel benchmark for one build |
| `.github/workflows/ci.yml` | Compiler matrix and CPU sampling on GitHub Actions |

## Use

    tools/fetch-kernel.sh      # v7.4-rc2 plus the patches, into ./linux
    make KDIR=$PWD/linux       # KDIR defaults to ~/linux
    ./test 30000               # every length to 30000, plus random longer ones
    taskset -c 2 ./bench -f k-today -f k-mixed
    ./bench -i -r 512,4096,1   # independent calls, random arbitrary lengths
    tools/sweep.sh 512 4608 8

`k-today` is what the kernel does now on a CPU with good AVX-512, `k-mixed`
is the new routine through glue equivalent to the kernel's, `k-3way` and
`k-avx512` are the kernel's other paths, and `v<K>s<S>r<R>` are prototype
shapes.
