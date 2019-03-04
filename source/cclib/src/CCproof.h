/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * CCproof.h
*/

#pragma once

#include "CCapi.h"
#include "CCbigint.hpp"
#include "CCparams.h"

//#define TEST_SUPPORT_ZK_KEYGEN	1	// for setup

#ifndef TEST_SUPPORT_ZK_KEYGEN
#define TEST_SUPPORT_ZK_KEYGEN	0
#endif

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

CCPROOF_API CCProof_Init();

CCPROOF_API CCProof_GenProof(TxPay& tx);

CCPROOF_API CCProof_PreloadVerifyKeys(bool require_all = false);

CCPROOF_API CCProof_VerifyProof(TxPay& tx);
