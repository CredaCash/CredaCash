/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * CCticks.cpp
*/

#include "CCdef.h"
#include "CCticks.hpp"

uint32_t ccticks(clockid_t clock_id)
{
	timespec ts;

	auto rc = clock_gettime(clock_id, &ts);
	CCASSERTZ(rc);

	return ts.tv_sec * CCTICKS_PER_SEC + (ts.tv_nsec + 1000 * 1000 * 1000 / CCTICKS_PER_SEC / 2) / (1000 * 1000 * 1000 / CCTICKS_PER_SEC);
}

uint32_t ccticksnz(uint32_t non_zero, clockid_t clock_id)
{
	auto ticks = ccticks(clock_id);

	if (!ticks)
		return non_zero;

	return ticks;
}

int32_t ccticks_elapsed(uint32_t t0, uint32_t t1)
{
	int32_t diff = t1 - t0;

	if (diff < 0)
		return 0;
	else
		return diff;
}

void ccticks_test()
{
	for (unsigned i = 0; i < 100; ++i)
	{
		auto t0 = ccticks();
		auto t1 = t0;
		while (t1 == t0)
			t1 = ccticks();
		auto elapsed = ccticks_elapsed(t0, t1);
		cerr << "ccticks_test elapsed " << elapsed << endl;
	}
}