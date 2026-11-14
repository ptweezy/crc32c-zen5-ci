// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Throughput benchmark, in GB/s (1e9 bytes/s).
 *
 * Default mode chains calls (crc = f(crc, buf, len)) the way the kernel's
 * crc_kunit benchmark does, so the whole latency of each call counts.
 * -i runs independent calls instead, letting the CPU overlap adjacent calls.
 *
 * usage: bench [-i] [-o offset] [-m chunk_max] [-f name-substring]... [-l len]...
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crc32c_mixed.h"
#include "impls.h"

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static volatile uint32_t sink;

static double run(crc_fn fn, const uint8_t *buf, size_t len, long iters, int independent)
{
	uint32_t crc = 0x12345678;
	double t = now();

	if (independent) {
		uint32_t acc = 0;

		for (long i = 0; i < iters; i++)
			acc += fn(0x12345678, buf, len);
		crc = acc;
	} else {
		for (long i = 0; i < iters; i++)
			crc = fn(crc, buf, len);
	}
	t = now() - t;
	sink = crc;
	return t;
}

/*
 * Random-length mode: every call gets a different length, so the
 * length-dependent branches inside each implementation can't be predicted
 * the way they are when one length is repeated.
 */
/*
 * Long enough that the branch predictor can't simply learn the sequence over
 * the repetitions.
 */
#define NUM_RANDOM_LENS 65536

static double run_random(crc_fn fn, const uint8_t *buf, const uint32_t *lens,
			 int independent)
{
	uint32_t crc = 0x12345678;
	uint32_t acc = 0;
	double t = now();

	for (int i = 0; i < NUM_RANDOM_LENS; i++) {
		if (independent)
			acc += fn(0x12345678, buf, lens[i]);
		else
			crc = fn(crc, buf, lens[i]);
	}
	t = now() - t;
	sink = crc + acc;
	return t;
}

static double measure_random(crc_fn fn, const uint8_t *buf, const uint32_t *lens,
			     int independent)
{
	double best = 1e30, total = 0;

	for (int i = 0; i < NUM_RANDOM_LENS; i++)
		total += lens[i];
	for (int rep = 0; rep < 9; rep++) {
		double t = run_random(fn, buf, lens, independent);

		if (t < best)
			best = t;
	}
	return total / best / 1e9;
}

static double measure(crc_fn fn, const uint8_t *buf, size_t len, int independent)
{
	long iters = 16;
	double best = 1e30;

	/* Calibrate to roughly 5 ms per sample. */
	for (;;) {
		double t = run(fn, buf, len, iters, independent);

		if (t >= 0.002) {
			iters = (long)(iters * 0.005 / t) + 1;
			break;
		}
		iters *= 4;
	}
	for (int rep = 0; rep < 9; rep++) {
		double t = run(fn, buf, len, iters, independent) / iters;

		if (t < best)
			best = t;
	}
	return (double)len / best / 1e9;
}

int main(int argc, char **argv)
{
	static const size_t default_lens[] = { 64, 128, 256, 512, 1024, 2048, 4092,
					       4096, 8192, 16352, 16384, 65536,
					       1048576 };
	size_t lens[64];
	size_t nlens = 0;
	const char *filters[32];
	int nfilters = 0;
	int independent = 0;
	size_t offset = 0;
	size_t rand_lo[16], rand_hi[16], rand_step[16];
	int nrand = 0;
	uint8_t *base;

	int ns_per_call = 0;
	const char *lens_file = NULL;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-i"))
			independent = 1;
		else if (!strcmp(argv[i], "-n"))
			ns_per_call = 1;	/* report ns per call, not GB/s */
		else if (!strcmp(argv[i], "-L") && i + 1 < argc)
			lens_file = argv[++i];	/* lengths, one per line, in order */
		else if (!strcmp(argv[i], "-r") && i + 1 < argc && nrand < 16) {
			/* -r LO,HI[,STEP]: random lengths, multiples of STEP */
			rand_step[nrand] = 1;
			if (sscanf(argv[++i], "%zu,%zu,%zu", &rand_lo[nrand],
				   &rand_hi[nrand], &rand_step[nrand]) < 2) {
				fprintf(stderr, "bad -r argument\n");
				return 2;
			}
			nrand++;
		}
		else if (!strcmp(argv[i], "-o") && i + 1 < argc)
			offset = strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-m") && i + 1 < argc)
			crc32c_chunk_max = strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-f") && i + 1 < argc && nfilters < 32)
			filters[nfilters++] = argv[++i];
		else if (!strcmp(argv[i], "-l") && i + 1 < argc && nlens < 64)
			lens[nlens++] = strtoul(argv[++i], NULL, 0);
		else {
			fprintf(stderr, "usage: %s [-i] [-o offset] [-m chunk_max] [-f name]... [-l len]...\n",
				argv[0]);
			return 2;
		}
	}
	if (!nlens && !nrand && !lens_file) {
		nlens = sizeof(default_lens) / sizeof(default_lens[0]);
		memcpy(lens, default_lens, sizeof(default_lens));
	}

	crc32c_mixed_init();
	base = aligned_alloc(4096, 4 << 20);
	for (size_t i = 0; i < (4 << 20); i++)
		base[i] = (uint8_t)(i * 2654435761u >> 13);

	printf("mode=%s offset=%zu chunk_max=%zu   (%s)\n",
	       independent ? "independent" : "chained", offset, crc32c_chunk_max,
	       ns_per_call ? "ns per call" : "GB/s");
	printf("%8s", "len");
	for (size_t ii = 0; ii < num_impls; ii++) {
		int show = !nfilters;

		for (int f = 0; f < nfilters; f++)
			show |= strstr(impls[ii].name, filters[f]) != NULL;
		if (show)
			printf(" %9s", impls[ii].name);
	}
	printf("\n");
	for (size_t li = 0; li < nlens; li++) {
		printf("%8zu", lens[li]);
		for (size_t ii = 0; ii < num_impls; ii++) {
			int show = !nfilters;

			for (int f = 0; f < nfilters; f++)
				show |= strstr(impls[ii].name, filters[f]) != NULL;
			if (show) {
				double v = measure(impls[ii].fn, base + offset,
						   lens[li], independent);

				printf(ns_per_call ? " %9.2f" : " %9.1f",
				       ns_per_call ? lens[li] / v : v);
			}
		}
		printf("\n");
		fflush(stdout);
	}
	for (int ri = 0; ri < nrand; ri++) {
		static uint32_t rlens[NUM_RANDOM_LENS];
		uint64_t s = 0x9e3779b97f4a7c15ull;
		size_t span = (rand_hi[ri] - rand_lo[ri]) / rand_step[ri] + 1;
		char label[64];
		double mean = 0;

		for (int i = 0; i < NUM_RANDOM_LENS; i++) {
			s ^= s >> 12;
			s ^= s << 25;
			s ^= s >> 27;
			rlens[i] = (uint32_t)(rand_lo[ri] + rand_step[ri] *
				((s * 0x2545f4914f6cdd1dull >> 33) % span));
			mean += rlens[i];
		}
		mean /= NUM_RANDOM_LENS;
		snprintf(label, sizeof(label), "%zu-%zu/%zu", rand_lo[ri],
			 rand_hi[ri], rand_step[ri]);
		printf("%16s", label);
		for (size_t ii = 0; ii < num_impls; ii++) {
			int show = !nfilters;

			for (int f = 0; f < nfilters; f++)
				show |= strstr(impls[ii].name, filters[f]) != NULL;
			if (show) {
				double v = measure_random(impls[ii].fn, base + offset,
							  rlens, independent);

				printf(ns_per_call ? " %9.2f" : " %9.1f",
				       ns_per_call ? mean / v : v);
			}
		}
		printf("   (random lengths)\n");
		fflush(stdout);
	}
	if (lens_file) {
		/* A recorded sequence of lengths, replayed in its own order. */
		static uint32_t flens[NUM_RANDOM_LENS];
		FILE *f = fopen(lens_file, "r");
		int n = 0;
		double mean = 0;
		unsigned int v;

		if (!f) {
			perror(lens_file);
			return 1;
		}
		while (n < NUM_RANDOM_LENS && fscanf(f, "%u", &v) == 1)
			flens[n++] = v;
		fclose(f);
		if (!n) {
			fprintf(stderr, "%s: no lengths\n", lens_file);
			return 1;
		}
		for (int i = n; i < NUM_RANDOM_LENS; i++)
			flens[i] = flens[i % n];
		for (int i = 0; i < NUM_RANDOM_LENS; i++)
			mean += flens[i];
		mean /= NUM_RANDOM_LENS;
		printf("%16s", "from file");
		for (size_t ii = 0; ii < num_impls; ii++) {
			int show = !nfilters;

			for (int fi = 0; fi < nfilters; fi++)
				show |= strstr(impls[ii].name, filters[fi]) != NULL;
			if (show) {
				double g = measure_random(impls[ii].fn, base + offset,
							  flens, independent);

				printf(ns_per_call ? " %9.2f" : " %9.1f",
				       ns_per_call ? mean / g : g);
			}
		}
		printf("   (%d lengths read, mean %.1f)\n", n, mean);
	}
	return 0;
}
