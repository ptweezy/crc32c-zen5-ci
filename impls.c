// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The implementations under test: the kernel's own x86 CRC32C code, assembled
 * unmodified from the kernel tree, and the mixed prototype variants.
 */
#include "crc32c_mixed.h"
#include "impls.h"

size_t crc32c_chunk_max = 32768;

uint32_t crc32c_ref(uint32_t crc, const uint8_t *p, size_t len)
{
	while (len--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78u : 0);
	}
	return crc;
}

/* ---- Kernel code (lib/crc/x86), built from the kernel tree ---- */

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define ____cacheline_aligned __attribute__((aligned(64)))
#define __maybe_unused __attribute__((unused))
#include "crc-pclmul-consts.h"

uint32_t crc32_lsb_vpclmul_avx512(uint32_t crc, const uint8_t *p, size_t len,
				  const void *consts_ptr);
uint32_t crc32c_x86_3way(uint32_t crc, const uint8_t *p, size_t len);

#ifdef HAVE_KERNEL_MIXED
/* The kernel assembly version of the mixed code, if the tree has it. */
#include "crc32c-mixed-consts.h"

uint32_t crc32c_mixed_avx512(uint32_t crc, const uint8_t *p, size_t len,
			     const u32 *consts);

#define CRC32C_MIXED_MAX_LEN 4096
#define round_up_64(x) (((x) + 63) & ~(size_t)63)

typedef uint32_t (*mixed_fn)(uint32_t crc, const uint8_t *p, size_t len,
			     const u32 *consts);

/* Mirrors crc32c_mixed() in the kernel's lib/crc/x86/crc32.h. */
static inline __attribute__((always_inline)) uint32_t
crc32c_mixed_glue(mixed_fn fn, uint32_t crc, const uint8_t *p, size_t len)
{
	/* The first len % 8 bytes, without branching on how many there are. */
	if (len % 8) {
		const unsigned int bits = 8 * (len % 8);
		uint64_t head = (crc ^ crc32c_ld64(p)) << (64 - bits);

		crc = (uint32_t)(((uint64_t)crc >> bits) ^ _mm_crc32_u64(0, head));
		p += len % 8;
		len -= len % 8;
	}

	do {
		size_t n = len;

		if (n > CRC32C_MIXED_MAX_LEN) {
			n = round_up_64(n / 2);
			if (n > CRC32C_MIXED_MAX_LEN)
				n = CRC32C_MIXED_MAX_LEN;
		}
		crc = fn(crc, p, n, crc32_lsb_0x82f63b78_mixed_consts);
		p += n;
		len -= n;
	} while (len);
	return crc;
}

/* What crc32c_arch() does with the assembly enabled, minus the FPU section. */
static inline __attribute__((always_inline)) uint32_t
kernel_mixed_with(mixed_fn fn, uint32_t crc, const uint8_t *p, size_t len)
{
	if (len >= 512)
		return crc32c_mixed_glue(fn, crc, p, len);
	return crc32c_one_chain(crc, p, len);
}

static __attribute__((noinline)) uint32_t
kernel_mixed(uint32_t crc, const uint8_t *p, size_t len)
{
	return kernel_mixed_with(crc32c_mixed_avx512, crc, p, len);
}

#ifdef HAVE_ALT
uint32_t crc32c_mixed_alt(uint32_t crc, const uint8_t *p, size_t len,
			  const u32 *consts);

static __attribute__((noinline)) uint32_t
kernel_alt(uint32_t crc, const uint8_t *p, size_t len)
{
	return kernel_mixed_with(crc32c_mixed_alt, crc, p, len);
}
#endif
#endif

static __attribute__((noinline)) uint32_t
kernel_one_chain(uint32_t crc, const uint8_t *p, size_t len)
{
	return crc32c_one_chain(crc, p, len);
}

/* What crc32c_arch() does on a CPU with good AVX-512, minus the FPU section. */
static __attribute__((noinline)) uint32_t
kernel_today(uint32_t crc, const uint8_t *p, size_t len)
{
	if (len >= 512)
		return crc32_lsb_vpclmul_avx512(crc, p, len,
			crc32_lsb_0x82f63b78_consts.fold_across_128_bits_consts);
	return crc32c_one_chain(crc, p, len);
}

/* The AVX-512 function alone, whenever it is callable (len >= 16). */
static __attribute__((noinline)) uint32_t
kernel_avx512(uint32_t crc, const uint8_t *p, size_t len)
{
	if (len >= 16)
		return crc32_lsb_vpclmul_avx512(crc, p, len,
			crc32_lsb_0x82f63b78_consts.fold_across_128_bits_consts);
	return crc32c_one_chain(crc, p, len);
}

/* What crc32c_arch() does on CPUs without good AVX-512. */
static __attribute__((noinline)) uint32_t
kernel_3way(uint32_t crc, const uint8_t *p, size_t len)
{
	if (len >= 512)
		return crc32c_x86_3way(crc, p, len);
	return crc32c_one_chain(crc, p, len);
}

/* ---- Mixed prototype variants ---- */

/* Each variant is built twice: two-stage merge ("...") and direct merge ("...d"). */
#define V(K, S, R)                                                            \
	static __attribute__((noinline)) uint32_t                             \
	mixed_##K##_##S##_##R(uint32_t crc, const uint8_t *p, size_t len)     \
	{                                                                     \
		return crc32c_mixed(crc, p, len, crc32c_chunk_max, K, S, R, 0); \
	}                                                                     \
	static __attribute__((noinline)) uint32_t                             \
	mixed_##K##_##S##_##R##_d(uint32_t crc, const uint8_t *p, size_t len) \
	{                                                                     \
		return crc32c_mixed(crc, p, len, crc32c_chunk_max, K, S, R, 1); \
	}
#include "variants.h"
#undef V

const struct impl impls[] = {
	{ "one-chain", kernel_one_chain, 1 },
	{ "k-3way", kernel_3way, 1 },
	{ "k-avx512", kernel_avx512, 1 },
	{ "k-today", kernel_today, 1 },
#ifdef HAVE_KERNEL_MIXED
	{ "k-mixed", kernel_mixed, 1 },
#ifdef HAVE_ALT
	{ "k-alt", kernel_alt, 1 },
#endif
#endif
#define V(K, S, R)                                             \
	{ "v" #K "s" #S "r" #R, mixed_##K##_##S##_##R, 0 },     \
	{ "v" #K "s" #S "r" #R "d", mixed_##K##_##S##_##R##_d, 0 },
#include "variants.h"
#undef V
};
const size_t num_impls = sizeof(impls) / sizeof(impls[0]);
