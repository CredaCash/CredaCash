/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * CCmint.h
*/

#pragma once

#define Implement_CCMint(blockchain)	((blockchain) == MAINNET_BLOCKCHAIN)

//#define TEST_SMALL_MINT		1	// set params to test a small mint

#ifndef TEST_SMALL_MINT
#define TEST_SMALL_MINT		0		// if defined, set params to test a small mint
#endif

// normal mint params
#if !TEST_SMALL_MINT
#define CC_MINT_COUNT		200000	// number of mints
#define CC_MINT_ACCEPT_SPAN	11		// param level acceptance span
#define CC_MINT_BLOCK_SEC	15		// seconds per block

// TEST_SMALL_MINT params
#else
#define CC_MINT_COUNT		12		// number of mints
#define CC_MINT_ACCEPT_SPAN	3		// param level acceptance span
#define CC_MINT_BLOCK_SEC	1		// seconds per block
#endif

#define CC_MINT_ZKKEY_ID	15

#define CC_MINT_FOUNDATION_POOL	2

extern uint16_t genesis_nwitnesses;
extern uint16_t genesis_maxmal;
extern std::atomic<unsigned> max_mint_level;