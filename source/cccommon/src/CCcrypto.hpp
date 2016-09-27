/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCcrypto.hpp
*/

#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

void CCRandom(void *data, unsigned nbytes);

void CCPseudoRandom(void *data, unsigned nbytes);

#if defined(__cplusplus)
}

#include <string>

std::string PseudoRandomLetters(unsigned len);

#endif


