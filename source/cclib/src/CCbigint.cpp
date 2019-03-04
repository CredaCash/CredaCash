/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * CCbigint.cpp
*/

#include "cclib.h"
#include "CCbigint.hpp"

bool bigint_bit(const bigint_t& bigval, unsigned bit)
{
	unsigned bpl = sizeof(BIGWORD(bigval, 0)) * CHAR_BIT;
	unsigned word = bit / bpl;
	unsigned shift = bit - word * bpl;
	unsigned result = 0;

	if (word < bigval.numberLimbs())
		result = (bool)(BIGWORD(bigval, word) & ((mp_limb_t)1 << shift));

	//cerr << "bigint_bit " << hex << bigval << dec << " bit " << bit << " shift " << shift << " = " << result << endl;

	return result;
}

void bigint_shift_up(bigint_t& bigval, unsigned nbits)
{
	//cerr << "bigint_shift_up nbits " << nbits << " in  " << hex << bigval << dec << endl;

	unsigned bpl = sizeof(BIGWORD(bigval, 0)) * CHAR_BIT;

	while (nbits >= bpl)
	{
		for (int i = bigval.numberLimbs() - 1; i >= 1; --i)
			BIGWORD(bigval, i) = BIGWORD(bigval, i - 1);

		BIGWORD(bigval, 0) = 0;

		nbits -= bpl;
	}

	for (int i = bigval.numberLimbs() - 1; i >= 0 && nbits; --i)
	{
		BIGWORD(bigval, i) <<= nbits;

		if (i)
			BIGWORD(bigval, i) |= (BIGWORD(bigval, i - 1) >> (bpl - nbits));
	}

	//cerr << "bigint_shift_up nbits " << nbits << " out " << hex << bigval << dec << endl;
}

void bigint_shift_down(bigint_t& bigval, unsigned nbits)
{
	//cerr << "bigint_shift_down nbits " << nbits << " in  " << hex << bigval << dec << endl;

	unsigned bpl = sizeof(BIGWORD(bigval, 0)) * CHAR_BIT;

	while (nbits >= bpl)
	{
		for (unsigned i = 0; i < bigval.numberLimbs() - 1; ++i)
			BIGWORD(bigval, i) = BIGWORD(bigval, i + 1);

		BIGWORD(bigval, bigval.numberLimbs() - 1) = 0;

		nbits -= bpl;
	}

	for (unsigned i = 0; i < bigval.numberLimbs() && nbits; ++i)
	{
		BIGWORD(bigval, i) >>= nbits;

		if (i < bigval.numberLimbs() - 1)
			BIGWORD(bigval, i) |= (BIGWORD(bigval, i + 1) << (bpl - nbits));
	}

	//cerr << "bigint_shift_down nbits " << nbits << " out " << hex << bigval << dec << endl;
}

void bigint_mask(bigint_t& bigval, unsigned nbits)
{
	//cerr << "bigint_mask nbits " << nbits << " in  " << hex << bigval << dec << endl;

	unsigned bpl = sizeof(BIGWORD(bigval, 0)) * CHAR_BIT;

	for (unsigned i = 0; i < bigval.numberLimbs(); ++i)
	{
		if (i * bpl >= nbits)
			BIGWORD(bigval, i) = 0;
		else if ((i + 1) * bpl > nbits)
		{
			auto mask = ((mp_limb_t)1 << (nbits - i * bpl)) - 1;

			//cerr << "bigint_mask i " << i << " bpl " << bpl << " nbits " << nbits << " mask " << hex << mask << dec << endl;

			BIGWORD(bigval, i) &= mask;
		}
	}

	//cerr << "bigint_mask nbits " << nbits << " out " << hex << bigval << dec << endl;
}

unsigned bigint_bytes_in_use(bigint_t& bigval)
{
	//cerr << "bigint_bytes_in_use " << hex << bigval << dec << endl;

	for (int i = bigval.numberLimbs() * sizeof(BIGWORD(bigval, 0)) - 1; i >= 0; --i)
	{
		if (*(((uint8_t*)&bigval) + i))
			return i + 1;
	}

	return 0;
}

void bigint_test()
{
	cerr << "bigint_test" << endl;

	bigint_t bigval;

	for (unsigned i = 0; i < 300; ++i)
		CCASSERTZ(bigint_bit(bigval, i));

	for (unsigned i = 0; i < bigval.numberLimbs(); ++i)
		BIGWORD(bigval, i) = -1;

	for (unsigned i = 0; i < 300; ++i)
		CCASSERT(bigint_bit(bigval, i) == (i < 256));

	for (unsigned j = 0; j < 300; ++j)
	{
		bigval = 1UL;
		bigint_shift_up(bigval, j);

		for (unsigned i = 0; i < 300; ++i)
			CCASSERT(bigint_bit(bigval, i) == (i < 256 && i == j));
	}

	for (unsigned i = 0; i < 2000; ++i)
	{
		bigint_t r;
		r.randomize();

		for (unsigned j = 0; j < 256; ++j)
		{
			bigval = r;
			bigint_shift_up(bigval, j);

			for (unsigned i = 0; i < 256; ++i)
				CCASSERT(bigint_bit(bigval, i) == bigint_bit(r, i - j));
		}

		for (unsigned j = 0; j < 256; ++j)
		{
			bigval = r;
			bigint_shift_down(bigval, j);

			for (unsigned i = 0; i < 256; ++i)
				CCASSERT(bigint_bit(bigval, i) == bigint_bit(r, i + j));
		}

		for (unsigned j = 0; j < 256; ++j)
		{
			bigval = r;
			bigint_mask(bigval, j);

			for (unsigned i = 0; i < 256; ++i)
				CCASSERT(bigint_bit(bigval, i) == (bigint_bit(r, i) && i < j));
		}
	}

	cerr << "bigint_test done" << endl;
}
