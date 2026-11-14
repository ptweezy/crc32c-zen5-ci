/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The kernel's short CRC32C path on x86 (a single chain of crc32
 * instructions), as it is today and with the last len % 8 bytes handled
 * without branching on how many of them there are.  Needs only SSE4.2.
 */
#ifndef SHORT_H
#define SHORT_H

#include <nmmintrin.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline __attribute__((always_inline)) uint64_t short_ld64(const uint8_t *p)
{
	uint64_t v;

	memcpy(&v, p, 8);
	return v;
}

/* The last 0-7 bytes, the way crc32c_arch() does them today. */
static inline __attribute__((always_inline)) uint64_t
short_tail_tests(uint64_t c, const uint8_t *p, size_t len)
{
	if (len & 4) {
		uint32_t v;

		memcpy(&v, p, 4);
		c = _mm_crc32_u32((uint32_t)c, v);
		p += 4;
	}
	if (len & 2) {
		uint16_t v;

		memcpy(&v, p, 2);
		c = _mm_crc32_u16((uint32_t)c, v);
		p += 2;
	}
	if (len & 1)
		c = _mm_crc32_u8((uint32_t)c, *p);
	return c;
}

/* What crc32c_arch() does today when it doesn't use vector code. */
static inline __attribute__((always_inline)) uint32_t
crc32c_short_current(uint32_t crc, const uint8_t *p, size_t len)
{
	uint64_t c = crc;
	size_t num_longs;

	for (num_longs = len / 8; num_longs != 0; num_longs--, p += 8)
		c = _mm_crc32_u64(c, short_ld64(p));
	return (uint32_t)short_tail_tests(c, p, len);
}

/*
 * Proposed.  Updating a CRC with k bytes shifts it right by 8*k bits and XORs
 * in the CRC, from zero, of those bytes XORed with the bits that got shifted
 * out.  One crc32q computes the latter if the bytes sit at the end of a qword
 * behind zero bytes, as zero bytes leave a zero CRC unchanged.  When the input
 * is at least a qword long, its last qword contains the k bytes at its end
 * already, so shifting that qword down and back up is all the preparation
 * needed.
 *
 * NO_BRANCH also drops the test for k == 0, using two-step shifts so that a
 * shift by 64 is well defined.
 */
static inline __attribute__((always_inline)) uint32_t
crc32c_short_proposed(uint32_t crc, const uint8_t *p, size_t len, const int NO_BRANCH)
{
	uint64_t c = crc;
	size_t num_longs = len / 8;

	if (num_longs) {
		const uint8_t *end = p + len;
		const size_t k = len % 8;

		do {
			c = _mm_crc32_u64(c, short_ld64(p));
			p += 8;
		} while (--num_longs);

		if (NO_BRANCH) {
			const unsigned int down = 63 - 8 * k;
			uint64_t x = ((short_ld64(end - 8) >> down >> 1) ^ c) << down << 1;

			c = (c >> (8 * k)) ^ _mm_crc32_u64(0, x);
		} else if (k) {
			const unsigned int shift = 64 - 8 * k;
			uint64_t x = ((short_ld64(end - 8) >> shift) ^ c) << shift;

			c = (c >> (8 * k)) ^ _mm_crc32_u64(0, x);
		}
		return (uint32_t)c;
	}
	return (uint32_t)short_tail_tests(c, p, len);
}

/*
 * The proposed code as it works out on 32-bit x86, where a long is 4 bytes:
 * crc32l in the loop, and 1-3 bytes left over.  This is here only so that the
 * 32-bit arithmetic can be tested on a 64-bit machine.
 */
static inline __attribute__((always_inline)) uint32_t
crc32c_short_proposed_32bit(uint32_t crc, const uint8_t *p, size_t len)
{
	size_t num_longs = len / 4;

	if (num_longs) {
		const uint8_t *end = p + len;
		const size_t rem = len % 4;

		do {
			uint32_t v;

			memcpy(&v, p, 4);
			crc = _mm_crc32_u32(crc, v);
			p += 4;
		} while (--num_longs);

		if (rem) {
			const unsigned int shift = 32 - 8 * rem;
			uint32_t tail;

			memcpy(&tail, end - 4, 4);
			tail = ((tail >> shift) ^ crc) << shift;
			crc = (crc >> (8 * rem)) ^ _mm_crc32_u32(0, tail);
		}
		return crc;
	}
	if (len & 2) {
		uint16_t v;

		memcpy(&v, p, 2);
		crc = _mm_crc32_u16(crc, v);
		p += 2;
	}
	if (len & 1)
		crc = _mm_crc32_u8(crc, *p);
	return crc;
}

#endif /* SHORT_H */
