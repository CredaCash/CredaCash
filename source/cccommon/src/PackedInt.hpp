/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * PackedInt.hpp
*/

#pragma once

template <unsigned N, unsigned div = 1, unsigned offset = 0>
class PackedUnsigned
{
	uint8_t value[N];

public:

	uint64_t ExtractValue()
	{
		uint64_t v = 0;

		memcpy(&v, &value, sizeof(value));

		return v;
	}

	void ImplantValue(const void *v)
	{
		memcpy(&value, v, sizeof(value));
	}

	void SetValue(uint64_t v)
	{
		v /= div;

		if (v > offset/div)
			v -= offset/div;
		else
			v = 0;

		ImplantValue(&v);
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
