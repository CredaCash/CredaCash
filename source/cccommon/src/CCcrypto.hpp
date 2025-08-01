/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCcrypto.hpp
*/

#pragma once

//#define TEST_EXTRA_RANDOM	1	// use for keygen

#ifndef TEST_EXTRA_RANDOM
#define TEST_EXTRA_RANDOM	0	// don't
#endif

#if defined(__cplusplus)
extern "C" {
#endif

void CCRandom(void *data, unsigned nbytes);
void CCPseudoRandomInit(void *pseed = NULL);
void CCPseudoRandomDeInit();
void CCPseudoRandom(void *data, unsigned nbytes);
void PseudoRandomLetters(void *data, unsigned nbytes);

int ComputePOW(const void *data, unsigned nbytes, uint64_t difficulty, uint64_t deadline, uint64_t& nonce);
int CheckPOW(const void *data, unsigned nbytes, uint64_t difficulty, uint64_t nonce);

#if TEST_EXTRA_RANDOM
void CCCollectEntropy();
#endif

#if defined(__cplusplus)
}
#endif
