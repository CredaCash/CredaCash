/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * siphash.h
*/

#pragma once

// Note: siphash.c must be compiled with -DUNALIGNED_WORD_ACCESS=1, otherwise it will not work correctly

#define HAVE_UINT64_T 1

#if defined(__cplusplus)
extern "C" {
#endif

#include <siphash/src/siphash.h>

inline uint64_t siphash(const void *data, size_t nbytes, const void *key = NULL, size_t keylen = 0)
{
	CCASSERT(keylen <= 16);

	uint8_t k[16];

	if (keylen < 16)
		memset(k, 0, sizeof(k));

	if (keylen > 0)
		memcpy(k, key, keylen);

	return sip_hash24(k, (uint8_t*)data, nbytes);
}

#if defined(__cplusplus)
}
#endif
