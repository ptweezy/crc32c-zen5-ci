// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test and time the short CRC32C path variants in short.h.  Self-contained
 * and needs only SSE4.2, so it runs on any x86-64 CPU the kernel's crc32
 * instructions path does.  Build it without -march=native so that the code
 * resembles what a kernel build would generate.
 *
 * usage: shortbench [-i] [-l len]... [-r lo,hi[,step]]... [-L lengths-file]...
 *
 *   -l   one fixed length, repeated
 *   -r   a different pseudo-random length on every call
 *   -L   lengths read from a file, one per line, replayed in order
 *   -i   independent calls instead of each call's CRC feeding the next
 *
 * Output is nanoseconds per call: current, proposed, proposed without the
 * len % 8 == 0 test.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "short.h"

#define NUM_LENS 65536	/* long enough that the predictor can't learn it */

typedef uint32_t (*crc_fn)(uint32_t, const uint8_t *, size_t);

static __attribute__((noinline)) uint32_t f_current(uint32_t c, const uint8_t *p, size_t n)
{
	return crc32c_short_current(c, p, n);
}

static __attribute__((noinline)) uint32_t f_proposed(uint32_t c, const uint8_t *p, size_t n)
{
	return crc32c_short_proposed(c, p, n, 0);
}

static __attribute__((noinline)) uint32_t f_nobranch(uint32_t c, const uint8_t *p, size_t n)
{
	return crc32c_short_proposed(c, p, n, 1);
}

static const struct {
	const char *name;
	crc_fn fn;
} impls[] = {
	{ "current", f_current },
	{ "proposed", f_proposed },
	{ "no-branch", f_nobranch },
};
#define NUM_IMPLS (sizeof(impls) / sizeof(impls[0]))

static uint32_t crc32c_bitwise(uint32_t crc, const uint8_t *p, size_t len)
{
	while (len--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78u : 0);
	}
	return crc;
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static volatile uint32_t sink;

/* Best-of-9 time per call, in ns, over the NUM_LENS calls in lens[]. */
static double time_calls(crc_fn fn, const uint8_t *buf, const uint32_t *lens,
			 int independent)
{
	double best = 1e30;

	for (int rep = 0; rep < 9; rep++) {
		uint32_t crc = 0x12345678, acc = 0;
		double t = now();

		for (int i = 0; i < NUM_LENS; i++) {
			if (independent)
				acc += fn(0x12345678, buf, lens[i]);
			else
				crc = fn(crc, buf, lens[i]);
		}
		t = now() - t;
		sink = crc + acc;
		if (t < best)
			best = t;
	}
	return best / NUM_LENS * 1e9;
}

static void report(const char *label, const uint8_t *buf, const uint32_t *lens,
		   int independent)
{
	double t[NUM_IMPLS];

	for (size_t i = 0; i < NUM_IMPLS; i++)
		t[i] = time_calls(impls[i].fn, buf, lens, independent);
	printf("%-24s %8.2f %8.2f %8.2f   %+5.0f%% %+5.0f%%\n", label, t[0], t[1], t[2],
	       100 * (t[1] / t[0] - 1), 100 * (t[2] / t[0] - 1));
	fflush(stdout);
}

int main(int argc, char **argv)
{
	static uint32_t lens[NUM_LENS];
	uint8_t *buf = aligned_alloc(4096, 1 << 16);
	int independent = 0;
	uint64_t s = 0x9e3779b97f4a7c15ull;

	for (int i = 0; i < (1 << 16); i++) {
		s ^= s >> 12;
		s ^= s << 25;
		s ^= s >> 27;
		buf[i] = (uint8_t)(s * 0x2545f4914f6cdd1dull >> 56);
	}

	/* Correctness first: every length to 2100, several alignments and CRCs. */
	for (size_t len = 0; len <= 2100; len++) {
		for (int k = 0; k < 3; k++) {
			const uint8_t *p = buf + (len * 7 + k * 13) % 64;
			uint32_t init = (uint32_t)(len * 2654435761u + k * 40503u);
			uint32_t want = crc32c_bitwise(init, p, len);

			for (size_t i = 0; i < NUM_IMPLS; i++) {
				if (impls[i].fn(init, p, len) != want) {
					printf("FAIL: %s is wrong at len=%zu\n",
					       impls[i].name, len);
					return 1;
				}
			}
			if (crc32c_short_proposed_32bit(init, p, len) != want) {
				printf("FAIL: 32-bit form is wrong at len=%zu\n", len);
				return 1;
			}
		}
	}
	printf("all three, and the 32-bit form of the proposal, agree with a bit-at-a-time CRC for every length to 2100\n");
	printf("%-24s %8s %8s %8s   %6s %6s\n", "ns per call", "current",
	       "proposed", "no-br", "prop", "no-br");

	for (int a = 1; a < argc; a++) {
		char label[64];

		if (!strcmp(argv[a], "-i")) {
			independent = 1;
			printf("-- independent calls from here on --\n");
		} else if (!strcmp(argv[a], "-l") && a + 1 < argc) {
			unsigned int len = strtoul(argv[++a], NULL, 0);

			for (int i = 0; i < NUM_LENS; i++)
				lens[i] = len;
			snprintf(label, sizeof(label), "fixed %u", len);
			report(label, buf, lens, independent);
		} else if (!strcmp(argv[a], "-r") && a + 1 < argc) {
			unsigned int lo, hi, step = 1;

			if (sscanf(argv[++a], "%u,%u,%u", &lo, &hi, &step) < 2 || hi < lo) {
				fprintf(stderr, "bad -r argument\n");
				return 2;
			}
			for (int i = 0; i < NUM_LENS; i++) {
				s ^= s >> 12;
				s ^= s << 25;
				s ^= s >> 27;
				lens[i] = lo + step * (uint32_t)((s * 0x2545f4914f6cdd1dull >> 33) %
								 ((hi - lo) / step + 1));
			}
			snprintf(label, sizeof(label), "random %u-%u step %u", lo, hi, step);
			report(label, buf, lens, independent);
		} else if (!strcmp(argv[a], "-L") && a + 1 < argc) {
			FILE *f = fopen(argv[++a], "r");
			unsigned int v;
			int n = 0;
			double mean = 0;

			if (!f) {
				perror(argv[a]);
				return 1;
			}
			while (n < NUM_LENS && fscanf(f, "%u", &v) == 1)
				lens[n++] = v > 60000 ? 60000 : v;
			fclose(f);
			if (!n) {
				fprintf(stderr, "%s: no lengths\n", argv[a]);
				return 1;
			}
			for (int i = n; i < NUM_LENS; i++)
				lens[i] = lens[i % n];
			for (int i = 0; i < NUM_LENS; i++)
				mean += lens[i];
			snprintf(label, sizeof(label), "file (mean %.1f)", mean / NUM_LENS);
			report(label, buf, lens, independent);
		} else {
			fprintf(stderr, "usage: %s [-i] [-l len]... [-r lo,hi[,step]]... [-L file]...\n",
				argv[0]);
			return 2;
		}
	}
	return 0;
}
