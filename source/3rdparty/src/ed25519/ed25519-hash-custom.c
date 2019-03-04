/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * ed25519-hash-custom.c
*/

#include <ed25519-hash-custom.h>
#include <blake2b.h>

#include <assert.h>

typedef blake2b_ctx ed25519_hash_context;

void ed25519_hash_init(ed25519_hash_context *ctx)
{
	int rc = blake2b_init(ctx, 512/8, NULL, 0);
	assert(!rc);
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
	assert(!rc);
}
