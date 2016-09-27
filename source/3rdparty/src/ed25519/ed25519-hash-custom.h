/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * ed25519-hash-custom.h
*/

#pragma once

#include <KeccakHash.h>
typedef Keccak_HashInstance ed25519_hash_context;

#include <ed25519-donna-portable-identify.h>

//#include <blake2b.h>
//typedef blake2b_ctx ed25519_hash_context;

void ed25519_hash_init(ed25519_hash_context *ctx);
void ed25519_hash_update(ed25519_hash_context *ctx, const uint8_t *in, size_t inlen);
void ed25519_hash_final(ed25519_hash_context *ctx, uint8_t *hash);
void ed25519_hash(uint8_t *hash, const uint8_t *in, size_t inlen);
