/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCticks.cpp
*/

#include "CCticks.hpp"
#include <CCassert.h>

using namespace std;

uint32_t ccticks(clockid_t clock_id)
{
	timespec ts;

	auto rc = clock_gettime(clock_id, &ts);
	CCASSERTZ(rc);

	return ts.tv_sec * CCTICKS_PER_SEC + (ts.tv_nsec + 1000 * 1000 * 1000 / CCTICKS_PER_SEC / 2) / (1000 * 1000 * 1000 / CCTICKS_PER_SEC);
}

int32_t ccticks_elapsed(uint32_t t0, uint32_t t1)
{
	int32_t diff = t1 - t0;

	if (diff < 0)
		return 0;
	else
		return diff;
}
