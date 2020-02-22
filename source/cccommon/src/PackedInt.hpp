/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * PackedInt.hpp
*/

#pragma once

template <unsigned N, unsigned div = 1, unsigned offset = 0>
class PackedUnsigned
{
	uint8_t value[N];

public:

	void SetValue(uint64_t v)
	{
		v /= div;

		if (v > offset/div)
			v -= offset/div;
		else
			v = 0;

		memcpy(&value, &v, sizeof(value));
	}

	uint64_t GetValue() const
	{
		uint64_t v = 0;
		memcpy(&v, &value, sizeof(value));

		v += offset/div;
		v *= div;

		return v;
	}
};
