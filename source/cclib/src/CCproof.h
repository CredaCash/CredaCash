/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCproof.h
*/

#pragma once

#include "CCapi.h"
#include "CCbigint.hpp"

//#define SUPPORT_ZK_KEYGEN	1	// for setup

#ifndef SUPPORT_ZK_KEYGEN
#define SUPPORT_ZK_KEYGEN	0
#endif

#ifndef CCPROOF_API
#define CCPROOF_API CCRESULT
#endif

#define TX_MAXOUT		16
#define TX_MAXIN		18
#define TX_MAXINPATH	16

#define TX_INPUT_MAX				(bigint_t(0UL)-bigint_t(1UL))		// define to limit inputs to prime field
//#define TX_INPUT_MAX				(bigint_t(0UL))						// define to allow 256 bit inputs

#define TX_INPUT_BITS				256
#define TX_FIELD_BITS				254
#define TX_PAYNUM_BITS				128
#define TX_VALUE_BITS				64
#define TX_COMMIT_IV_BITS			128
#define TX_MERKLE_LEAFINDEX_BITS	48
#define TX_MERKLE_PATH_BITS			TX_FIELD_BITS
#define TX_MERKLE_ROOT_BITS			TX_FIELD_BITS
#define TX_MERKLE_DEPTH				48

#define TX_COMMIT_IV_MASK			"340282366920938463463374607431768211455"	// 2^TX_COMMIT_IV_BITS - 1

#define HASH_MAX_INPUTS				4
#define CC_HASH_BITS				TX_FIELD_BITS

// # basis indexes is 1024 = (HASHBASES_FINISH_START - HASHBASES_RANDOM_START)/2

#define HASH_BASES_MERKLE_NODE		-1	// must be -1
#define HASH_BASES_SPEND_SECRET		0
#define HASH_BASES_RECEIVE_SECRET	1
#define HASH_BASES_DESTINATION		2
#define HASH_BASES_ADDRESS			3
#define HASH_BASES_VALUEENC			4
#define HASH_BASES_COMMITMENT		5
#define HASH_BASES_MERKLE_LEAF		6
#define HASH_BASES_SERIALNUM		7

#define ZKPROOF_VALS				9

#define MAX_INPUTS_FOR_TESTING		32		// must be power of 2 >= TX_MAXIN, i.e., 32

#if SUPPORT_ZK_KEYGEN
CCPROOF_API CCProof_GenKeys();
#endif

CCPROOF_API CCProof_Init();

CCPROOF_API CCProof_GenProof(struct TxPay& tx);

CCPROOF_API CCProof_PreloadVerifyKeys();

CCPROOF_API CCProof_VerifyProof(struct TxPay& tx);
