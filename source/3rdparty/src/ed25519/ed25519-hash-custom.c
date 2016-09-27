/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * ed25519-hash-custom.c
*/

#include "ed25519-hash-custom.h"

#include <CCassert.h>

void ed25519_hash_init(ed25519_hash_context *ctx)
{
	int rc = Keccak_HashInitialize_SHA3_512(ctx);
	CCASSERTZ(rc);
}

void ed25519_hash_update(ed25519_hash_context *ctx, const uint8_t *in, size_t inlen)
{
	int rc = Keccak_HashUpdate(ctx, in, inlen);
	CCASSERTZ(rc);
}

void ed25519_hash_final(ed25519_hash_context *ctx, uint8_t *hash)
{
	int rc = Keccak_HashFinal(ctx, hash);
	CCASSERTZ(rc);
}

void ed25519_hash(uint8_t *hash, const uint8_t *in, size_t inlen)
{
	ed25519_hash_context ctx;

	ed25519_hash_init(&ctx);
	ed25519_hash_update(&ctx, in, inlen);
	ed25519_hash_final(&ctx, hash);
}


#if 0	// using keccak (see above)

void ed25519_hash_init(ed25519_hash_context *ctx)
{
	int rc = blake2b_init(ctx, 512/8, NULL, 0);
	CCASSERTZ(rc);
}

void ed25519_hash_update(ed25519_hash_context *ctx, const uint8_t *in, size_t inlen)
{
	blake2b_update(ctx, in, inlen);
}

void ed25519_hash_final(ed25519_hash_context *ctx, uint8_t *hash)
{
	blake2b_final(ctx, hash);
}

void ed25519_hash(uint8_t *hash, const uint8_t *in, size_t inlen)
{
	int rc = blake2b(hash, 512/8, NULL, 0, in, inlen);
	CCASSERTZ(rc);
}

#endif
