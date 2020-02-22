/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * CCbigint.hpp
*/

// for snarklib/snarkfront:
#define USE_ASSERT
#define USE_ASM
#define USE_ADD_SPECIAL

#include <snarkfront/Alg_BigInt.hpp>

#pragma once

#if 0
#include <snarkfront/Alg_Field.hpp>
typedef BN128_FR		bigint_t;
#define BIGINT(x)		(x[0]).asBigInt()
#endif

#define BIGINT(x)		(x)

#define BIGDATA(x)		(BIGINT(x).data())
#define BIGWORD(x,i)	(BIGDATA(x)[i])
#define BIG64(x)		BIGWORD(x,0)

bool bigint_bit(const snarkfront::bigint_t& bigval, unsigned bit);
void bigint_shift_up(snarkfront::bigint_t& bigval, unsigned nbits);
void bigint_shift_down(snarkfront::bigint_t& bigval, unsigned nbits);
void bigint_mask(snarkfront::bigint_t& bigval, unsigned nbits);
unsigned bigint_bytes_in_use(snarkfront::bigint_t& bigval);

void bigint_test();
