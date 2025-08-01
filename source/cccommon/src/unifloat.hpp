/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * unifloat.hpp
*/

// A "universal" float implemented using integer operations only so the results don't depend on the CPU/compiler implemention of float

#pragma once

#define UNIFLOAT_BITS				(32)
#define UNIFLOAT_EXPONENT_BITS		(11)
#define UNIFLOAT_MATISSA_BITS		(UNIFLOAT_BITS - UNIFLOAT_EXPONENT_BITS)
#define UNIFLOAT_WIRE_BYTES			((int)((UNIFLOAT_BITS + 7) / 8))

#pragma pack(push, 1)

struct UniFloatTuple
{
	uint64_t mant;
	int16_t  exp;
	int16_t  sign;

	string DebugString() const;
};

#pragma pack(pop)

class UniFloat
{
	double val;

	static UniFloat Compose(const double v)
	{
		UniFloat a;
		a.val = v;
		return a;
	}

public:

	double asFloat() const
	{
		return val;
	}

	string asFullString(unsigned precision = 0) const;

	string asRoundedString(int rounding = 0, unsigned precision = 0) const;

	UniFloat()
	 : val(0)
	{ }

	UniFloat(double v, int rounding = 0)
	{
		*this = Recompose(Decompose(v, 0, rounding));
	}

	UniFloat(const UniFloat& other)
	 : val(other.val)
	{ }

	static int64_t WireEncode(const UniFloat& a, int rounding = 0, bool allow_zero = true)
	{
		return WireEncode(a.val, rounding, allow_zero);
	}

	static int64_t WireEncode(const double& v, int rounding = 0, bool allow_zero = true);
	static UniFloat WireDecode(uint64_t v, int64_t increment = 0, bool allow_zero = true, uint64_t *mantissa = NULL, int64_t *exponent = NULL);

	static UniFloatTuple Decompose(const UniFloat& a, unsigned nbits = 0, int rounding = 0, int increment = 0)
	{
		return Decompose(a.val, nbits, rounding, increment);
	}

	static UniFloatTuple Decompose(const double& v, unsigned nbits = 0, int rounding = 0, int increment = 0);
	static UniFloat Recompose(const UniFloatTuple& v);

	static UniFloat Round(const UniFloat& a, int rounding = 0);

	static UniFloat Add(const UniFloat& a, const UniFloat& b, int rounding = 0, bool average = false);

	static UniFloat Average(const UniFloat& a, const UniFloat& b, int rounding = 0)
	{
		return Add(a, b, rounding, true);
	}

	static UniFloat Multiply(const UniFloat& a, const UniFloat& b, int rounding = 0);
	static UniFloat Divide(const UniFloat& a, const UniFloat& b, int rounding = 0);
	static UniFloat Power(const UniFloat& a, int b);

	static UniFloat ApplySign(int sign, const UniFloat& a);

	UniFloat operator- () const
	{
		return Compose(-val);
	}

	UniFloat& operator= (const UniFloat& other)
	{
		val = other.val;
		return *this;
	}

	bool operator== (const UniFloat& other) const
	{
		return val == other.val;
	}

	bool operator!= (const UniFloat& other) const
	{
		return val != other.val;
	}

	bool operator< (const UniFloat& other) const
	{
		return val < other.val;
	}

	bool operator<= (const UniFloat& other) const
	{
		return val <= other.val;
	}

	bool operator> (const UniFloat& other) const
	{
		return val > other.val;
	}

	bool operator>= (const UniFloat& other) const
	{
		return val >= other.val;
	}

	bool operator== (const double& v) const
	{
		return val == v;
	}

	bool operator!= (const double& v) const
	{
		return val != v;
	}

	bool operator< (const double& v) const
	{
		return val < v;
	}

	bool operator<= (const double& v) const
	{
		return val <= v;
	}

	bool operator> (const double& v) const
	{
		return val > v;
	}

	bool operator>= (const double& v) const
	{
		return val >= v;
	}

	static bool CheckLE(const UniFloat& a, const UniFloat& b);
	static bool CheckGE(const UniFloat& a, const UniFloat& b)
	{
		return CheckLE(b, a);
	}

	static void Test();
};

ostream& operator<< (ostream& os, const UniFloat& a);
