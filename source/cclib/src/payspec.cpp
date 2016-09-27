/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * payspec.cpp
*/

#include "CCdef.h"
#include "payspec.h"
#include "jsonutil.h"
#include "transaction.hpp"
#include "CChash.hpp"
#include "CCproof.h"

#include <CCticks.hpp>

#include <keccak/KeccakHash.h>
#include <siphash/siphash.h>

static const uint8_t hashkey[16] = {};	// all zeros

static uint64_t hash_passphrase(const string& passphrase, const bigint_t& salt, int millisec, uint64_t iterations, bigint_t& result)
{
	CCASSERT(millisec || iterations);

	Keccak_HashInstance ctx;
	uint8_t prehash[512/8];
	uint8_t hash[512/8];
	uint8_t accum[512/8];

	int
	rc  = Keccak_HashInitialize_SHA3_512(&ctx);
	rc |= Keccak_HashUpdate(&ctx, (const uint8_t*)passphrase.c_str(), passphrase.length());
	rc |= Keccak_HashFinal(&ctx, prehash);

	rc |= Keccak_HashInitialize_SHA3_512(&ctx);
	rc |= Keccak_HashUpdate(&ctx, prehash, sizeof(prehash));
	rc |= Keccak_HashUpdate(&ctx, (const uint8_t*)&salt, sizeof(salt));
	rc |= Keccak_HashFinal(&ctx, accum);

	auto t0 = ccticks(CLOCK_THREAD_CPUTIME_ID);
	unsigned n = iterations;
	if (!n)
		n = 5000;

	iterations = 0;

	while (true)
	{
		for (unsigned i = 0; i < n; ++i)
		{
			rc |= Keccak_HashInitialize_SHA3_512(&ctx);
			rc |= Keccak_HashUpdate(&ctx, prehash, sizeof(prehash));
			rc |= Keccak_HashUpdate(&ctx, accum, sizeof(accum));
			rc |= Keccak_HashFinal(&ctx, hash);

			for (unsigned j = 0; j < 512/64; ++j)
				accum[j] ^= hash[j];
		}

		iterations += n;

		auto t1 = ccticks(CLOCK_THREAD_CPUTIME_ID);
		auto elapsed = ccticks_elapsed(t0, t1);
		auto diff = millisec - elapsed;
		//cerr << "hash_passphrase iterations " << iterations << " elapsed " << elapsed << " diff " << diff << endl;

		if (diff <= 0 || iterations > ((uint64_t)1UL << 62))
		{
			//cerr << "hash_passphrase iterations " << iterations << " elapsed time " << elapsed << " ms" << endl;
			break;
		}

		if (elapsed < 5)
			continue;

		if (diff > 10)
			n = (iterations * (diff-10) + elapsed/2) / elapsed;
		else
			n = (iterations * diff + elapsed/2) / elapsed;

		if (n > 500000)
			n = 500000;
	}

	CCASSERTZ(rc);

	memcpy(&result, &accum, sizeof(result));

	return iterations;
}

CCRESULT generate_master_secret(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	string key;
	Json::Value value;

	bigint_t bigval;

	key = "milliseconds";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, bufsize);
	if (rc) return rc;
	auto millisec = BIG64(bigval);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	//cerr << "millisec " << millisec << endl;

	if (millisec < 10 || millisec > 1000000)
		return copy_error_to_output(fn + ": milliseconds must be >= 10 and <= 1000000", output, bufsize);

	bigint_t salt;
	CCRandom(&salt, sizeof(salt));

	//for (unsigned i = 0; i < 4; ++i)
	//	BIGWORD(salt, i) = (uint64_t)(-1);	// for testing

	// call hash_passphrase to calibrate milliseconds to iterations
	auto iterations = hash_passphrase("dummy", salt, millisec, 0, bigval);

	//cerr << hex << "salt " << salt << dec << endl;
	//cerr << "iterations " << iterations << endl;

	string outs = "CCMS";
	encode(base58, 58, 0UL, false, 0, salt, outs);
	//cerr << "salt: " << outs << endl;
	encode(base58, 58, 0UL, false, -1, iterations, outs);
	uint64_t hash = sip_hash24(hashkey, (const uint8_t *)outs.data(), outs.length(), true);
	//cerr << "hash " << hash << " " << outs << endl;
	encode(base58, 58, 0UL, false, 5, hash, outs);

	//cerr << outs << endl;

	return copy_result_to_output(fn, outs, output, bufsize);
}

CCRESULT master_secret_to_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	string key;
	Json::Value value;

	key = "scrambled-master-secret";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto msspec = value.asString();

	key = "passphrase";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto passphrase = value.asString();

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	if (msspec.length() < 10)
		return copy_error_to_output(fn + ": invalid scrambled master secret length", output, bufsize);

	if (msspec.compare(0, 4, "CCMS"))
		return copy_error_to_output(fn + ": invalid scrambled master secret", output, bufsize);

	bigint_t salt, iterations, secret;

	auto inlen = msspec.length();
	string instring = msspec.substr(4);

	auto rc = decode(fn, base58int, 58, false, 44, instring, salt, output, bufsize);
	if (rc) return rc;

	if (instring.length() < 6)
		return error_unexpected_char(fn, output, bufsize);

	rc = decode(fn, base58int, 58, false, instring.length() - 5, instring, iterations, output, bufsize);
	if (rc) return rc;

	//cerr << "remaining string " << instring << endl;

	string outs;
	uint64_t hash = sip_hash24(hashkey, (const uint8_t *)msspec.data(), inlen - 5, true);
	encode(base58, 58, 0UL, false, 5, hash, outs);
	//cerr << "hash " << hash << " " << outs << " " << instring << endl;
	if (outs != instring)
		return error_checksum_mismatch(fn, output, bufsize);

	//cerr << hex << "salt " << salt << dec << endl;
	//cerr << "iterations " << iterations << endl;

	hash_passphrase(passphrase, salt, 0, BIG64(iterations), secret);

	ostringstream os;
	os << hex;
	os << "0x" << secret;

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, bufsize);
}

CCRESULT payspec_from_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	if (root.size() != 1)
		return copy_error_to_output(fn + ": json payspec must contain exactly one object", output, bufsize);

	string key;
	Json::Value value;

	auto it = root.begin();
	key = it.name();
	root = *it;

	if (key != "payspec")
		return error_unexpected_key(fn, key, output, bufsize);

	bigint_t dest, seqtype, amount;

	key = "destination";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, dest, output, bufsize);
		if (rc) return rc;
	}
	else
	{
		bigint_t spend_secret, spend_secret_hashed, enforce_spendspec_hash, spendspec_hashed;
		vector<CCHashInput> hashin(HASH_MAX_INPUTS);

		key = "hashed-spend-secret";
		if (root.removeMember(key, &value))
		{
			auto rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, spend_secret_hashed, output, bufsize);
			if (rc) return rc;
		}
		else
		{
			key = "spend-secret";
			if (!root.removeMember(key, &value))
				return error_missing_key(fn, "destination or spend-secret or hashed-spend-secret", output, bufsize);
			auto rc = parse_int_value(fn, key, value.asString(), 0, 0UL, spend_secret, output, bufsize);
			if (rc) return rc;

			hashin.clear();
			hashin.resize(1);
			hashin[0].SetValue(spend_secret, TX_FIELD_BITS);
			spend_secret_hashed = CCHash::Hash(hashin, HASH_BASES_RECEIVE_SECRET, TX_FIELD_BITS);
		}

		key = "enforce-spend-spec-hash";
		if (root.removeMember(key, &value))
		{
			auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, enforce_spendspec_hash, output, bufsize);
			if (rc) return rc;

			key = "hashed-spend-spec";
			if (root.removeMember(key, &value))
			{
				auto rc = parse_int_value(fn, key, value.asString(), 0, 0UL, spendspec_hashed, output, bufsize); // it's a stdhash and can exceed the prime modulus
				if (rc) return rc;

				spendspec_hashed = enforce_spendspec_hash * spendspec_hashed;
			}
			else if (enforce_spendspec_hash)
				return error_missing_key(fn, key, output, bufsize);
		}

		// #dest = zkhash(@enforce_spendspec_hash, @enforce_spendspec_hash*stdhash(S-spendspec), zkhash(@secret))

		hashin.clear();
		hashin.resize(3);
		hashin[0].SetValue(enforce_spendspec_hash, 1);
		hashin[1].SetValue(spendspec_hashed, TX_FIELD_BITS);
		hashin[2].SetValue(spend_secret_hashed, TX_FIELD_BITS);
		dest = CCHash::Hash(hashin, HASH_BASES_DESTINATION, TX_FIELD_BITS);
	}

	key = "sequence-type";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, 9UL, seqtype, output, bufsize);
	if (rc) return rc;
	if (BIG64(seqtype) != 0 && BIG64(seqtype) != 1 && BIG64(seqtype) != 9)
		return error_invalid_value(fn, key, output, bufsize);

	key = "requested-amount";
	bool has_amount = false;
	if (root.removeMember(key, &value))
	{
		has_amount = true;
		auto rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, amount, output, bufsize);
		if (rc) return rc;
	}

	//cerr << "dest " << dest << endl;
	//cerr << "seqtype " << seqtype << endl;
	//cerr << "amount " << amount << endl;
	//cerr << "extra info" << endl << root << endl;

	string outs = "CC";
	outs += to_string(BIG64(seqtype));

	encode(base58, 58, TX_INPUT_MAX, false, 0, dest, outs);

	if (has_amount)
		encode(base58, 58, 0UL, true, -1, amount, outs);

	outs.push_back(SEPARATOR);

	// !!! extract wallet url here

	outs.push_back(SEPARATOR);

	uint64_t hash = sip_hash24(hashkey, (const uint8_t *)outs.data(), outs.length(), true);
	encode(base58, 58, 0UL, false, 5, hash, outs);

#if 0	// skip extra for now
	bool has_extra = false;

	for (auto i = root.begin(); i != root.end(); ++i)
	{
		//cerr << i.name() << " : " i->asString() << endl;

		if (!i.name().length()) && !i->asString().length())
			continue;

		has_extra = true;

		outs.push_back(SEPARATOR);
		encodestring(i.name(), outs);

		outs.push_back(SEPARATOR);
		encodestring(i->asString(), outs);
	}

	if (has_extra)
	{
		outs.push_back(SEPARATOR);
		uint64_t hash = sip_hash24(hashkey, (const uint8_t *)outs.data(), outs.length(), true);
		encode(base58, 58, 0UL, false, 5, hash, outs);
	}
#endif

	//cerr << outs << endl;

	return copy_result_to_output(fn, outs, output, bufsize);
}

CCRESULT payspec_to_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	//cerr << "payspec_to_json" << endl;

	if (!root.isString())
		return copy_error_to_output(fn + ": payspec must contain exactly one string", output, bufsize);

	auto payspec = root.asString();

	if (payspec.length() < 3)
		return copy_error_to_output(fn + ": invalid payspec length", output, bufsize);

	char seqtype = payspec[2];

	//cerr << "seqtype " << seqtype << endl;

	if (payspec.compare(0, 2, "CC") || (seqtype != '0' && seqtype != '1' && seqtype != '9'))
		return copy_error_to_output(fn + ": unrecognized payspec type", output, bufsize);

	bigint_t dest, amount;

	auto inlen = payspec.length();
	string instring = payspec.substr(3);

	auto rc = decode(fn, base58int, 58, false, 44, instring, dest, output, bufsize);
	if (rc) return rc;

	//cerr << "amount string " << instring << endl;

	bool has_amount = false;
	if (!instring.empty() && instring.front() != SEPARATOR && instring.front() != SEPARATOR_ALT)
	{
		has_amount = true;
		auto rc = decode(fn, base58int, 58, true, 0, instring, amount, output, bufsize);
		if (rc) return rc;
	}

	//cerr << "remaining string " << instring << endl;

	if (instring.empty() || (instring.front() != SEPARATOR && instring.front() != SEPARATOR_ALT))
		return error_unexpected_char(fn, output, bufsize);

	instring = instring.substr(1, string::npos);

	// !!! extract wallet url here

	if (instring.length() < 6 || (instring.front() != SEPARATOR && instring.front() != SEPARATOR_ALT))
		return error_unexpected_char(fn, output, bufsize);

	string outs;
	uint64_t hash = sip_hash24(hashkey, (const uint8_t *)payspec.data(), inlen - instring.length() + 1, true);
	encode(base58, 58, 0UL, false, 5, hash, outs);
	//cerr << "hash " << outs << " " << instring << endl;
	if (outs != instring.substr(1,5))
		return error_checksum_mismatch(fn, output, bufsize);

	//cerr << "dest " << dest << endl;
	//cerr << "seqtype " << seqtype << endl;
	//cerr << "amount " << amount << endl;

	ostringstream os;
	os << hex;

	os << "{\"payspec\":" JSON_ENDL
	os << "{\"destination\":\"0x" << dest << "\"" JSON_ENDL
	os << ",\"sequence-type\":\"0x" << seqtype << "\"" JSON_ENDL
	if (has_amount)
	{
		os << ",\"requested-amount\":\"0x" << amount << "\"" JSON_ENDL
	}
	os << "}}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, bufsize);
}

CCRESULT hash_spend_secret(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	if (root.size() != 1)
		return copy_error_to_output(fn + ": json spend secret must contain exactly one object", output, bufsize);

	string key;
	Json::Value value;

	bigint_t spend_secret;

	key = "spend-secret";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, 0UL, spend_secret, output, bufsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	vector<CCHashInput> hashin(1);
	hashin[0].SetValue(spend_secret, TX_FIELD_BITS);
	auto spend_secret_hashed = CCHash::Hash(hashin, HASH_BASES_RECEIVE_SECRET, TX_FIELD_BITS);

	ostringstream os;
	os << hex;

	os << "{\"hashed-spend-secret\":\"0x" << spend_secret_hashed << "\"}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, bufsize);
}

CCRESULT compute_address(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	string key;
	Json::Value value;

	bigint_t dest, paynum;

	key = "destination";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, dest, output, bufsize);
	if (rc) return rc;

	key = "payment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_PAYNUM_BITS, 0UL, paynum, output, bufsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	// M-address = zkhash(&dest, &paynum)

	vector<CCHashInput> hashin(2);
	hashin[0].SetValue(dest, TX_FIELD_BITS);
	hashin[1].SetValue(paynum, TX_PAYNUM_BITS);
	auto address = CCHash::Hash(hashin, HASH_BASES_ADDRESS, TX_FIELD_BITS);
	auto value_xor = BIG64(CCHash::Hash(hashin, HASH_BASES_VALUEENC, TX_VALUE_BITS));

	//cerr << hex << "address " << address << dec << endl;
	//cerr << hex << "value_xor " << value_xor << dec << endl;

	ostringstream os;
	os << hex;

	os << "{\"address\":\"0x" << address << "\"";
	os << ",\"value-encode-xor\":\"0x" << value_xor << "\"";
	os << "}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, bufsize);
}

#if SUPPORT_GENERATE_TEST_INPUTS

struct test_input_params
{
	bool has_spend_secret;
	bool has_enforce_spendspec_hash;
	bool has_spendspec_hashed;
	bigint_t spend_secret;
	bigint_t enforce_spendspec_hash;
	bigint_t spendspec_hashed;

	bigint_t dest;
	bigint_t paynum;
	bigint_t value;
	bigint_t commitment_iv;

	bigint_t commitment;
	bigint_t leafindex;
	array<bigint_t, TX_MERKLE_DEPTH+1> tree;
	vector<bigint_t> path;
};

static CCRESULT generate_test_input(const string& fn, Json::Value& root, test_input_params& input, char *output, const uint32_t bufsize)
{
	string key;
	Json::Value value;

	input.has_spend_secret = false;
	input.has_enforce_spendspec_hash = false;
	input.has_spendspec_hashed = false;

	key = "spend-secret";
	if (root.removeMember(key, &value))
	{
		// not required--if provided, it is echo'ed back for convenience
		auto rc = parse_int_value(fn, key, value.asString(), 0, 0UL, input.spend_secret, output, bufsize);
		if (rc) return rc;

		input.has_spend_secret = true;
	}

	key = "enforce-spend-spec-hash";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, input.enforce_spendspec_hash, output, bufsize);
		if (rc) return rc;

		input.has_enforce_spendspec_hash = true;
	}

	key = "hashed-spend-spec";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 0, 0UL, input.spendspec_hashed, output, bufsize); // it's a stdhash and can exceed the prime modulus
		if (rc) return rc;

		input.has_spendspec_hashed = true;
	}

	key = "destination";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, input.dest, output, bufsize);
	if (rc) return rc;

	key = "payment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_PAYNUM_BITS, 0UL, input.paynum, output, bufsize);
	if (rc) return rc;

	key = "value";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_VALUE_BITS, 0UL, input.value, output, bufsize);
	if (rc) return rc;

	key = "commitment-iv";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_COMMIT_IV_BITS, 0UL, input.commitment_iv, output, bufsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	vector<CCHashInput> hashin(HASH_MAX_INPUTS);

	// M-commitment = zkhash(#dest, #paynum, #value, M-commitment_iv)

	hashin.clear();
	hashin.resize(4);
	hashin[0].SetValue(input.dest, TX_FIELD_BITS);
	hashin[1].SetValue(input.paynum, TX_PAYNUM_BITS);
	hashin[2].SetValue(input.value, TX_VALUE_BITS);
	hashin[3].SetValue(input.commitment_iv, TX_COMMIT_IV_BITS);
	input.commitment = CCHash::Hash(hashin, HASH_BASES_COMMITMENT, TX_FIELD_BITS);

	input.leafindex.randomize();
	if (TX_MERKLE_LEAFINDEX_BITS < 64)
		BIG64(input.leafindex) &= ((uint64_t)(1) << TX_MERKLE_LEAFINDEX_BITS) - 1;
	for (unsigned i = 1; i < input.leafindex.numberLimbs(); ++i)
		BIGWORD(input.leafindex, i) = 0;
	//cerr << hex << input.leafindex << dec << endl;

	hashin.clear();
	hashin.resize(2);
	hashin[0].SetValue(input.commitment, TX_FIELD_BITS);
	hashin[1].SetValue(input.leafindex, TX_MERKLE_LEAFINDEX_BITS);
	//hashin[0].Dump();
	//hashin[1].Dump();
	input.tree[0] = CCHash::Hash(hashin, HASH_BASES_MERKLE_LEAF, TX_MERKLE_PATH_BITS);

	//cerr << "set input[i].tree[0] to " << hex << input.tree[0] << dec << endl;

	return 0;
}

static void generate_tree(test_input_params input[])
{
	// only works when MAX_INPUTS_FOR_TESTING is a power of 2

	vector<CCHashInput> hashin(2);

	hashin[0].mask_higher_bits = (TX_MERKLE_PATH_BITS < CC_HASH_BITS);

	for (unsigned i = 0; i < MAX_INPUTS_FOR_TESTING; ++i)
		input[i].path.resize(TX_MERKLE_DEPTH);

	unsigned width = MAX_INPUTS_FOR_TESTING;

	for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
	{
		//cerr << "level " << j << " width " << width << endl;

		if (width == 1)
		{
			input[1].tree[j].randomize();
			input[1].tree[j] = input[1].tree[j] * bigint_t(1UL);	// mod prime

			width = 2;
			CCASSERT(MAX_INPUTS_FOR_TESTING >= width);

			//cerr << "set input[1].tree[" << j << "] to random " << hex << input[1].tree[j] << dec << endl;
		}

		for (unsigned i = 0; i < MAX_INPUTS_FOR_TESTING; ++i)
		{
			input[i].path[j] = input[(i/(((uint64_t)(1)) << j)) ^ 1].tree[j];

			//cerr << "set input[" << i << "].path[" << j << "] to " << hex << input[i].path[j] << dec << endl;
		}

		for (unsigned i = 0; i < width/2; ++i)
		{
			hashin[0].SetValue(input[2*i].tree[j], TX_MERKLE_PATH_BITS);
			hashin[1].SetValue(input[2*i + 1].tree[j], TX_MERKLE_PATH_BITS);

			//hashin[0].Dump();
			//hashin[1].Dump();

			input[i].tree[j+1] = CCHash::Hash(hashin, HASH_BASES_MERKLE_NODE, TX_MERKLE_PATH_BITS);

			//cerr << "set input[" << i << "].tree[" << j + 1 << "] to " << hex << input[i].tree[j+1] << dec << endl;
		}

		width /= 2;
	}

	for (unsigned i = 0; i < MAX_INPUTS_FOR_TESTING; ++i)
	{
		bigint_t root = CCHash::Merkle(input[i].tree[0], TX_MERKLE_PATH_BITS, input[i].path, TX_MERKLE_PATH_BITS, TX_MERKLE_ROOT_BITS);

		if (root != input[0].tree[TX_MERKLE_DEPTH])
			cerr << "error: input " << i << " merkle " << hex << root << " != " << input[0].tree[TX_MERKLE_DEPTH] << dec << endl;

		CCASSERT(root == input[0].tree[TX_MERKLE_DEPTH]);
	}
}

CCRESULT generate_test_inputs(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	if (!root.isArray()) // no longer enforced: || root.size() < 1)
		return error_not_array(fn, fn, output, bufsize);

	if (root.size() > MAX_INPUTS_FOR_TESTING)
		return error_too_many_objs(fn, fn, MAX_INPUTS_FOR_TESTING, output, bufsize);

	unique_ptr<test_input_params[]> input(new test_input_params[MAX_INPUTS_FOR_TESTING]);
	CCASSERT(input);

	for (unsigned i = 0; i < MAX_INPUTS_FOR_TESTING; ++i)
	{
		input[i].tree[0].randomize();
		input[i].tree[0] = input[i].tree[0] * bigint_t(1UL);	// mod prime
	}

	for (unsigned i = 0; i < root.size(); ++i)
	{
		auto rc = generate_test_input(fn, root[i], input[i], output, bufsize);
		if (rc) return rc;
	}

	generate_tree(input.get());

	ostringstream os;
	os << hex;
	os << "\"parameter-level\":\"0x0\"" JSON_ENDL
	os << ",\"merkle-root\":\"0x" << input[0].tree[TX_MERKLE_DEPTH] << "\"" JSON_ENDL
	os << ",\"inputs\" : [" JSON_ENDL

	for (unsigned i = 0; i < root.size(); ++i)
	{
		if (i)
			os << ",";
		os << "{\"payment-number\":\"0x" << input[i].paynum << "\"" JSON_ENDL
		os << ",\"value\":\"0x" << input[i].value << "\"" JSON_ENDL
		os << ",\"commitment-iv\":\"0x" << input[i].commitment_iv << "\"" JSON_ENDL
		if (input[i].has_spend_secret)
			os << ",\"spend-secret\":\"0x" << input[i].spend_secret << "\"" JSON_ENDL
		if (input[i].has_enforce_spendspec_hash)
			os << ",\"enforce-spend-spec-hash\":\"0x" << input[i].enforce_spendspec_hash << "\"" JSON_ENDL
		if (input[i].has_spendspec_hashed)
			os << ",\"hashed-spend-spec\":\"0x" << input[i].spendspec_hashed << "\"" JSON_ENDL
		os << ",\"commitment\":\"0x" << input[i].commitment << "\"" JSON_ENDL

		os << ",\"commitment-number\":\"0x" << input[i].leafindex << "\"" JSON_ENDL
		os << ",\"merkle-path\":" JSON_ENDL
		os << "[";
		for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
		{
			if (j) os << ",";
			os << "\"0x" << input[i].path[j] << "\"" JSON_ENDL
		}
		os << "]}" JSON_ENDL
	}

	os << "]";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, bufsize);
}

#endif // SUPPORT_GENERATE_TEST_INPUTS
