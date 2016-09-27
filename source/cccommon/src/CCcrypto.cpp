/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCcrypto.cpp
 *
*/

#include "CCcrypto.hpp"
#include "SpinLock.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Wincrypt.h>
#endif

#include <random>
#include <string>
#include <iostream>

using namespace std;

#if defined(__cplusplus)
extern "C"
#endif
void CCRandom(void *data, unsigned nbytes)
{
#ifdef _WIN32
	static HCRYPTPROV hProvider = 0;
	static FastSpinLock rnd_lock;

	{
		lock_guard<FastSpinLock> lock(rnd_lock);

		if (!hProvider)
		{
			auto rc = ::CryptAcquireContextW(&hProvider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT);
			CCASSERT(rc);
			CCASSERT(hProvider);
		}
	}

	auto rc = ::CryptGenRandom(hProvider, nbytes, (BYTE*)data);
	CCASSERT(rc);
#else
	auto rc = getrandom(data, nbytes, 0);
	CCASSERT(rc > 0);
#endif
}

#if defined(__cplusplus)
extern "C"
#endif
void CCPseudoRandom(void *data, unsigned nbytes)
{
	static thread_local mt19937_64 *prnd = NULL;

	if (!prnd)
	{
		mt19937_64::result_type seed;
		CCRandom(&seed, sizeof(seed));

		prnd = new mt19937_64(seed);
	}

	unsigned i = 0;

	for ( ; i < nbytes; i += sizeof(mt19937_64::result_type))
	{
		*(mt19937_64::result_type*)((uint8_t*)data + i) = (*prnd)();
	}

	if (i < nbytes)
	{
		auto rnd = (*prnd)();

		memcpy((uint8_t*)data + i, &rnd, nbytes - i);
	}
}

string PseudoRandomLetters(unsigned len)
{
	string str(len, 0);

	CCPseudoRandom((void*)str.data(), len);

	for (unsigned i = 0; i < len; ++i)
	{
		unsigned letter = str[i];

		letter &= 255;
		letter %= 26*2;
		letter += 'A';

		if (letter > 'Z')
			letter += 'a' - 'Z' - 1;

		//cerr << "PseudoRandomLetters " << i << " " << letter << endl;

		str[i] = letter;
	}

	return str;
}
