/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
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

inline uint64_t siphash(const uint8_t *data, size_t len)
{
	uint8_t key[16];
	memset(key, 0, sizeof(key));

	return sip_hash24(key, const_cast<uint8_t*>(data), len);
}

inline uint64_t siphash_keyed(const uint8_t key[16], const uint8_t *data, size_t len)
{
	return sip_hash24(const_cast<uint8_t*>(key), const_cast<uint8_t*>(data), len);
}

#if defined(__cplusplus)
}
#endif
