/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * CCcrypto.cpp
 *
*/

#include "CCdef.h"
#include "CCcrypto.hpp"
#include "SpinLock.hpp"

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
	static int fd_rnd = -1;

	if (fd_rnd == -1)
		fd_rnd = open("/dev/urandom", O_RDONLY);

	auto rc = read(fd_rnd, data, nbytes);
	CCASSERT(rc == (int)nbytes);
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

	for ( ; i + sizeof(mt19937_64::result_type) < nbytes; i += sizeof(mt19937_64::result_type))
	{
		*(mt19937_64::result_type*)((uint8_t*)data + i) = (*prnd)();
	}

	if (i < nbytes)
	{
		auto rnd = (*prnd)();

		memcpy((uint8_t*)data + i, &rnd, nbytes - i);
	}
}

#if defined(__cplusplus)
extern "C"
#endif
void PseudoRandomLetters(void *data, unsigned nbytes)
{
	CCPseudoRandom(data, nbytes);

	for (unsigned i = 0; i < nbytes; ++i)
	{
		auto p = (uint8_t*)data + i;

		unsigned letter = *p;

		letter %= 26*2;
		letter += 'A';

		if (letter > 'Z')
			letter += 'a' - 'Z' - 1;

		//cerr << "PseudoRandomLetters " << i << " " << letter << endl;

		*p = letter;
	}
}
