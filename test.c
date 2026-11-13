// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Correctness test: every implementation must match a reference CRC32C for
 * every length, alignment, initial CRC and chunk size tried here.
 */
#include <stdio.h>
#include <stdlib.h>

#include "crc32c_mixed.h"
#include "impls.h"

#define MAX_LEN 200000

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint64_t rnd(void)
{
	rng_state ^= rng_state >> 12;
	rng_state ^= rng_state << 25;
	rng_state ^= rng_state >> 27;
	return rng_state * 0x2545f4914f6cdd1dull;
}

static uint32_t fast_ref(uint32_t crc, const uint8_t *p, size_t len)
{
	return crc32c_one_chain(crc, p, len);
}

/* usage: test [N]   -- test every length from 0 to N (default 9000) */
int main(int argc, char **argv)
{
	static const size_t chunk_maxes[] = { 2048, 4096, 8192, 32768 };
	uint8_t *buf = aligned_alloc(64, MAX_LEN + 128);
	unsigned long total_fail = 0;
	size_t every_len_to = argc > 1 ? strtoul(argv[1], NULL, 0) : 9000;

	if (every_len_to > MAX_LEN)
		every_len_to = MAX_LEN;

	crc32c_mixed_init();
	for (size_t i = 0; i < MAX_LEN + 128; i++)
		buf[i] = (uint8_t)rnd();

	/* Known answer, then the instruction-based reference vs bit-at-a-time. */
	if ((crc32c_ref(~0u, (const uint8_t *)"123456789", 9) ^ ~0u) != 0xe3069283u) {
		printf("FAIL: bitwise reference does not give the CRC-32C check value\n");
		return 1;
	}
	for (size_t len = 0; len <= 3000; len++) {
		size_t off = len % 64;
		uint32_t init = (uint32_t)rnd();

		if (crc32c_ref(init, buf + off, len) != fast_ref(init, buf + off, len)) {
			printf("FAIL: instruction reference != bitwise at len=%zu\n", len);
			return 1;
		}
	}
	printf("references agree (check value 0xe3069283, lengths 0..3000)\n");

	for (size_t ci = 0; ci < sizeof(chunk_maxes) / sizeof(chunk_maxes[0]); ci++) {
		crc32c_chunk_max = chunk_maxes[ci];
		for (size_t ii = 0; ii < num_impls; ii++) {
			const struct impl *im = &impls[ii];
			unsigned long fails = 0, tests = 0;

			if (im->is_baseline && ci > 0)
				continue;	/* chunk size doesn't affect baselines */

			/* Every length up to the limit, at varying alignment. */
			for (size_t len = 0; len <= every_len_to; len++) {
				size_t off = (len * 7) % 64;
				uint32_t init = (uint32_t)rnd();

				tests++;
				if (im->fn(init, buf + off, len) !=
				    fast_ref(init, buf + off, len)) {
					if (fails++ < 3)
						printf("  FAIL %s chunk_max=%zu len=%zu off=%zu\n",
						       im->name, crc32c_chunk_max, len, off);
				}
			}
			/* Random longer lengths, alignments and initial CRCs. */
			for (int k = 0; k < 4000; k++) {
				size_t len = rnd() % (MAX_LEN + 1);
				size_t off = rnd() % 64;
				uint32_t init = (uint32_t)rnd();

				tests++;
				if (im->fn(init, buf + off, len) !=
				    fast_ref(init, buf + off, len)) {
					if (fails++ < 3)
						printf("  FAIL %s chunk_max=%zu len=%zu off=%zu\n",
						       im->name, crc32c_chunk_max, len, off);
				}
			}
			printf("%-10s chunk_max=%-6zu %s (%lu tests, %lu failures)\n",
			       im->name, im->is_baseline ? (size_t)0 : crc32c_chunk_max,
			       fails ? "FAIL" : "ok", tests, fails);
			total_fail += fails;
		}
	}
	printf("%s\n", total_fail ? "SOME TESTS FAILED" : "all tests passed");
	return total_fail != 0;
}
