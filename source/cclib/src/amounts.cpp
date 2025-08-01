/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * amounts.cpp
*/

#include "cclib.h"
#include "amounts.h"
#include "CCparams.h"
#include "jsonutil.h"

#include <boost/math/special_functions/round.hpp>

#ifdef _WIN32
#define bswap_64	_bswap64
#else
#include <byteswap.h>
#endif

//#define TRACE_AMOUNTS	1
//#define TEST_AMOUNTS	1

#ifndef TRACE_AMOUNTS
#define TRACE_AMOUNTS	0 // don't test
#endif

#ifndef TEST_AMOUNTS
#define TEST_AMOUNTS	0 // don't test
#endif

namespace mm = boost::math;
using namespace snarkfront;

#define B63		((uint64_t)1 << 63)
#define I63		((amtint_t)B63)
#define F63		((amtfloat_t)B63)
#define I126	(I63 * I63)
#define F126	(F63 * F63)
#define I254	(I126 * I126 * 4)
#define F252	(F126 * F126)

// for 128 bit amounts:
//const amtint_t amount_int_max = I126 + (I126 - 1);
//const amtfloat_t amount_float_max = F126 * 2 - 1;

// for 256 bit amounts:
const amtint_t amount_int_max = I254 + (I254 - 1);
const amtfloat_t amount_float_max = F252 * 8 - 1;

#if TEST_AMOUNTS

static void test_experiment_convert_one(amtfloat_t vf)
{
	cerr << "\ntest_experiment_convert_one" << endl;

	amtint_t vi;
	//mp::checked_int256_t vi;
	//bigint_t vi;

	stringstream ss;

	ss.precision(1);
	ss << fixed;

	cout << "vf " << vf << endl;

	ss << vf;

	auto s = ss.str();
	cout << "ss " << s << endl;

	cout << "no exp " << (s.find_first_of("eE") == string::npos) << endl;

	auto dec = s.find('.');
	cout << "dec " << dec << endl;

	//if (dec != string::npos)
	//	s.erase(dec);

	cout << "ss " << s << endl;

	ss.str(s);
	cout << "ss " << ss.str() << endl;

	#if 1
	cout << "stringstream init" << endl;
	vi = 9UL;
	ss >> vi;
	cout << "vi " << vi << endl;
	#endif

	#if 0
	cout << "string init" << endl;
	vi = 9UL;
	vi = s;
	cout << "vi " << vi << endl;
	#endif

	#if 0
	cout << "parseint init" << endl;
	vi = 9UL;
	char output[128] = {0};
	uint32_t outsize = sizeof(output);
	auto rc = parse_int_value("", "", s, 2*TX_AMOUNT_DECODED_BITS, 0UL, vi, output, outsize);
	cout << "rc " << rc << endl;
	cout << "output: " << output << endl;
	cout << "vi " << vi << endl;
	#endif
}

static void test_experiment_conversion()
{
	string ss = "1234567891.1234567892123456789312345678941234567895123456789612345678971234567898";

	amtfloat_t vf = (amtfloat_t)ss;
	cout << "vf " << vf << endl;

	test_experiment_convert_one(mm::round(vf));
	test_experiment_convert_one(mm::round(vf * amtfloat_t("1E30")));
	test_experiment_convert_one(mm::round(vf * amtfloat_t("1E66")));
	test_experiment_convert_one(mm::round(vf * amtfloat_t("1E67")));
	//test_experiment_convert_one(mm::round(vf * amtfloat_t("1E68")));	// will throw exception with 256 bit checked ints
}

static int test_result_code(bool convert, bool pack_signed, bool pack_unsigned)
{
	return convert | ((convert || pack_signed) << 1) | ((convert || pack_unsigned) << 2);
}

static int test_one_amount(const bigint_t& v)
{
	cout << "\ntest_one_amount " << v << " = " << hex << v << dec << endl;

	bigint_t maxv, minv;

	if (1)
	{
		BIGWORD(maxv, 0) = UINT64_MAX;
		BIGWORD(maxv, 1) = UINT64_MAX;
		BIGWORD(maxv, 2) = UINT64_MAX;
		BIGWORD(maxv, 3) = ((uint64_t)1 << (255 - 3*64)) - 1;

		subBigInt(bigint_t(0UL), maxv, minv, false);

		//cerr << hex << " minv " << minv << " maxv " << maxv << dec << endl;
	}

	bigint_t maxup, maxp, minp;

	if (8*AMOUNT_UNSIGNED_PACKED_BYTES == 128)
	{
		BIGWORD(maxup, 0) = UINT64_MAX;
		BIGWORD(maxup, 1) = UINT64_MAX;
	}
	else
		CCASSERT(0);

	if (8*AMOUNT_SIGNED_PACKED_BYTES == 128)
	{
		BIGWORD(maxp, 0) = UINT64_MAX;
		BIGWORD(maxp, 1) = INT64_MAX;

		BIGWORD(minp, 1) = ((uint64_t)1 << 63);
		BIGWORD(minp, 2) = UINT64_MAX;
		BIGWORD(minp, 3) = UINT64_MAX;
	}
	else if (8*AMOUNT_SIGNED_PACKED_BYTES == 192)
	{
		BIGWORD(maxp, 0) = UINT64_MAX;
		BIGWORD(maxp, 1) = UINT64_MAX;
		BIGWORD(maxp, 2) = INT64_MAX;

		BIGWORD(minp, 2) = ((uint64_t)1 << 63);
		BIGWORD(minp, 3) = UINT64_MAX;
	}
	else if (8*AMOUNT_SIGNED_PACKED_BYTES == 256)
	{
		subBigInt(bigint_t(0UL), bigint_t(1UL), maxup, false);
		maxp = minp;
	}
	else
		CCASSERT(0);

	bigint_t b1, b2, b3, b4, b5, b6;
	amtint_t i1, i3, i4, i5, i6;
	amtfloat_t f2, f3, f4;
	packed_unsigned_amount_t p1, p2;
	packed_signed_amount_t p3, p4;
	const uint64_t asset = ASSET_NO_SCALE;
	int rc = 0;

	#define CATCH_AMOUNT_TEST_EXCEPTIONS

	#ifdef CATCH_AMOUNT_TEST_EXCEPTIONS
	try
	#endif
	{
		// bigint_t <--> amtint_t
		amount_from_bigint(v, i1);
		amount_to_bigint(i1, b1);
		if (TRACE_AMOUNTS) cout << "i1 " << i1 << " = " << hex << (i1 > 0 ? i1 : 0) << dec << endl;
		if (TRACE_AMOUNTS) cout << "b1 " << b1 << " = " << hex << b1 << dec << endl;

		// bigint_t <--> amtfloat_t
		amount_to_float(asset, v, f2);
		rc |= amount_from_float(asset, f2, b2);
		if (TRACE_AMOUNTS) cout << "f2 " << f2 << endl;
		if (TRACE_AMOUNTS) cout << "b2 " << b2 << " = " << hex << b2 << dec << endl;

		// amtint_t <--> amtfloat_t --> bigint_t
		amount_to_float(asset, i1, f3);
		rc |= amount_from_float(asset, f3, i3);
		rc |= amount_from_float(asset, f3, b3);
		if (TRACE_AMOUNTS) cout << "f3 " << f3 << endl;
		if (TRACE_AMOUNTS) cout << "i3 " << i3 << " = " << hex << (i3 > 0 ? i3 : 0) << dec << endl;
		if (TRACE_AMOUNTS) cout << "b3 " << b3 << " = " << hex << b3 << dec << endl;

		// amtfloat_t <--> amtint_t --> bigint_t
		rc |= amount_from_float(asset, f2, i4);
		amount_to_float(asset, i4, f4);
		amount_to_bigint(i4, b4);
		if (TRACE_AMOUNTS) cout << "i4 " << i4 << " = " << hex << (i4 > 0 ? i4 : 0) << dec << endl;
		if (TRACE_AMOUNTS) cout << "f4 " << f4 << endl;
		if (TRACE_AMOUNTS) cout << "b4 " << b4 << " = " << hex << b4 << dec << endl;
	}
	#ifdef CATCH_AMOUNT_TEST_EXCEPTIONS
	catch (...)
	{
		rc |= 1;
	}
	#endif

	bool isbad = (v > maxv && v < minv);
	cout << "test_one_amount_ isbad " << isbad << " (v > maxv) " << (v > maxv) << " (v < minv) " << (v < minv) << " rc " << rc << " amount " << hex << v << " maxv " << maxv << " minv " << minv << dec << endl;

	CCASSERTZ(!isbad && rc);
	CCASSERTZ(isbad && !rc);

	if (rc) return test_result_code(rc, 0, 0);

	CCASSERT(v == b1);
	CCASSERT(v == b2);
	CCASSERT(v == b3);
	CCASSERT(v == b4);

	CCASSERT(i1 == i3);
	CCASSERT(i1 == i4);

	CCASSERT(f2 == f3);
	CCASSERT(f2 == f4);

	int rcu = 0;

	#ifdef CATCH_AMOUNT_TEST_EXCEPTIONS
	try
	#endif
	{
		// packed test
		rcu |= pack_unsigned_amount(v, p1);
		rcu |= pack_unsigned_amount(i1, p2);

		unpack_unsigned_amount(p1, b5);
		unpack_unsigned_amount(p1, i5);
	}
	#ifdef CATCH_AMOUNT_TEST_EXCEPTIONS
	catch (...)
	{
		rcu |= 1;
	}
	#endif

	isbad = (v > maxup);
	cout << "test_one_amount_ isbad " << isbad << " (v > maxup) " << (v > maxup) << " rcu " << rcu << " amount " << hex << v << " maxup " << maxup << dec << endl;

	CCASSERTZ(!isbad && rcu);
	CCASSERTZ(isbad && !rcu);

	if (!rcu)
	{
		CCASSERT(v == b5);

		CCASSERT(i1 == i5);

		CCASSERTZ(memcmp(&p1, &p2, sizeof(p1)));

		try
		{
			auto rc2 = pack_unsigned_amount(i1 - 1, p2);
			if (!rc2 && i1 > 0)
				CCASSERT(memcmp(&p1, &p2, sizeof(p1)) > 0);
		}
		catch (...)
		{ }

		try
		{
			auto rc2 = pack_unsigned_amount(i1 + 1, p2);
			if (!rc2 && i1 >= 0)
				CCASSERT(memcmp(&p1, &p2, sizeof(p1)) < 0);
		}
		catch (...)
		{ }
	}

	int rcs = 0;

	#ifdef CATCH_AMOUNT_TEST_EXCEPTIONS
	try
	#endif
	{
		rcs |= pack_signed_amount(v, p3);
		rcs |= pack_signed_amount(i1, p4);

		unpack_signed_amount(p3, b6);
		unpack_signed_amount(p3, i6);
	}
	#ifdef CATCH_AMOUNT_TEST_EXCEPTIONS
	catch (...)
	{
		rcs |= 1;
	}
	#endif

	isbad = (v > maxp && v < minp);
	cout << "test_one_amount_ isbad " << isbad << " (v > maxp) " << (v > maxp) << " (v < minp) " << (v < minp) << " rcs " << rcs << " amount " << hex << v << " maxp " << maxp << " minp " << minp << dec << endl;

	CCASSERTZ(!isbad && rcs);
	CCASSERTZ(isbad && !rcs);

	if (!rcs)
	{
		CCASSERT(v == b6);

		CCASSERT(i1 == i6);

		CCASSERTZ(memcmp(&p3, &p3, sizeof(p3)));

		try
		{
			auto rc2 = pack_signed_amount(i1 - 1, p4);
			if (!rc2)
				CCASSERT(memcmp(&p3, &p4, sizeof(p3)) > 0);
		}
		catch (...)
		{ }

		try
		{
			auto rc2 = pack_signed_amount(i1 + 1, p4);
			if (!rc2)
				CCASSERT(memcmp(&p3, &p4, sizeof(p3)) < 0);
		}
		catch (...)
		{ }
	}

	auto result = test_result_code(0, rcs, rcu);
	cout << "test_one_amount_ result " << result << endl;

	return result;
}

static void test_random_amount(unsigned i)
{
	bigint_t r, v;

	r.randomize();
	v.randomize();

	if (!(i >> 11))
	{
		BIGWORD(v, 1) = (i >> 0) & 1;
		BIGWORD(v, 2) = (i >> 1) & 1;
		BIGWORD(v, 3) = (i >> 2) & 1;
		BIGWORD(v, 0) = (i >> 3);
	}
	//else
	//	return;

	unsigned nbits = BIGWORD(v, 0) & 255;

	if (!(BIGWORD(v, 1) & 31))
		r = 0UL;
	if (!(BIGWORD(v, 2) & 31))
		r = 1UL;
	if (!(BIGWORD(v, 3) & 31))
		subBigInt(bigint_t(0UL), bigint_t(1UL), r, false);

	v = 0UL;

	if (nbits)
	{
		v = 1UL;
		bigint_shift_up(v, nbits - 1);
		bigint_mask(r, nbits - 1);
		addBigInt(v, r, v, false);
	}

	cout << "test_random_amount nbits " << nbits << " r " << hex << r << " v " << v << dec << endl;

	auto expected = test_result_code(nbits >= 256, nbits >= 8*AMOUNT_SIGNED_PACKED_BYTES, nbits > 8*AMOUNT_UNSIGNED_PACKED_BYTES);
	CCASSERT(test_one_amount(v) == expected);

	subBigInt(bigint_t(0UL), v, v, false);

	expected = test_result_code(nbits >= 256, (r && nbits >= 8*AMOUNT_SIGNED_PACKED_BYTES) || nbits > 8*AMOUNT_SIGNED_PACKED_BYTES, nbits > 0);
	CCASSERT(test_one_amount(v) == expected);
}

static void test_amounts()
{
	cerr << "UINT64_MAX " << hex << UINT64_MAX << dec << endl;

	// 0

	bigint_t v = 0UL;

	CCASSERTZ(bigint_bytes_in_use(v));

	CCASSERTZ(test_one_amount(v));

	// INT128_MAX

	BIGWORD(v, 0) = UINT64_MAX;
	BIGWORD(v, 1) = INT64_MAX;

	CCASSERTZ(test_one_amount(v));

	v = v + (bigint_t)(1UL);

	CCASSERTZ(test_one_amount(v));

	// UINT128_MAX

	BIGWORD(v, 0) = UINT64_MAX;
	BIGWORD(v, 1) = UINT64_MAX;

	CCASSERTZ(test_one_amount(v));

	v = v + (bigint_t)(1UL);

	CCASSERT(test_one_amount(v) == test_result_code(0, 0, 1));

	// INT192_MAX

	v = 0UL;
	BIGWORD(v, 0) = UINT64_MAX;
	BIGWORD(v, 1) = UINT64_MAX;
	BIGWORD(v, 2) = INT64_MAX;

	CCASSERT(test_one_amount(v) == test_result_code(0, 0, 1));

	v = v + (bigint_t)(1UL);

	// -INT192_MAX

	v = 0UL;
	BIGWORD(v, 2) = (uint64_t)1 << 63;
	BIGWORD(v, 3) = UINT64_MAX;

	CCASSERT(test_one_amount(v) == test_result_code(0, 0, 1));

	v = v - (bigint_t)(1UL);

	CCASSERT(test_one_amount(v) == test_result_code(0, 1, 1));

	// not convertable

	v = 0UL;
	BIGWORD(v, 3) = (uint64_t)1 << 63;

	CCASSERT(test_one_amount(v) == test_result_code(1, 1, 1));

	BIGWORD(v, 0) = 1;

	CCASSERT(test_one_amount(v) == test_result_code(0, 1, 1));

	for (unsigned i = 1; i <= 256; ++i)
	{
		v = 1UL;
		bigint_shift_up(v, i - 1);

		cout << "i " << i << " bytes_in_use " << bigint_bytes_in_use(v) << " v " << hex << v << dec << endl;

		CCASSERT(bigint_bytes_in_use(v) == (i+7)/8);

		auto expected = test_result_code(i >= 256, i >= 8*AMOUNT_SIGNED_PACKED_BYTES, i > 8*AMOUNT_UNSIGNED_PACKED_BYTES);
		CCASSERT(test_one_amount(v) == expected);

		subBigInt(bigint_t(0UL), v, v, false);

		expected = test_result_code(i >= 256, i > 8*AMOUNT_SIGNED_PACKED_BYTES, 1);
		CCASSERT(test_one_amount(v) == expected);

		subBigInt(v, bigint_t(1UL), v, false);

		expected = test_result_code(i > 256, i >= 8*AMOUNT_SIGNED_PACKED_BYTES, 1);
		CCASSERT(test_one_amount(v) == expected);
	}

	for (unsigned i = 0; i <= 256; ++i)
	{
		v = 1UL;
		subBigInt(bigint_t(0UL), v, v, false);
		bigint_mask(v, i);

		cout << "i " << i << " bytes_in_use " << bigint_bytes_in_use(v) << " v " << hex << v << dec << endl;

		CCASSERT(bigint_bytes_in_use(v) == (i+7)/8);

		auto expected = test_result_code(i > 256, i >= 8*AMOUNT_SIGNED_PACKED_BYTES && i < 256, i > 8*AMOUNT_UNSIGNED_PACKED_BYTES);
		CCASSERT(test_one_amount(v) == expected);

		subBigInt(bigint_t(0UL), v, v, false);

		expected = test_result_code(i > 256, i >= 8*AMOUNT_SIGNED_PACKED_BYTES && i < 256, i > 0 && i < 256);
		CCASSERT(test_one_amount(v) == expected);

		subBigInt(v, bigint_t(1UL), v, false);

		expected = test_result_code(i == 255, i >= 8*AMOUNT_SIGNED_PACKED_BYTES && i < 256, i < 256);
		CCASSERT(test_one_amount(v) == expected);
	}

	//return;

	for (unsigned i = 0; i < 20000; ++i)
		test_random_amount(i);
}

#endif

void amount_from_bigint(const bigint_t& amount, amtint_t& val)
{
	bool neg = (BIGWORD(amount, 3) & ((uint64_t)1 << 63));

	bigint_t adj_amount = amount;
	if (neg)
		subBigInt(bigint_t(0UL), adj_amount, adj_amount, false);

	if (TRACE_AMOUNTS) cout << "amount_from_bigint neg " << neg << " amount " << amount << " = " << hex << amount << dec << " adj_amount " << adj_amount << " = " << hex << adj_amount << dec << endl;

	val = BIGWORD(adj_amount, 3);
	val <<= 64;
	val |= BIGWORD(adj_amount, 2);
	val <<= 64;
	val |= BIGWORD(adj_amount, 1);
	val <<= 64;
	val |= BIGWORD(adj_amount, 0);

	if (neg)
		val = -val;
}

void amount_to_bigint(const amtint_t& amount, bigint_t& val)
{
	amtint_t adj_amount = amount;
	if (amount < 0)
		adj_amount = -amount;

	if (TRACE_AMOUNTS) cout << "amount_to_bigint (amount < 0) " << (amount < 0) << " adj_amount " << adj_amount << " = " << hex << adj_amount << dec << endl;

	BIGWORD(val, 0) = (uint64_t)(adj_amount & UINT64_MAX);
	adj_amount >>= 64;
	BIGWORD(val, 1) = (uint64_t)(adj_amount & UINT64_MAX);
	adj_amount >>= 64;
	BIGWORD(val, 2) = (uint64_t)(adj_amount & UINT64_MAX);
	adj_amount >>= 64;
	BIGWORD(val, 3) = (uint64_t)(adj_amount & UINT64_MAX);

	if (amount < 0)
		subBigInt(bigint_t(0UL), val, val, false);
}

static const amtfloat_t scale_to_int[32] =
{
	amtfloat_t("1E0"),
	amtfloat_t("1E1"),
	amtfloat_t("1E2"),
	amtfloat_t("1E3"),
	amtfloat_t("1E4"),
	amtfloat_t("1E5"),
	amtfloat_t("1E6"),
	amtfloat_t("1E7"),
	amtfloat_t("1E8"),
	amtfloat_t("1E9"),
	amtfloat_t("1E10"),
	amtfloat_t("1E11"),
	amtfloat_t("1E12"),
	amtfloat_t("1E13"),
	amtfloat_t("1E14"),
	amtfloat_t("1E15"),
	amtfloat_t("1E16"),
	amtfloat_t("1E17"),
	amtfloat_t("1E18"),
	amtfloat_t("1E19"),
	amtfloat_t("1E20"),
	amtfloat_t("1E21"),
	amtfloat_t("1E22"),
	amtfloat_t("1E23"),
	amtfloat_t("1E24"),
	amtfloat_t("1E25"),
	amtfloat_t("1E26"),
	amtfloat_t("1E27"),
	amtfloat_t("1E28"),
	amtfloat_t("1E29"),
	amtfloat_t("1E30"),
	amtfloat_t("1E31")
};

static const amtfloat_t scale_from_int[32] =
{
	amtfloat_t("1E-0"),
	amtfloat_t("1E-1"),
	amtfloat_t("1E-2"),
	amtfloat_t("1E-3"),
	amtfloat_t("1E-4"),
	amtfloat_t("1E-5"),
	amtfloat_t("1E-6"),
	amtfloat_t("1E-7"),
	amtfloat_t("1E-8"),
	amtfloat_t("1E-9"),
	amtfloat_t("1E-10"),
	amtfloat_t("1E-11"),
	amtfloat_t("1E-12"),
	amtfloat_t("1E-13"),
	amtfloat_t("1E-14"),
	amtfloat_t("1E-15"),
	amtfloat_t("1E-16"),
	amtfloat_t("1E-17"),
	amtfloat_t("1E-18"),
	amtfloat_t("1E-19"),
	amtfloat_t("1E-20"),
	amtfloat_t("1E-21"),
	amtfloat_t("1E-22"),
	amtfloat_t("1E-23"),
	amtfloat_t("1E-24"),
	amtfloat_t("1E-25"),
	amtfloat_t("1E-26"),
	amtfloat_t("1E-27"),
	amtfloat_t("1E-28"),
	amtfloat_t("1E-29"),
	amtfloat_t("1E-30"),
	amtfloat_t("1E-31")
};

amtfloat_t asset_scale_factor(uint64_t asset)
{
	return scale_to_int[(asset - ASSET_NO_SCALE) & 31];
}

int amount_from_float(uint64_t asset, const amtfloat_t& val, amtint_t& amount)
{
	auto v2 = mm::round(val * scale_to_int[(asset - ASSET_NO_SCALE) & 31]);

	if (TRACE_AMOUNTS) cout << "amount_from_float asset " << asset << " val " << fixed << val << " amount_float_max " << amount_float_max << " amount_int_max " << amount_int_max << " hex " << hex << amount_int_max << dec << endl;

	if (v2 < -amount_float_max)	// INT64_MIN is not allowed because some functions compute -amount
	{
		amount = -amount_int_max;
		return -1;
	}

	if (v2 > amount_float_max)
	{
		amount = amount_int_max;
		return -1;
	}

	stringstream ss;
	ss.precision(1);

	ss << fixed << v2;

	ss >> amount;

	if (TRACE_AMOUNTS) cout << "amount_from_float ss " << ss.str() << " amount " << amount << endl;

	return 0;
}

int amount_from_float(uint64_t asset, const amtfloat_t& val, bigint_t& amount)
{
	amtint_t vi;

	auto rc = amount_from_float(asset, val, vi);

	amount_to_bigint(vi, amount);

	return rc;
}

void amount_to_float(uint64_t asset, const amtint_t& amount, amtfloat_t& val)
{
	#if TEST_AMOUNTS
	static bool dotest = true;
	if (dotest)
	{
		dotest = false;
		test_experiment_conversion();
		test_amounts();
	}
	#endif

	amtint_t adj_amount = amount;
	if (amount < 0)
		adj_amount = -amount;

	if (TRACE_AMOUNTS) cout << "amount_to_float asset " << asset << " (amount < 0) " << (amount < 0) << " adj_amount " << hex << adj_amount << dec << endl;

	static const amtfloat_t upshift = (amtfloat_t)((uint64_t)1 << 63) * 2;
	auto scale = scale_from_int[(asset - ASSET_NO_SCALE) & 31];

	mp::cpp_dec_float_100 big_val;
	big_val  = (uint64_t)((adj_amount >> (3*64)) & UINT64_MAX);
	big_val *= upshift;
	big_val += (uint64_t)((adj_amount >> (2*64)) & UINT64_MAX);
	big_val *= upshift;
	big_val += (uint64_t)((adj_amount >> (1*64)) & UINT64_MAX);
	big_val *= upshift;
	big_val += (uint64_t)((adj_amount >> (0*64)) & UINT64_MAX);
	big_val *= scale;

	if (amount < 0)
		big_val = -big_val;

	val = (amtfloat_t)big_val;

	if (TRACE_AMOUNTS)
	{
		cout << "amount_to_float amount " << amount << " upshift " << upshift << " asset " << asset << " scale " << scale << " val " << big_val << endl;
		stringstream ss;
		ss.precision(100);
		ss << fixed << val;
		cout << "amount_to_float    val " << ss.str() << endl;
	}
}

void amount_to_float(uint64_t asset, const bigint_t& amount, amtfloat_t& val)
{
	amtint_t vi;

	amount_from_bigint(amount, vi);

	amount_to_float(asset, vi, val);
}

void amount_to_string(uint64_t asset, const amtint_t& amount, string& s, bool add_decimal)
{
	amtfloat_t val;

	amount_to_float(asset, amount, val);

	amount_to_string(val, s, add_decimal);
}

void amount_to_string(uint64_t asset, const bigint_t& amount, string& s, bool add_decimal)
{
	amtfloat_t val;

	amount_to_float(asset, amount, val);

	amount_to_string(val, s, add_decimal);
}

void amount_to_string(const amtfloat_t& amount, string& s, bool add_decimal)
{
	if (TRACE_AMOUNTS) cout << "amount_to_string " << hex << amount << dec << endl;

	//amount = (amtfloat_t)("12345678911234567892123456789312345678941234567895123456789612345678971234567898e-90");
	//amount = (amtfloat_t)("1e-31");
	//amount = (amtfloat_t)("100");

	stringstream ss;
	ss.precision(31);
	ss << fixed << amount;
	s = ss.str();

	//s = "100";
	//s = "100.";
	//s = "100.0";
	//cout << "amount_to_string  in " << s << endl;

	if (s.find_first_of("eE") == string::npos)
	{
		auto dec = s.find('.');
		if (dec != string::npos)
		{
			auto z = s.find_last_not_of('0');

			if (dec == z)
				s.erase(dec);
			else if (z != string::npos)
				s.erase(z + 1);
		}

		if (add_decimal && s.find('.') == string::npos)
			s.append(".0");
	}

	//cout << "amount_to_string out " << s << endl;
}

void unpack_unsigned_amount(const packed_unsigned_amount_t& packed, bigint_t& amount)
{
	if (TRACE_AMOUNTS) cout << "unpack_unsigned_amount " << hex << packed.hi << ":" << packed.lo << dec << endl;

	BIGWORD(amount, 0) = bswap_64(packed.lo);
	BIGWORD(amount, 1) = bswap_64(packed.hi);
}

void unpack_unsigned_amount(const packed_unsigned_amount_t& packed, amtint_t& amount)
{
	bigint_t vi;

	unpack_unsigned_amount(packed, vi);

	amount_from_bigint(vi, amount);
}

void unpack_unsigned_amount(const void *packed, bigint_t& amount)
{
	return unpack_unsigned_amount(*(const packed_unsigned_amount_t*)packed, amount);
}

void unpack_unsigned_amount(const void *packed, amtint_t& amount)
{
	return unpack_unsigned_amount(*(const packed_unsigned_amount_t*)packed, amount);
}

int pack_unsigned_amount(const bigint_t& amount, packed_unsigned_amount_t& packed)
{
	if (TRACE_AMOUNTS) cout << "pack_unsigned_amount " << hex << amount << dec << endl;

	packed.lo = bswap_64(BIGWORD(amount, 0));				// byte-swapped so binary sort order is same as numeric sort order
	packed.hi = bswap_64(BIGWORD(amount, 1));

	bigint_t unpacked;

	unpack_unsigned_amount(packed, unpacked);

	auto rc = (unpacked != amount);

	if (rc) cout << "pack_unsigned_amount error packing amount " << amount << " hex " << hex << amount << " unpacked " << unpacked << " packed " << packed.hi << ":" << packed.lo << dec << " thread " << cc_thread_id() << endl;

	return rc;
}

int pack_unsigned_amount(const amtint_t& amount, packed_unsigned_amount_t& packed)
{
	bigint_t vi;

	amount_to_bigint(amount, vi);

	return pack_unsigned_amount(vi, packed);
}

const static bigint_t pack_offset = (bigint_t)((uint64_t)1 << 63) * (bigint_t)((uint64_t)1 << 63) * (bigint_t)((uint64_t)1 << 63) * (bigint_t)4UL;

void unpack_signed_amount(const packed_signed_amount_t& packed, bigint_t& amount)
{
	if (TRACE_AMOUNTS) cout << "unpack_signed_amount " << hex << packed.hi << ":" << packed.mid << ":" << packed.lo << dec << endl;

	BIGWORD(amount, 0) = bswap_64(packed.lo);
	BIGWORD(amount, 1) = bswap_64(packed.mid);
	BIGWORD(amount, 2) = bswap_64(packed.hi);

	subBigInt(amount, pack_offset, amount, false);

	if (BIGWORD(amount, 2) & ((uint64_t)1 << 63))
		BIGWORD(amount, 3) = UINT64_MAX;
	else
		BIGWORD(amount, 3) = 0UL;
}

void unpack_signed_amount(const packed_signed_amount_t& packed, amtint_t& amount)
{
	bigint_t vi;

	unpack_signed_amount(packed, vi);

	amount_from_bigint(vi, amount);
}

void unpack_signed_amount(const void *packed, bigint_t& amount)
{
	return unpack_signed_amount(*(const packed_signed_amount_t*)packed, amount);
}

void unpack_signed_amount(const void *packed, amtint_t& amount)
{
	return unpack_signed_amount(*(const packed_signed_amount_t*)packed, amount);
}

int pack_signed_amount(const bigint_t& amount, packed_signed_amount_t& packed)
{
	if (TRACE_AMOUNTS) cout << "pack_signed_amount " << hex << amount << " pack_offset " << pack_offset << dec << endl;

	bigint_t adj_amount = amount;

	addBigInt(adj_amount, pack_offset, adj_amount, false);

	packed.lo  = bswap_64(BIGWORD(adj_amount, 0));				// byte-swapped so binary sort order is same as numeric sort order
	packed.mid = bswap_64(BIGWORD(adj_amount, 1));
	packed.hi  = bswap_64(BIGWORD(adj_amount, 2));

	bigint_t unpacked;

	unpack_signed_amount(packed, unpacked);

	auto rc = (unpacked != amount);

	if (rc) cout << "pack_signed_amount error packing amount " << amount << " hex " << hex << amount << " unpacked " << unpacked << " packed " << packed.hi << ":" << packed.lo << dec << endl;

	return rc;
}

int pack_signed_amount(const amtint_t& amount, packed_signed_amount_t& packed)
{
	bigint_t vi;

	amount_to_bigint(amount, vi);

	return pack_signed_amount(vi, packed);
}
