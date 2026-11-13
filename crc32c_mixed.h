/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Userspace prototype: CRC32C using crc32q chains and 512-bit VPCLMULQDQ
 * folding in the same loop.
 *
 * This is an experiment for choosing a design, not kernel code.  It uses
 * compiler intrinsics and a runtime-generated constants table, neither of
 * which a kernel version would do.
 *
 * Layout of one chunk of `len` bytes:
 *
 *   [ V: 64*K*n bytes ][ lane 0: 8*(q+t) ][ lane 1: 8*q ] ... [ lane S-1: 8*q ][ F: 16 ][ tail: len%8 ]
 *
 * V is folded by K zmm accumulators (4*K independent 128-bit states), K blocks
 * per loop iteration.  Each of the S lanes is one crc32q dependency chain, R
 * qwords per loop iteration.  Lane 0 absorbs the t leftover qwords, because
 * its length affects only the constants for V.  At the end, V's state and the
 * CRCs of lanes 0..S-2 are multiplied by the power of x that moves them to the
 * position of F, XORed into F, and the last lane's chain consumes F.
 *
 * Every constant is g(d) = x^(64*d + 31) mod P for some qword distance d, so
 * one table indexed by d serves the in-loop fold, the final reduction and the
 * merge.
 */
#ifndef CRC32C_MIXED_H
#define CRC32C_MIXED_H

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Largest qword distance the table covers.  Prototype-sized, not kernel-sized. */
#define CRC32C_G_MAX 8200
#define CRC32C_MAX_K 8

/* g[d] = x^(64*d + 31) mod P, in the bit-reflected form the instructions use. */
extern uint32_t crc32c_g[CRC32C_G_MAX + 1];
/* The same table in descending order: crc32c_gd[i] = g(CRC32C_G_MAX - i). */
extern uint32_t crc32c_gd[CRC32C_G_MAX + 1];
/* Per K: constants folding every 128-bit state of every accumulator to the end of V. */
extern uint64_t crc32c_red[CRC32C_MAX_K + 1][CRC32C_MAX_K][8] __attribute__((aligned(64)));

void crc32c_mixed_init(void);

static inline __attribute__((always_inline)) uint64_t crc32c_ld64(const uint8_t *p)
{
	uint64_t v;

	memcpy(&v, p, 8);
	return v;
}

/* One crc32 dependency chain, as the kernel does today below 512 bytes. */
static inline __attribute__((always_inline)) uint32_t
crc32c_one_chain(uint32_t crc, const uint8_t *p, size_t len)
{
	uint64_t c = crc;

	for (; len >= 8; len -= 8, p += 8)
		c = _mm_crc32_u64(c, crc32c_ld64(p));
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
	return (uint32_t)c;
}

static inline __attribute__((always_inline)) __m512i
crc32c_fold512(__m512i acc, __m512i consts, __m512i data)
{
	__m512i lo = _mm512_clmulepi64_epi128(acc, consts, 0x00);
	__m512i hi = _mm512_clmulepi64_epi128(acc, consts, 0x11);

	return _mm512_ternarylogic_epi64(lo, hi, data, 0x96);
}

/* Smallest chunk the mixed code accepts: one loop iteration plus F. */
#define CRC32C_MIXED_MIN(K, S, R) ((size_t)(64 * (K) + 8 * (S) * (R) + 16))

/*
 * CRC one chunk.  K, S, R and DIRECT must be compile-time constants: K in 0..8
 * (0 = no vector part), S in 1..8, R in 1..8.  Requires
 * len >= CRC32C_MIXED_MIN(K, S, R) and len / 8 + 8 * K <= CRC32C_G_MAX.
 *
 * DIRECT selects how V's state reaches F:
 *   0: fold the 4*K states to the end of V with fixed constants, XOR them
 *      together, then multiply that one state across the lanes (two
 *      carryless-multiply stages).
 *   1: multiply every state straight to F.  State s needs g(m + 2 + d_s) and
 *      g(m + 1 + d_s), where d_s is its distance from the end of V, so one
 *      accumulator needs 8 consecutive table entries in descending order:
 *      a single zero-extending load from the descending table.
 */
static inline __attribute__((always_inline)) uint32_t
crc32c_mixed_chunk(uint32_t crc, const uint8_t *p, size_t len,
		   const int K, const int S, const int R, const int DIRECT)
{
	const size_t iter_bytes = 64 * (size_t)K + 8 * (size_t)S * R;
	const size_t n = (len - 16) / iter_bytes;
	const size_t rem_q = (len - 16 - n * iter_bytes) / 8;
	const size_t e = rem_q / S;	/* extra rounds on every lane */
	const size_t t = rem_q % S;	/* extra qwords on lane 0 only */
	const size_t q = n * R + e;	/* qwords per lane (lane 0: q + t) */
	const size_t m = (size_t)S * q + t;	/* qwords between V and F */
	const size_t lq = 8 * q;	/* lane stride in bytes */
	const uint8_t *vp = p;
	const uint8_t *lp = p + 64 * (size_t)K * n;
	const __m512i kloop = _mm512_broadcast_i32x4(
		_mm_set_epi64x(crc32c_g[K ? 8 * K - 1 : 0], crc32c_g[8 * K]));
	__m512i acc[CRC32C_MAX_K];
	uint64_t c[8];
	__m128i T;
	const uint8_t *F;
	uint64_t r;

	for (int i = 0; i < 8; i++)
		c[i] = 0;
	if (K) {
#pragma GCC unroll 8
		for (int j = 0; j < K; j++)
			acc[j] = _mm512_loadu_si512(vp + 64 * j);
		/* (_mm512_zextsi128_si512() would do, but gcc 9 lacks it.) */
		acc[0] = _mm512_xor_si512(acc[0],
			_mm512_inserti32x4(_mm512_setzero_si512(),
					   _mm_cvtsi32_si128((int)crc), 0));
		vp += 64 * (size_t)K;
	} else {
		c[0] = crc;
	}

	/* Lane 0's leftover qwords. */
	for (size_t i = 0; i < t; i++, lp += 8)
		c[0] = _mm_crc32_u64(c[0], crc32c_ld64(lp));

	/* First iteration: the vector part was only the loads above. */
#pragma GCC unroll 8
	for (int rr = 0; rr < R; rr++) {
#pragma GCC unroll 8
		for (int i = 0; i < S; i++)
			c[i] = _mm_crc32_u64(c[i], crc32c_ld64(lp + i * lq + 8 * rr));
	}
	lp += 8 * (size_t)R;

	for (size_t it = 1; it < n; it++) {
#pragma GCC unroll 8
		for (int j = 0; j < K; j++)
			acc[j] = crc32c_fold512(acc[j], kloop,
						_mm512_loadu_si512(vp + 64 * j));
		vp += 64 * (size_t)K;
#pragma GCC unroll 8
		for (int rr = 0; rr < R; rr++) {
#pragma GCC unroll 8
			for (int i = 0; i < S; i++)
				c[i] = _mm_crc32_u64(c[i],
					crc32c_ld64(lp + i * lq + 8 * rr));
		}
		lp += 8 * (size_t)R;
	}

	/* Extra rounds: scalar lanes only. */
	for (size_t x = 0; x < e; x++, lp += 8) {
#pragma GCC unroll 8
		for (int i = 0; i < S; i++)
			c[i] = _mm_crc32_u64(c[i], crc32c_ld64(lp + i * lq));
	}
	/* lp is now the start of lane 1, so F is (S-1) lanes further on. */
	F = lp + (size_t)(S - 1) * lq;

	T = _mm_setzero_si128();
	if (K && DIRECT) {
		/* Multiply all 4*K states straight to the position of F. */
		const uint32_t *gd = &crc32c_gd[CRC32C_G_MAX - m - 8 * (size_t)K];
		__m512i z = _mm512_setzero_si512();
		__m256i y;

#pragma GCC unroll 8
		for (int j = 0; j < K; j++) {
			__m512i cj = _mm512_cvtepu32_epi64(_mm256_loadu_si256(
					(const __m256i *)(gd + 8 * j)));

			z = crc32c_fold512(acc[j], cj, z);
		}
		y = _mm256_xor_si256(_mm512_castsi512_si256(z),
				     _mm512_extracti64x4_epi64(z, 1));
		T = _mm_xor_si128(_mm256_castsi256_si128(y),
				  _mm256_extracti128_si256(y, 1));
	} else if (K) {
		/* Fold all 4*K states to the end of V, then across the lanes to F. */
		const uint64_t (*red)[8] = crc32c_red[K];
		const __m128i kab = _mm_set_epi64x(crc32c_g[m + 1], crc32c_g[m + 2]);
		__m512i z = _mm512_setzero_si512();
		__m256i y;
		__m128i x;

#pragma GCC unroll 8
		for (int j = 0; j < K; j++) {
			__m512i cj = _mm512_load_si512(red[j]);

			z = crc32c_fold512(acc[j], cj, z);
		}
		y = _mm256_xor_si256(_mm512_castsi512_si256(z),
				     _mm512_extracti64x4_epi64(z, 1));
		x = _mm_xor_si128(_mm256_castsi256_si128(y),
				  _mm256_extracti128_si256(y, 1));
		x = _mm_xor_si128(x, _mm512_extracti32x4_epi32(acc[K - 1], 3));
		T = _mm_xor_si128(_mm_clmulepi64_si128(x, kab, 0x00),
				  _mm_clmulepi64_si128(x, kab, 0x11));
	}
#pragma GCC unroll 8
	for (int i = 0; i < S - 1; i++) {
		const __m128i k = _mm_cvtsi32_si128((int)crc32c_g[q * (size_t)(S - 1 - i)]);

		T = _mm_xor_si128(T, _mm_clmulepi64_si128(
					_mm_cvtsi64_si128((long long)c[i]), k, 0x00));
	}

	r = _mm_crc32_u64(c[S - 1],
			  crc32c_ld64(F) ^ (uint64_t)_mm_cvtsi128_si64(T));
	r = _mm_crc32_u64(r, crc32c_ld64(F + 8) ^ (uint64_t)_mm_extract_epi64(T, 1));
	return crc32c_one_chain((uint32_t)r, F + 16, len & 7);
}

/*
 * CRC a whole buffer: split it into chunks of at most chunk_max bytes
 * (a multiple of 64), and finish anything too short for the mixed code with a
 * single crc32 chain.
 */
static inline __attribute__((always_inline)) uint32_t
crc32c_mixed(uint32_t crc, const uint8_t *p, size_t len, size_t chunk_max,
	     const int K, const int S, const int R, const int DIRECT)
{
	const size_t min_len = CRC32C_MIXED_MIN(K, S, R);

	while (len >= min_len) {
		size_t cl = len;

		if (len > chunk_max) {
			/* Don't leave a remainder too short for the mixed code. */
			cl = len - chunk_max >= min_len ? chunk_max :
						       (len / 2) & ~(size_t)63;
		}
		crc = crc32c_mixed_chunk(crc, p, cl, K, S, R, DIRECT);
		p += cl;
		len -= cl;
	}
	return crc32c_one_chain(crc, p, len);
}

#endif /* CRC32C_MIXED_H */
