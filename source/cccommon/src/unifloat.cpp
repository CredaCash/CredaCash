/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * unifloat.cpp
*/

#include "CCdef.h"
#include "unifloat.hpp"

#include <float.h>
#include <math.h>

//#define TRACE			1	// for debugging
//#define TRACE_WIRE	1	// for debugging

//#define TEST_UNIFLOAT	1	// for testing

#ifndef TRACE
#define TRACE			0	// don't debug
#endif

#ifndef TRACE_WIRE
#define TRACE_WIRE		0	// don't debug
#endif

#ifndef TEST_UNIFLOAT
#define TEST_UNIFLOAT		0	// don't test
#endif

#define HEX hex

#define UNIFLOAT_COMPUTATION_BITS		52
#define UNIFLOAT_DECIMAL_PRECISION		17 // this must be >= ceil(UNIFLOAT_COMPUTATION_BITS * log10(2) + 1) so a UniFloat can be converted to a decimal text string and back without changing value

string UniFloatTuple::DebugString() const
{
	ostringstream out;

	out << " sign " << sign;
	out << " mant " << HEX << mant << dec;
	out << " exp " << exp;

	return out.str();
}

ostream& operator<< (ostream& os, const UniFloat& a)
{
	os << a.asFullString();
	return os;
}

// Converts to a string with "precision" digits
string UniFloat::asFullString(unsigned precision) const
{
	if (!precision)
		precision = UNIFLOAT_DECIMAL_PRECISION;

	auto v = asFloat();

	if (v < 0)
		v = -v;

	// prevent JSONCPP parsing overflows
	double max = 0;
	if (precision <= 1)
		max = 1e+308;
	if (precision == 2)
		max = 1.7e+308;
	if (precision == 3)
		max = 1.79e+308;
	if (precision == 4)
		max = 1.797e+308;
	if (precision == 5)
		max = 1.7976e+308;
	if (precision == 6)
		max = 1.79769e+308;
	if (precision == 7)
		max = 1.797693e+308;
	if (precision == 8)
		max = 1.7976931e+308;
	if (precision == 9)
		max = 1.79769313e+308;
	if (precision == 10)
		max = 1.797693134e+308;
	if (precision == 11)
		max = 1.7976931348e+308;
	if (precision == 12)
		max = 1.79769313486e+308;
	if (precision == 13)
		max = 1.797693134862e+308;
	if (precision == 14)
		max = 1.7976931348623e+308;
	if (precision == 15)
		max = 1.79769313486231e+308;
	if (precision >= 16)
		max = 1.797693134862315e+308; // note ends in 5 instead of 6

	if (max && v > max)
		v = max;

	if (asFloat() < 0)
		v = -v;

	ostringstream s;
	s.precision(precision);
	s << v;

	//cout << asFloat() << " precision " << precision << " = " << s.str() <<endl;

	return s.str();
}

// Converts to a string with "precision" digits *after* the decimal point
//	rounding > 0: UniFloat(return value) >= this UniFloat
//	rounding < 0: UniFloat(return value) <= this UniFloat
string UniFloat::asRoundedString(int rounding, unsigned precision) const
{
	if (!precision)
		precision = UNIFLOAT_DECIMAL_PRECISION;

	auto scale = pow((double)10, precision);

	auto base = val * scale;

	if (rounding == 0) base = round(base);
	if (rounding > 0)  base = floor(base) - 1;
	if (rounding < 0)  base =  ceil(base) + 1;

	for (unsigned i = 0; i < 100 && isfinite(base); ++i)
	{
		ostringstream s;
		s.precision(precision);
		s << (base / scale);
		auto rv = s.str();

		if (rv.length() > 1 && !(rv[0] == '0' || (rv[0] == '-' && rv[1] == '0')))
		{
			auto pt = rv.find('.');

			if (pt != string::npos)
			{
				ostringstream s;
				s.precision(precision + pt - (rv[0] == '-'));
				s << (base / scale);
				rv = s.str();
			}
		}

		if (!rounding)
			return rv;

		double check;

		try
		{
			check = stod(rv);
		}
		catch (...)
		{
			break;
		}

		if ((rounding > 0 && UniFloat(check) >= *this) || (rounding < 0 && UniFloat(check) <= *this))
		{
			//cout << i << " UniFloat::asString " << *this << " = " << rv << endl;

			return rv;
		}

		base += (rounding > 0 ? 1 : -1);
	}

	return "error";
}

// Encode Float:
//   exponent = max(0, floor(log2(rate)) + 2^(UNIFLOAT_EXPONENT_BITS - 1)
//		check: exponent < 2^XCX_RATE_EXPONENT_BITS
//   mantissa = ceil(rate / 2^(exponent - 2^(UNIFLOAT_EXPONENT_BITS - 1) - UNIFLOAT_MATISSA_BITS)) - 2^XCX_RATE_MATISSA_BITS
//		check: mantissa >= 0 and mantissa < 2^XCX_RATE_MATISSA_BITS
// Decode Float:
//   rate = (2^XCX_RATE_MATISSA_BITS + mantissa) * 2^(exponent - 2^(UNIFLOAT_EXPONENT_BITS - 1) - UNIFLOAT_MATISSA_BITS)

int64_t UniFloat::WireEncode(const double& v, int rounding, bool allow_zero)
{
	if (TRACE_WIRE) cout << "UniFloat::WireEncode " << v << " rounding " << rounding << " thread " << cc_thread_id() << endl;

	if (!v && allow_zero)
		return 0;

	if (v <= 0)
		throw range_error("invalid value");

	auto maxexp = (int64_t)1 << (UNIFLOAT_EXPONENT_BITS - 1);
	auto manhi  = (uint64_t)1 << UNIFLOAT_MATISSA_BITS;

	double roundby = 3.5;

	while (true)
	{
		--roundby;

		if (roundby < -3)
			throw runtime_error("rounding failed");

		auto exponent = (int64_t)floor(log2(v)) - 2;	// the starting point
		auto mantissa = manhi;

		while (mantissa >= manhi)
		{
			++exponent;
			auto m = v / pow((double)2, exponent);
			m = m * pow((double)2, UNIFLOAT_MATISSA_BITS) - manhi;
			if (rounding > 0)
				m -= roundby;			// rounding up, so start low
			else if (rounding < 0)
				m = ceil(m + roundby);	// rounding down, so start high
			else
				m += 0.5;				// round to nearest
			mantissa = (uint64_t)m;

			if (TRACE_WIRE) cout << "UniFloat::WireEncode " << v << " rounding " << rounding << " roundby " << roundby << " m " << m << " mantissa " << mantissa << " manhi " << manhi << " thread " << cc_thread_id() << endl;
			if (TRACE_WIRE) cout << "UniFloat::WireEncode " << v << " exponent " << exponent << " maxexp " << maxexp << " thread " << cc_thread_id() << endl;

			if (exponent > maxexp + 3)
				break;
		}

		if (exponent >= maxexp)
		{
			if (roundby > -2)
				continue;

			if (rounding > 0)
				throw range_error("value exceeds limits");

			exponent = maxexp - 1;
			mantissa = manhi - 1;
		}

		if (exponent < -maxexp)
		{
			if (roundby > -2)
				continue;

			if (rounding >= 0)
				return 1;

			if (allow_zero)
				return 0;

			throw range_error("value exceeds limits");
		}

		auto rv = ((exponent + maxexp) << UNIFLOAT_MATISSA_BITS) | mantissa;

		if (rounding)
		{
			auto check = WireDecode(rv);

			if (rounding > 0 && check < v)
				continue;
			if (rounding < 0 && check > v)
				continue;
		}

		if (TRACE_WIRE) cout << "UniFloat::WireEncode " << v << " result " << hex << rv << dec << " thread " << cc_thread_id() << endl;

		return rv;
	}
}

UniFloat UniFloat::WireDecode(uint64_t v, int64_t increment, bool allow_zero, uint64_t *pmantissa, uint64_t *pexponent)
{
	if (allow_zero && !v && !increment)
	{
		if (TRACE_WIRE) cout << "UniFloat::WireDecode " << v << " thread " << cc_thread_id() << endl;

		return 0;
	}

	auto maxexp = (uint64_t)1 << (UNIFLOAT_EXPONENT_BITS - 1);
	auto manhi  = (uint64_t)1 << UNIFLOAT_MATISSA_BITS;

	auto exponent = (v >> UNIFLOAT_MATISSA_BITS);
	if (TRACE_WIRE) cout << "UniFloat::WireDecode " << hex << v << dec << " exponent " << exponent << " thread " << cc_thread_id() << endl;
	if (exponent >= 2*maxexp)
		throw range_error("invalid value");

	auto mantissa = v & (manhi - 1);

	if (increment < 0 && (uint64_t)-increment > mantissa && !exponent)
	{
		if (pmantissa) *pmantissa = -1;
		if (pexponent) *pexponent = -1;

		if (TRACE_WIRE) cout << "UniFloat::WireDecode " << hex << v << dec << " increment " << increment << " + mantissa " << mantissa << " < 0" << " thread " << cc_thread_id() << endl;

		return -DBL_MAX;
	}

	mantissa += increment;

	if (mantissa || exponent) mantissa += manhi;

	while (mantissa >= 2*manhi)
	{
		mantissa >>= 1;
		++exponent;

		if (exponent > maxexp)
		{
			if (pmantissa) *pmantissa = -1;
			if (pexponent) *pexponent = -1;

			if (TRACE_WIRE) cout << "UniFloat::WireDecode " << hex << v << dec << " increment " << increment << " mantissa " << mantissa << " exponent " << exponent << " > maxexp " << maxexp << " thread " << cc_thread_id() << endl;

			return DBL_MAX;
		}
	}

	if (pmantissa) *pmantissa = mantissa;
	if (pexponent) *pexponent = exponent;

	auto result = mantissa * pow((double)2, (int64_t)(exponent - maxexp - UNIFLOAT_MATISSA_BITS));
	if (TRACE_WIRE) cout << "UniFloat::WireDecode " << hex << v << dec << " increment " << increment << " mantissa " << mantissa << " exponent " << exponent << " result " << result << " thread " << cc_thread_id() << endl;

	return result;
}

// Functions to do floating point computations using integers so the results do not depend on the implementation of type "double"

// TODO add a function to check assumptions

UniFloatTuple UniFloat::Decompose(const double& v, unsigned nbits, int rounding, int increment)
{
	#if TEST_UNIFLOAT
	static bool dotest = true;
	if (dotest)
	{
		dotest = false;
		Test();
	}
	#endif

	UniFloatTuple x;

	if (v == 0)
	{
		memset(&x, 0, sizeof(x));
		return x;
	}

	if (!nbits)
		nbits = UNIFLOAT_COMPUTATION_BITS;

	auto limit = (uint64_t)1 << nbits;	// nbits is the # of bits not including the sign bit

	CCASSERT(FLT_RADIX == 2);
	CCASSERT(nbits < 64);

	auto m = v;

	if (isinf(m))
		m = (m < 0 ? -DBL_MAX : DBL_MAX);

	int exp;

	m = frexp(m, &exp) * limit;

	if (m > 0)
		x.sign = 1;
	else
	{
		x.sign = -1;
		m = -m;
	}

	x.exp = exp - nbits;

	uint64_t& y = x.mant;

	while (true)
	{
		if (rounding < 0)
			y = floor(m);
		else if (rounding > 0)
			y = ceil(m);
		else
			y = round(m);

		y += x.sign * increment;

		if (y < limit)
			break;

		m /= 2;
		++x.exp;
	}

	if (TRACE) cout << "UniFloat::Decompose v " << HEX << v << dec << " nbits " << nbits << " rounding " << rounding << " increment " << increment << x.DebugString() << endl;

	CCASSERT(isfinite(m));

	return x;
}

UniFloat UniFloat::Recompose(const UniFloatTuple& x)
{
	auto v = x.sign * ldexp((double)x.mant, x.exp);

	if (v == HUGE_VAL)
		v = DBL_MAX;

	if (v == -HUGE_VAL)
		v = -DBL_MAX;

	if (TRACE) cout << "UniFloat::Recompose  v " << HEX << v << dec << x.DebugString() << endl;

	CCASSERT(isfinite(v));

	return Compose(v);
}

// check that a <= b, with an allowance for rounding errors
bool UniFloat::CheckLE(const UniFloat& a, const UniFloat& b)
{
	auto v = Decompose(b);

	v.mant += 2;

	auto c = Recompose(v);

	//cout << "UniFloat::CheckLE " << b << " --> " << c << endl;

	return a <= c;
}

UniFloat UniFloat::ApplySign(int sign, const UniFloat& a)
{
	if (sign > 0)
		return a;
	else if (sign < 0)
		return -a;
	else
		return Compose(0);
}

static uint64_t Shift(unsigned __int128 v, int shift, int rounding)
{
	CCASSERT(shift >= 0);

	uint64_t r;

	if (rounding < 0 || !shift)
	{
		// round down

		r = v >> shift;
	}
	else if (rounding > 0)
	{
		// round up

		auto up = v & (((unsigned __int128)1 << shift) - 1);

		r = v >> shift;

		if (up)
			++r;
	}
	else
	{
		// round nearest

		auto up = v & ((unsigned __int128)1 << (shift - 1));

		r = v >> shift;

		if (up)
			++r;
	}

	if (TRACE) cout << "UniFloat::Shift shift " << shift << " rounding " << rounding << " result " << HEX << r << dec << endl;

	return r;
}

static void Normalize(unsigned __int128 mant, UniFloatTuple &x, int rounding)
{
	if (TRACE) x.mant = mant;
	if (TRACE) cout << "UniFloat::Normalize in mant hi " << HEX << (uint64_t)(mant >> 64) << dec << x.DebugString() << endl;

	auto limit = (unsigned __int128)1 << UNIFLOAT_COMPUTATION_BITS;

	unsigned shift = 0;

	while ((mant >> shift) >= limit)
		++shift;

	x.mant = Shift(mant, shift, rounding);

	x.exp += shift;

	if (TRACE) cout << "UniFloat::Normalize out" << x.DebugString() << endl;
}

UniFloat UniFloat::Add(const UniFloat& a, const UniFloat& b, int rounding, bool average)
{
	int nbits = 8*sizeof(UniFloatTuple::mant) - 2;	// leave 1 bit for sign and 1 bit for overflow
	auto v = Decompose(a, nbits);
	auto x = Decompose(b, nbits);

	int diff = 0;

	if (v.mant && x.mant)
		diff = v.exp - x.exp;

	if (TRACE) cout << "UniFloat::Add v " << HEX << v.mant << " x " << x.mant << dec << " exp diff " << diff << endl;

	if (!x.mant || diff >= nbits)
	{
		//v = v;
	}
	else if (!v.mant || -diff >= nbits)
	{
		v = x;
	}
	else
	{
		if (diff > 0)
			x.mant = Shift(x.mant, diff, rounding);
		else if (diff < 0)
		{
			v.mant = Shift(v.mant, -diff, rounding);
			v.exp = x.exp;
		}

		if (TRACE) cout << "UniFloat::Add v " << HEX << v.mant << " x " << x.mant << dec << endl;

		auto j = v.sign * (int64_t)v.mant;
		auto k = x.sign * (int64_t)x.mant;

		auto y = j + k;

		if (!y)
			v.sign =  0;
		else if (y > 0)
			v.sign =  1;
		else
		{
			y = -y;
			v.sign = -1;
		}

		Normalize(y, v, rounding);
	}

	if (average)
		--v.exp;

	return Recompose(v);
}

UniFloat UniFloat::Multiply(const UniFloat& a, const UniFloat& b, int rounding)
{
	auto v = Decompose(a, 8*sizeof(UniFloatTuple::mant) - 1);
	auto x = Decompose(b, 8*sizeof(UniFloatTuple::mant) - 1);

	if (TRACE) cout << "UniFloat::Multiply v " << HEX << v.mant << " x " << x.mant << dec << endl;

	auto y = (unsigned __int128)v.mant * x.mant;

	v.sign *= x.sign;
	v.exp += x.exp;

	Normalize(y, v, rounding);

	return Recompose(v);
}

UniFloat UniFloat::Divide(const UniFloat& a, const UniFloat& b, int rounding)
{
	auto nbits = 8*sizeof(UniFloatTuple::mant) - 1;
	auto v = Decompose(a, nbits);
	auto x = Decompose(b, nbits, -rounding);

	if (TRACE) cout << "UniFloat::Divide v " << HEX << v.mant << " x " << x.mant << dec << endl;

	if (!x.mant)
		return (v.sign < 0 ? -DBL_MAX : DBL_MAX);

	auto y = (unsigned __int128)v.mant << (127 - nbits);

	if (!rounding)
		y += (x.mant + 1) / 2;	// round nearest
	else if (rounding > 0)
		y += x.mant - 1;		// round up

	y /= x.mant;

	v.sign *= x.sign;
	v.exp -= x.exp + 127 - nbits;

	Normalize(y, v, rounding);

	return Recompose(v);
}

UniFloat UniFloat::Power(const UniFloat& a, int b)
{
	// example: when b = 21 = binary 10101 = 16 + 4 + 1 --> a^b = a^21 = a^16 * a^4 * a^1

	UniFloat v = 1;
	UniFloat fac = a;

	if (b < 0)
		return 0;

	if (b == 0)
		return 1;

	while (true)
	{
		if (b & 1)
			v = Multiply(v, fac);

		//cout << "UniFloat::Power " << a << " ^ " << b << " fac " << fac << " v " << v << endl;

		b >>= 1;

		if (!b)
			break;

		fac = Multiply(fac, fac);
	}

	return v;
}

#if TEST_UNIFLOAT

#include <boost/multiprecision/cpp_dec_float.hpp>

void UniFloat::Test()
{
	cerr << "UniFloat::Test" << endl;

	#if 1
	cout << DBL_MAX << endl;
	cout << UniFloat(DBL_MAX) << endl;
	cout << DBL_MAX - UniFloat(DBL_MAX).asFloat() << endl;
	cout << UniFloat::Add(1, 0) << endl;
	cout << UniFloat::Add(0, 1) << endl;
	cout << UniFloat::Add(1e200, 1e-200) << endl;
	cout << UniFloat::Add(1e-200, 1e200) << endl;
	cout << UniFloat::Add(-1, -2) << endl;
	cout << UniFloat::Add(-2, 1) << endl;
	cout << UniFloat::Add(1, -2) << endl;
	cout << UniFloat::Add(2, -1) << endl;
	cout << UniFloat::Add(-1, 2) << endl;
	cout << UniFloat::Add(1, 2) << endl;
	cout << UniFloat::Add((uint32_t)(-1), (uint32_t)(-1)) << endl;		// 8589934590.0
	cout << UniFloat::Multiply((uint32_t)(-1), (uint32_t)(-1)) << endl;	// 1.8446744065119617025e+19
	cout << UniFloat::Divide((uint32_t)(-1), 2, -1) << endl;			// 2147483647.5
	cout << UniFloat::Divide((uint32_t)(-1), 2, 0) << endl;
	cout << UniFloat::Divide((uint32_t)(-1), 2, 1) << endl;
	cout << UniFloat::Divide((uint32_t)(-1), 7, -1) << endl;			// 613566756.42857142857142857142857
	cout << UniFloat::Divide((uint32_t)(-1), 7, 0) << endl;
	cout << UniFloat::Divide((uint32_t)(-1), 7, 1) << endl;
	cout << UniFloat::Divide((uint32_t)(-1) - 1, (uint32_t)(-1)    , -1) << endl; // 0.99999999976716935629192026245685
	cout << UniFloat::Divide((uint32_t)(-1) - 1, (uint32_t)(-1)    ,  1) << endl;
	cout << UniFloat::Divide((uint32_t)(-1)    , (uint32_t)(-1)    , -1) << endl; // 1.0
	cout << UniFloat::Divide((uint32_t)(-1)    , (uint32_t)(-1)    ,  1) << endl;
	cout << UniFloat::Divide((uint32_t)(-1)    , (uint32_t)(-1) - 1, -1) << endl; // 1.0000000002328306437622898462053
	cout << UniFloat::Divide((uint32_t)(-1)    , (uint32_t)(-1) - 1,  1) << endl;
	#endif

	#define ITER 10000000
	#define RMAX 1024

	mt19937_64 rgen(time(NULL));
	#define rdist(x) generate_canonical<double, 64>(x)

	#define rnd() ldexp(rdist(rgen) - 0.5, (rand() % (2*RMAX + 1)) - RMAX)

	for (int round = -1; round <= 1; ++round)
	{
	for (int op = 0; op <= 4; ++op)
	{
		long double count = 0, count_hi = 0, count_lo = 0, total_hi = 0, total_lo = 0;

		long double mind = LDBL_MAX;
		long double maxd = -mind;

		for (int iter = 0; iter < ITER; ++iter)
		{
			long double v1 = rnd();
			long double v2 = rnd();
			long double v3, v4;

			if (fabs(v1) < DBL_MIN)
				v1 = DBL_MIN * ((rand() % 3) - 1);

			if (fabs(v2) < DBL_MIN)
				v2 = DBL_MIN * ((rand() % 3) - 1);

			//if (TRACE) cout << v1 << " " << v2 << endl;

			try
			{
			switch (op)
			{
			case 0:
				v3 = v1;
				v2 = 0;
				break;
			case 1:
				v3 = UniFloat(v1, round).asFloat();
				v2 = 0;
				break;
			case 2:
				v3 = UniFloat(v1).asFloat() + UniFloat(v2).asFloat();
				break;
			case 3:
				v3 = UniFloat(v1).asFloat() * UniFloat(v2).asFloat();
				break;
			case 4:
				v3 = UniFloat(v1).asFloat() / UniFloat(v2).asFloat();
				break;
			}
			}
			catch (...)
			{
				continue;
			}

			if (!isfinite(v3))
				continue;

			switch (op)
			{
			case 0:
				v4 =           UniFloat(v1, round).asFloat();
				break;
			case 1:
				v4 = UniFloat((double)(boost::multiprecision::cpp_dec_float_50)UniFloat(v1, round).asFullString()).asFloat();
				break;
			case 2:
				v4 =      UniFloat::Add(v1, v2, round).asFloat();
				break;
			case 3:
				v4 = UniFloat::Multiply(v1, v2, round).asFloat();
				break;
			case 4:
				v4 =   UniFloat::Divide(v1, v2, round).asFloat();
				break;
			}

			auto diff = (v3 ? (v4 - v3) / v3 : 0);

			//if (diff) cout << v1 << " " << v2 << " " << v3 << " " << v4 << " " << diff << endl;
			//CCASSERTZ(diff);

			++count;
			mind = min(mind, diff);
			maxd = max(maxd, diff);

			if (diff > 0)
			{
				++count_hi;
				total_hi += diff;
			}
			else if (diff < 0)
			{
				++count_lo;
				total_lo += diff;
			}
		}

		cout << "\nop " << op << " round " << round << " count " << count << endl;
		cout << "count_lo " << count_lo << " count_hi " << count_hi << endl;
		cout << "avg lo " << total_lo / count_lo << " avg hi " << total_hi / count_hi << endl;
		cout << "mind " << mind << " maxd " << maxd << endl;
	}
	}
}

#endif
