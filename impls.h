/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IMPLS_H
#define IMPLS_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t (*crc_fn)(uint32_t crc, const uint8_t *p, size_t len);

struct impl {
	const char *name;
	crc_fn fn;
	int is_baseline;
};

extern const struct impl impls[];
extern const size_t num_impls;

/* Largest chunk the mixed variants process before merging (multiple of 64). */
extern size_t crc32c_chunk_max;

/* Bit-at-a-time reference. */
uint32_t crc32c_ref(uint32_t crc, const uint8_t *p, size_t len);

#endif
