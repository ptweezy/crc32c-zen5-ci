// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Constants for the mixed CRC32C prototype, generated at startup.
 * A kernel version would generate these at build time instead.
 */
#include "crc32c_mixed.h"

#define CRC32C_POLY 0x82f63b78u	/* bit-reflected, x^32 term omitted */

uint32_t crc32c_g[CRC32C_G_MAX + 1];
uint32_t crc32c_gd[CRC32C_G_MAX + 1];
uint64_t crc32c_red[CRC32C_MAX_K + 1][CRC32C_MAX_K][8] __attribute__((aligned(64)));

/* Multiply a bit-reflected polynomial by x^64 mod P, one bit at a time. */
static uint32_t mul_x64(uint32_t a)
{
	for (int i = 0; i < 64; i++)
		a = (a >> 1) ^ ((a & 1) ? CRC32C_POLY : 0);
	return a;
}

void crc32c_mixed_init(void)
{
	/* Bit i holds the coefficient of x^(31-i), so x^31 is the value 1. */
	uint32_t v = 1;

	for (int d = 0; d <= CRC32C_G_MAX; d++) {
		crc32c_g[d] = v;
		crc32c_gd[CRC32C_G_MAX - d] = v;
		v = mul_x64(v);
	}

	/*
	 * State l of accumulator j ends 8*(K-1-j) + 2*(3-l) qwords before the
	 * end of V.  The last state is already there and is XORed in as-is, so
	 * its constants are zero.
	 */
	for (int K = 1; K <= CRC32C_MAX_K; K++) {
		for (int j = 0; j < K; j++) {
			for (int l = 0; l < 4; l++) {
				int d = 8 * (K - 1 - j) + 2 * (3 - l);

				crc32c_red[K][j][2 * l] = d ? crc32c_g[d] : 0;
				crc32c_red[K][j][2 * l + 1] = d ? crc32c_g[d - 1] : 0;
			}
		}
	}
}
