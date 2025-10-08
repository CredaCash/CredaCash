/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCcrypto.cpp
 *
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "CCcrypto.hpp"
#include "SpinLock.hpp"

#include <siphash/siphash.h>

#if defined(__cplusplus)
extern "C"
#endif
#if TEST_EXTRA_RANDOM
void OSRandom(void *data, unsigned nbytes)
#else
void CCRandom(void *data, unsigned nbytes)
#endif
{
#ifdef _WIN32
	static HCRYPTPROV hProvider = 0;
	static FastSpinLock rnd_lock(__FILE__, __LINE__);

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

thread_local static mt19937_64 *prnd = NULL;

#if defined(__cplusplus)
extern "C"
#endif
void CCPseudoRandomDeInit()
{
	if (prnd)
	{
		delete prnd;

		prnd = NULL;
	}
}

#if defined(__cplusplus)
extern "C"
#endif
void CCPseudoRandomInit(void *pseed)
{
	CCPseudoRandomDeInit();

	if (pseed)
		prnd = new mt19937_64(*(mt19937_64::result_type*)pseed);
	else
	{
		mt19937_64::result_type seed;
		CCRandom(&seed, sizeof(seed));

		prnd = new mt19937_64(seed);
	}
}

#if defined(__cplusplus)
extern "C"
#endif
void CCPseudoRandom(void *data, unsigned nbytes)
{
	static FastSpinLock rnd_lock(__FILE__, __LINE__);
	lock_guard<FastSpinLock> lock(rnd_lock);

	if (!prnd)
		CCPseudoRandomInit();

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

#if defined(__cplusplus)
extern "C"
#endif
int ComputePOW(const void *data, unsigned nbytes, uint64_t difficulty, uint64_t deadline, uint64_t& nonce)
{
	if (!difficulty)
		return 0;

	//cerr << "ComputePOW size " << nbytes << " data " << buf2hex(data, nbytes) << endl;

	for (nonce = 0; nonce < (uint64_t)(-1) && !g_shutdown; ++nonce)
	{
		if (deadline && !(nonce & (((uint64_t)1 << 26) - 1)))
		{
			uint64_t t1 = unixtime();
			if (t1 > deadline)
				return 1;
			//cerr << "ComputePOW time " << t1 << endl;
		}

		auto hash = siphash(data, nbytes, &nonce, sizeof(nonce));

		if (hash < difficulty)
			return 0;
	}

	return -1;
}

#if defined(__cplusplus)
extern "C"
#endif
int CheckPOW(const void *data, unsigned nbytes, uint64_t difficulty, uint64_t nonce)
{
	if (!difficulty)
		return 0;

	//cerr << "CheckPOW   size " << nbytes << " data " << buf2hex(data, nbytes) << endl;

	auto hash = siphash(data, nbytes, &nonce, sizeof(nonce));

	if (hash < difficulty)
		return 0;

	return 1;
}

#if TEST_EXTRA_RANDOM

#include <blake2/blake2.h>

#define HASH_BYTES	(512/8)

static blake2b_ctx rnd_hash_context;

#if defined(__cplusplus)
extern "C"
#endif
void CCRandom(void *data, unsigned nbytes)
{
	OSRandom(data, nbytes);

	static uint64_t counter;
	blake2b_ctx copy;
	uint8_t hash[HASH_BYTES];

	++counter;
	memcpy(&copy, &rnd_hash_context, sizeof(rnd_hash_context));

	blake2b_update(&copy, &counter, sizeof(counter));
	blake2b_final(&copy, &hash);

	if (nbytes > HASH_BYTES)
		nbytes = HASH_BYTES;

	for (unsigned i = 0; i < nbytes; ++i)
		*((uint8_t*)data + i) ^= hash[i];

	//cerr << "CCRandom " << counter << " " << buf2hex(data, nbytes) << endl;
}

// ODROID-C2 GPIO

#define	GPIO_REG_MAP			0xC8834000
#define	BLOCK_SIZE				(4*1024)
#define	BIT(x)					(1 << (x))

#define GPIO_BANKS_OFFSET		136
#define	GPIOY_PIN_START			(GPIO_BANKS_OFFSET + 75)
#define GPIOY_INP_REG_OFFSET	0x111

static volatile uint32_t *gpio = 0;

#ifndef _WIN32

#include <sys/mman.h>

#else
#define O_SYNC		0	// to allow compilation under Windows
#define O_CLOEXEC	0
#define PROT_READ	0
#define PROT_WRITE	0
#define MAP_SHARED	0
#define mmap(...)	0
#endif

static void gpio_init(void)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);

	if (fd < 0)
	{
		cerr << "error openining /dev/mem" << endl;
		exit(-1);
		throw exception();
	}

	gpio = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_REG_MAP);

	if (gpio == (uint32_t *)(-1))
	{
		cerr << "mmap error" << endl;
		exit(-1);
		throw exception();
	}
}

static int get_gpio(int pin)
{
	if (!gpio) gpio_init();

	return *(gpio + GPIOY_INP_REG_OFFSET) & BIT(pin - GPIOY_PIN_START) ? 1 : 0;
}

static void collect_gpio(uint8_t *data, unsigned ndata)
{
	unsigned i = 0, n = 0;
	int state = 1;
	unsigned counter = 0;
	uint32_t t1, t0 = 0;
	unsigned tcheck = 0xffffff;
	unsigned tchecks = 0;

	while (true)
	{
		if ((++counter & tcheck) == 0 && t0)
		{
			++tchecks;

			t1 = ccticks();
			if (ccticks_elapsed(t0, t1) > 20 * CCTICKS_PER_SEC)
				break;
		}

		int ns = get_gpio(214);

		if (state != ns)
		{
			*(uint16_t*)&data[i] ^= counter;
			i += 2;
			if (i >= ndata)
				i = 0;
			++n;

			//cerr << counter << " ";

			state = ns;

			if (!t0)
				t0 = ccticks();
		}
	}

	cerr << "CCCollectEntropy collected " << n << " samples; " << ((uint64_t)tcheck + 1) * tchecks << " counts in " << ccticks_elapsed(t0, t1) << " millisecs" << endl;
}

#if defined(__cplusplus)
extern "C"
#endif
void CCCollectEntropy()
{
	cerr << "CCCollectEntropy started" << endl;

	blake2b_ctx gpio;

	int rc = blake2b_init(&rnd_hash_context, HASH_BYTES, NULL, 0);
	CCASSERTZ(rc);

	collect_gpio((uint8_t*)&gpio, sizeof(gpio));

	blake2b_update(&rnd_hash_context, &gpio, sizeof(gpio));

	cerr << "CCCollectEntropy done" << endl;
}

#endif // TEST_EXTRA_RANDOM
