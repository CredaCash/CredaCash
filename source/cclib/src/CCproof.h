/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * CCproof.h
*/

#pragma once

#include "CCapi.h"
#include "CCbigint.hpp"
#include "CCparams.h"

//#define TEST_SUPPORT_ZK_KEYGEN	1	// for setup; note: 68 minutes on ODROID-C2 to generate 31 key pairs

//#define TEST_SKIP_ZKPROOFS		1	// for faster testing of tx handling ***NOTE: also set *_work_difficulty = 0

#ifndef TEST_SUPPORT_ZK_KEYGEN
#define TEST_SUPPORT_ZK_KEYGEN		0
#endif

#ifndef TEST_SKIP_ZKPROOFS
#define TEST_SKIP_ZKPROOFS			0	// don't skip
#endif

#define KEY_PATH_ENV_VAR			"CC_PROOF_KEY_DIR"

#define CCPROOF_ERR_NO_KEY				-2
#define CCPROOF_ERR_INSUFFICIENT_KEY	-3
#define CCPROOF_ERR_LOADING_KEY			-4
#define CCPROOF_ERR_NO_PROOF			-5

#ifndef CCPROOF_API
#define CCPROOF_API CCRESULT
//#define CCPROOF_API CCAPI		// for testing
#endif

// basis indexes

#define HASH_BASES_MERKLE_NODE			-1	// must be -1
#define HASH_BASES_ROOT_SECRET			0
#define HASH_BASES_SPEND_SECRET			1
#define HASH_BASES_TRUST_SECRET			2
#define HASH_BASES_MONITOR_SECRET		3
#define HASH_BASES_RECEIVE_SECRET		4
#define HASH_BASES_DESTINATION			5
#define HASH_BASES_ADDRESS				6
#define HASH_BASES_AMOUNT_ENC			7
#define HASH_BASES_COMMITMENT			8
#define HASH_BASES_MERKLE_LEAF			9
#define HASH_BASES_SERIALNUM			10

struct TxPay;

#if TEST_SUPPORT_ZK_KEYGEN
CCPROOF_API CCProof_GenKeys();
#endif

CCPROOF_API CCProof_Init(const std::wstring& proof_key_dir = std::wstring());

CCPROOF_API CCProof_GenProof(TxPay& tx);

CCPROOF_API CCProof_PreloadVerifyKeys(bool require_all = false);

CCPROOF_API CCProof_VerifyProof(TxPay& tx);
