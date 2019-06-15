/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * payspec.cpp
*/

#include "cclib.h"
#include "payspec.h"
#include "jsonutil.h"
#include "transaction.h"
#include "transaction.hpp"
#include "transaction-json.hpp"
#include "CChash.hpp"
#include "CCparams.h"

#include <blake2/blake2.h>
#include <siphash/siphash.h>

#define KDF_HASH_BITS	512
#define KDF_HASH_BYTES	(KDF_HASH_BITS/8)

#define ITERATIONS_SCALE	10

#define TRACE	0

CCRESULT hash_passphrase(const string& passphrase, const bigint_t& salt, int millisec, uint64_t memory, uint64_t& iterations, bigint_t& result)
{
	CCASSERT(memory);
	CCASSERT(millisec || iterations);
	CCASSERTZ(millisec && iterations);

	memory = (memory * 1048571 + KDF_HASH_BYTES - 1) & ~(KDF_HASH_BYTES - 1);	// memeory units are 1048571 bytes (a prime < 1MB), rounded up to a multiple of KDF_HASH_BYTES
	CCASSERT(memory <= ((uint64_t)1 << 32) * sizeof(uint32_t));					// because memory index is computed with a 32-bit modulo operation

	if (TRACE) cerr << "hash_passphrase millisec " << millisec << " memory " << memory << " iterations " << iterations <<  endl;

	char* pmem = (char*)malloc(memory);
	if (!pmem)
		return -1;

	blake2b_ctx ctx;
	uint64_t hash[KDF_HASH_BITS/64];
	uint64_t accum[KDF_HASH_BITS/64];

	// passhash = hash(passphrase)

	int rc =
	blake2b_init(&ctx, sizeof(accum), NULL, 0);
	blake2b_update(&ctx, &salt, sizeof(salt));
	blake2b_update(&ctx, passphrase.c_str(), passphrase.length());
	blake2b_final(&ctx, accum);

	//cerr << "hash_passphrase salt " << buf2hex(&salt, sizeof(salt)) << endl;
	//cerr << "hash_passphrase passhash " << buf2hex(accum, sizeof(accum)) << endl;

	memcpy(pmem, accum, sizeof(accum));
	uint64_t mem_offset = sizeof(accum);
	uint64_t mem_filled = mem_offset;

	auto t0 = ccticks(CLOCK_THREAD_CPUTIME_ID);
	auto n = iterations;
	if (!n)
		n = 10000;

	if (millisec)
		iterations = 0;

	while (true)
	{
		for (uint64_t i = 0; i < n; ++i)
		{
			// accum = accum ^ hash(memory_values, accum)

			rc |= blake2b_init(&ctx, sizeof(hash), NULL, 0);

			for (unsigned j = 0; j < sizeof(accum)/sizeof(accum[0]); ++j)
			{
				uint32_t i0 = (uint32_t)(accum[j] >> 32) % (uint32_t)(mem_filled/sizeof(uint32_t));		// high word is index
				uint64_t offset = (uint64_t)i0 * sizeof(uint32_t);
				blake2b_update(&ctx, pmem + offset, sizeof(uint32_t));									// low word is added to hash

				//cerr << "hash_passphrase mix " << j << " offset " << i0 << " " << offset << " filled " << mem_filled << " memory " << memory << " data " << buf2hex(pmem + offset, sizeof(uint32_t)) << endl;
				//if (offset >= ((uint64_t)1 << 32))
				//	cerr << "hash_passphrase mix offset\tx" << hex << i0 << "\tx" << offset << "\tx" << mem_filled << dec << endl;
			}

			blake2b_update(&ctx, accum, sizeof(accum));
			blake2b_final(&ctx, hash);

			for (unsigned j = 0; j < sizeof(accum)/sizeof(accum[0]); ++j)
				accum[j] ^= hash[j];

			memcpy(pmem + mem_offset, accum, sizeof(accum));

			//cerr << "hash_passphrase fill " << mem_offset << " filled " << mem_filled << " memory " << memory << " data " << buf2hex(accum, sizeof(accum)) << endl;

			mem_offset += sizeof(accum);
			CCASSERT(mem_offset <= memory);

			if (mem_offset > mem_filled)
				mem_filled = mem_offset;

			if (mem_offset == memory)
				mem_offset = 0;
		}

		if (millisec)
			iterations += n;

		//cerr << "hash_passphrase iterations " << iterations << " mem offset " << mem_offset << " filled " << mem_filled << " memory " << memory << " data " << buf2hex(pmem, KDF_HASH_BYTES) << endl;

		auto t1 = ccticks(CLOCK_THREAD_CPUTIME_ID);
		auto elapsed = ccticks_elapsed(t0, t1);
		auto diff = millisec - elapsed;
		//cerr << "hash_passphrase iterations " << iterations << " elapsed " << elapsed << " diff " << diff << endl;

		if (millisec && elapsed < 100)
			continue;

		if (diff <= 0 || (millisec && elapsed > 2000 && elapsed > millisec/10) || iterations > ((uint64_t)1 << 62))
		{
			if (TRACE) cerr << "hash_passphrase iterations " << iterations << " elapsed time " << elapsed << " ms" << endl;

			if (millisec)
			{
				iterations = (double)iterations * millisec / elapsed + 0.5;
				if (!iterations)
					iterations = 1;

				if (TRACE) cerr << "hash_passphrase adjusted iterations " << iterations << endl;
			}

			break;
		}

		if (diff > 10)
			n = (iterations * (diff-10) + elapsed/2) / elapsed;
		else
			n = (iterations * diff + elapsed/2) / elapsed;

		if (n < 10000)
			n = 10000;

		if (n > 500000)
			n = 500000;
	}

	CCASSERTZ(rc);

	result = *(bigint_t*)&accum;

	memset(pmem, 0, memory);	// !!! secure erase would be better
	free(pmem);

	//cerr << "hash_passphrase iterations " << iterations << " result " << buf2hex(&result, sizeof(result)) << endl;

	return 0;
}

CCRESULT generate_random(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	bigint_t salt, bigval;
	unsigned nbits = 256;

	key = "nbits";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 0, 256UL, bigval, output, outsize);
		if (rc) return rc;
		nbits = BIG64(bigval);
	}

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	if (nbits < 1 || nbits > 256)
		return copy_error_to_output(fn, string("error: ") + key + " must be >= 1 and <= 256", output, outsize);

	salt.randomize();
	bigint_mask(salt, nbits);

	ostringstream os;
	os << hex;

	os << "{\"random\":\"0x" << salt << "\"";
	os << "}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

CCRESULT generate_master_secret_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	bigint_t salt, bigval;

	key = "memory";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) return rc;
	auto memory = BIG64(bigval);

	if (memory < 1 || memory > 16383)
		return copy_error_to_output(fn, "error: memory must be >= 1 and <= 16383", output, outsize);

	key = "milliseconds";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), 64, 0UL, bigval, output, outsize);
	if (rc) return rc;
	auto millisec = BIG64(bigval);
	//cerr << "millisec " << millisec << endl;

	if (/* millisec < 0 || */ millisec > 1000000)
		return copy_error_to_output(fn, string("error: ") + key + " must be >= 0 and <= 1000000", output, outsize);

	key = "seed";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), TX_INPUT_BITS, 0UL, salt, output, outsize);
		if (rc) return rc;
	}
	else
		salt.randomize();

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	return generate_master_secret(fn, memory, millisec, salt, output, outsize);
}

CCRESULT generate_master_secret(const string& fn, unsigned memory, unsigned millisec, const bigint_t& salt, char *output, const uint32_t outsize)
{
	//for (unsigned i = 0; i < 4; ++i)
	//	BIGWORD(salt, i) = (uint64_t)(-1);	// for testing

	uint64_t iterations = 1;
	bigint_t bigval;

	if (millisec)
	{
		// call hash_passphrase to calibrate milliseconds to iterations
		iterations = 0;
		auto rc = hash_passphrase("dummy", salt, millisec, memory, iterations, bigval);
		if (rc)
			return error_unexpected(fn, output, outsize);
	}

	iterations += (ITERATIONS_SCALE - 1);
	iterations /= ITERATIONS_SCALE;

	if (TRACE)
	{
		cerr << hex << "salt " << salt << dec << endl;
		cerr << "memory " << memory << endl;
		cerr << "iterations " << iterations * ITERATIONS_SCALE << endl;
	}

	string outs = "CCMS";

	cc_encode(base57, 57, 0UL, false, 0, salt, outs);
	//cerr << "salt: " << outs << endl;
	cc_encode(base57, 57, 0UL, true, -1, memory, outs);
	outs.push_back(CC_ENCODE_SEPARATOR);
	//cerr << "memory: " << outs << endl;
	cc_encode(base57, 57, 0UL, false, -1, iterations, outs);
	//cerr << "iterations: " << outs << endl;
	uint64_t hash = siphash((const uint8_t *)outs.data(), outs.length());
	//cerr << "hash " << hash << " " << outs << endl;
	cc_encode(base57, 57, 0UL, false, 5, hash, outs);

	outs = "{\"encrypted-master-secret\":\"" + outs + "\"}";

	//cerr << outs << endl;

	return copy_result_to_output(fn, outs, output, outsize);
}

CCRESULT compute_master_secret_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	bool ascii_only = true;
	bigint_t bigval;

	key = "encrypted-master-secret";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto msspec = value.asString();

	key = "passphrase";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto passphrase = value.asString();

	key = "ascii-only";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 1, 0UL, bigval, output, outsize);
		if (rc) return rc;
		ascii_only = BIG64(bigval);
	}

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	if (ascii_only)
	{
		auto rc = check_ascii_only(fn, passphrase, output, outsize);
		if (rc) return rc;
	}

	bigint_t master_secret;

	auto rc = compute_master_secret(fn, msspec, passphrase, master_secret, output, outsize);
	if (rc) return rc;

	ostringstream os;
	os << hex;

	os << "{\"master-secret\":\"0x" << master_secret << "\"";
	os << "}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

CCRESULT check_ascii_only(const string& fn, string& passphrase, char *output, const uint32_t outsize)
{
	string ascii;

	for (unsigned i = 0; i < passphrase.length(); ++i)
	{
		char c = passphrase[i];

		if (c < 32 || c >= 127)
			return copy_error_to_output(fn, "error: invalid character in ascii", output, outsize);

		if ((c < '0' || c > '9') && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z'))
			c = ' ';

		if (c == ' ' && (ascii.length() == 0 || ascii.back() == ' '))
			continue;

		ascii.push_back(c);
	}

	if (ascii.back() == ' ')
		ascii.pop_back();

	//cerr << "ascii normalize \"" << passphrase << "\" --> \"" << ascii << "\"" << endl;

	passphrase = ascii;

	return 0;
}

CCRESULT compute_master_secret(const string& fn, const string& msspec, const string& passphrase, bigint_t& master_secret, char *output, const uint32_t outsize)
{
	if (msspec.length() < 10)
		return copy_error_to_output(fn, "error: invalid scrambled master secret length", output, outsize);

	if (msspec.compare(0, 4, "CCMS"))
		return copy_error_to_output(fn, "error: invalid scrambled master secret", output, outsize);

	bigint_t salt, bigval;

	auto inlen = msspec.length();
	string instring = msspec.substr(4);

	auto rc = cc_decode(fn, base57int, 57, false, 44, instring, salt, output, outsize);
	if (rc) return rc;

	//cerr << "memory remaining string " << instring << endl;

	rc = cc_decode(fn, base57int, 57, true, 0, instring, bigval, output, outsize);
	if (rc) return rc;
	auto memory = BIG64(bigval);

	if (instring.empty() || (instring.front() != CC_ENCODE_SEPARATOR && instring.front() != CC_ENCODE_SEPARATOR_ALT))
		return error_unexpected_char(fn, output, outsize);

	instring = instring.substr(1, string::npos);

	//cerr << "iterations remaining string " << instring << endl;

	if (instring.length() < 6)
		return error_unexpected_char(fn, output, outsize);

	rc = cc_decode(fn, base57int, 57, false, instring.length() - 5, instring, bigval, output, outsize);
	if (rc) return rc;
	auto iterations = BIG64(bigval);

	//cerr << "remaining string " << instring << endl;

	string outs;
	uint64_t hash = siphash((const uint8_t *)msspec.data(), inlen - 5);
	cc_encode(base57, 57, 0UL, false, 5, hash, outs);
	//cerr << "hash " << hash << " " << outs << " " << instring << endl;
	if (outs != instring)
		return error_checksum_mismatch(fn, output, outsize);

	iterations *= ITERATIONS_SCALE;

	if (TRACE)
	{
		cerr << hex << "salt " << salt << dec << endl;
		cerr << "memory " << memory << endl;
		cerr << "iterations " << iterations << endl;
	}

	if (iterations)
	{
		rc = hash_passphrase(passphrase, salt, 0, memory, iterations, master_secret);
		if (rc)
			return error_unexpected(fn, output, outsize);
	}
	else
		master_secret = salt;

	return 0;
}

CCRESULT compute_secret(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	SpendSecretParams params;
	SpendSecrets secrets;

	memset(&params, 0, sizeof(params));
	memset(&secrets, 0, sizeof(secrets));

	auto rc = tx_secrets_from_json(fn, root, false, params, secrets, false, output, outsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	ostringstream os;
	os << hex;

	auto dash = fn.find('-');
	CCASSERT(dash != string::npos);

	os << "{\"" << fn.substr(dash + 1) << "\":\"0x";

	const SpendSecret& secret = secrets[0];

	if (fn == "compute-root-secret")
	{
		if (!secret.____have_master_secret)
			return error_missing_key(fn, "master-secret", output, outsize);
		os << secret.____root_secret;
	}
	else if (fn == "compute-spend-secret")
	{
		if (!secret.____have_master_secret && !secret.____have_root_secret)
			return error_missing_key(fn, "master-secret or root_secret", output, outsize);
		os << secret.____spend_secret;
	}
	else if (fn == "compute-trust-secret")
	{
		if (!secret.____have_master_secret && !secret.____have_root_secret && !secret.____have_spend_secret)
			return error_missing_key(fn, "master-secret, root_secret, or spend-secret", output, outsize);
		os << secret.____trust_secret;
	}
	else if (fn == "compute-monitor-secret")
	{
		if (!secret.____have_master_secret && !secret.____have_root_secret && !secret.____have_spend_secret && !secret.____have_trust_secret)
			return error_missing_key(fn, "master-secret, root_secret, spend-secret, or trust-secret", output, outsize);
		os << secret.____monitor_secret;
	}
	else if (fn == "compute-receive-secret")
	{
		if (!secret.____have_master_secret && !secret.____have_root_secret && !secret.____have_spend_secret && !secret.____have_trust_secret && !secret.____have_monitor_secret)
			return error_missing_key(fn, "master-secret, root_secret, spend-secret, trust-secret, or monitor_secret", output, outsize);
		os << secret.____receive_secret;
	}
	else
		CCASSERT(0);

	os << "\"}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

CCRESULT compute_serialnum_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	bigint_t commitment, commitnum;

	SpendSecretParams params;
	SpendSecrets secrets;

	memset(&params, 0, sizeof(params));
	memset(&secrets, 0, sizeof(secrets));

	auto rc = tx_secrets_from_json(fn, root, false, params, secrets, false, output, outsize);
	if (rc) return rc;

	const SpendSecret& secret = secrets[0];

	if (!secret.____have_master_secret && !secret.____have_root_secret && !secret.____have_spend_secret && !secret.____have_trust_secret && !secret.____have_monitor_secret)
		return error_missing_key(fn, "master-secret, root_secret, spend-secret, trust-secret, or monitor_secret", output, outsize);

	key = "commitment";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, commitment, output, outsize);
	if (rc) return rc;

	key = "commitment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, commitnum, output, outsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	// RULE tx input: S-serialnum = zkhash(@monitor_secret[0], M-commitment, M-commitnum)

	bigint_t serialnum;
	compute_serialnum(secret.____monitor_secret, commitment, BIG64(commitnum), serialnum);

	//cerr << hex << "address " << address << dec << endl;

	ostringstream os;
	os << hex;

	os << "{\"serial-number\":\"0x" << serialnum << "\"";
	os << "}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

CCRESULT payspec_from_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;
	bigint_t dest, type, amount;

	key = "destination";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, dest, output, outsize);
		if (rc) return rc;
	}
	else
	{
		SpendSecretParams params;
		SpendSecrets secrets;

		memset(&params, 0, sizeof(params));
		memset(&secrets, 0, sizeof(secrets));

		auto rc = tx_secrets_from_json(fn, root, true, params, secrets, false, output, outsize);
		if (rc) return rc;

		compute_destination(params, secrets, dest);
	}

	key = "type";
	bool has_type = false;
	(void)has_type;	// !!! for now, avoid unused variable compiler warning
	if (root.removeMember(key, &value))
	{
		has_type = true;
		// !!! this value should just be a string or single character, not an int
		auto rc = parse_int_value(fn, key, value.asString(), 0, 9UL, type, output, outsize);
		if (rc) return rc;
		if (BIG64(type) != 0)
			return error_invalid_value(fn, key, output, outsize);
	}

	key = "requested-amount";
	bool has_amount = false;
	if (root.removeMember(key, &value))
	{
		has_amount = true;
		auto rc = parse_int_value(fn, key, value.asString(), TX_AMOUNT_BITS, 0UL, amount, output, outsize);
		if (rc) return rc;
	}

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	if (TRACE)
	{
		cerr << "dest " << dest << endl;
		cerr << "type " << type << endl;
		cerr << "amount " << amount << endl;
		cerr << "extra info" << endl << root << endl;
	}

	string outs = "CC";
	outs += "0";		// type, always '0' for now

	cc_encode(base57, 57, TX_FIELD_MAX, false, 0, dest, outs);

	if (has_amount)
		cc_encode(base57, 57, 0UL, true, -1, amount, outs);

	outs.push_back(CC_ENCODE_SEPARATOR);

	// TODO: extract wallet url here

	outs.push_back(CC_ENCODE_SEPARATOR);

	uint64_t hash = siphash((const uint8_t *)outs.data(), outs.length());
	cc_encode(base57, 57, 0UL, false, 5, hash, outs);

#if 0	// skip extra for now
	bool has_extra = false;

	for (auto i = root.begin(); i != root.end(); ++i)
	{
		//cerr << i.name() << " : " i->asString() << endl;

		if (!i.name().length()) && !i->asString().length())
			continue;

		has_extra = true;

		outs.push_back(CC_ENCODE_SEPARATOR);
		encodestring(i.name(), outs);

		outs.push_back(CC_ENCODE_SEPARATOR);
		encodestring(i->asString(), outs);
	}

	if (has_extra)
	{
		outs.push_back(CC_ENCODE_SEPARATOR);
		uint64_t hash = siphash((const uint8_t *)outs.data(), outs.length());
		cc_encode(base57, 57, 0UL, false, 5, hash, outs);
	}
#endif

	outs = "{\"payspec\":\"" + outs + "\"}";

	//cerr << outs << endl;

	return copy_result_to_output(fn, outs, output, outsize);
}

CCRESULT payspec_to_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	//cerr << "payspec_to_json" << endl;

	string key;
	Json::Value value;

	key = "payspec";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	auto payspec = value.asString();

	//cerr << "payspec " << payspec << endl;

	if (payspec.length() < 3)
		return copy_error_to_output(fn, "error: invalid payspec length", output, outsize);

	char type = payspec[2];

	//cerr << "type " << type << endl;

	if (payspec.compare(0, 2, "CC") || (type != '0'))
		return copy_error_to_output(fn, "error: unrecognized payspec type", output, outsize);

	bigint_t dest, amount;

	auto inlen = payspec.length();
	string instring = payspec.substr(3);

	auto rc = cc_decode(fn, base57int, 57, false, 44, instring, dest, output, outsize);
	if (rc) return rc;

	//cerr << "amount string " << instring << endl;

	bool has_amount = false;
	if (!instring.empty() && instring.front() != CC_ENCODE_SEPARATOR && instring.front() != CC_ENCODE_SEPARATOR_ALT)
	{
		// !!! this code path needs to be tested

		has_amount = true;
		auto rc = cc_decode(fn, base57int, 57, true, 0, instring, amount, output, outsize);
		if (rc) return rc;

		auto test_val = amount;
		BIG64(test_val) &= ~TX_AMOUNT_MASK;
		if (test_val)
			return error_value_overflow(fn, "amount", TX_AMOUNT_BITS, TX_AMOUNT_MASK, output, outsize);
	}

	//cerr << "remaining string " << instring << endl;

	if (instring.empty() || (instring.front() != CC_ENCODE_SEPARATOR && instring.front() != CC_ENCODE_SEPARATOR_ALT))
		return error_unexpected_char(fn, output, outsize);

	instring = instring.substr(1, string::npos);

	// TODO: extract wallet url here

	if (instring.length() < 6 || (instring.front() != CC_ENCODE_SEPARATOR && instring.front() != CC_ENCODE_SEPARATOR_ALT))
		return error_unexpected_char(fn, output, outsize);

	string outs;
	uint64_t hash = siphash((const uint8_t *)payspec.data(), inlen - instring.length() + 1);
	cc_encode(base57, 57, 0UL, false, 5, hash, outs);
	//cerr << "hash " << outs << " " << instring << endl;
	if (outs != instring.substr(1,5))
		return error_checksum_mismatch(fn, output, outsize);

	if (TRACE)
	{
		cerr << "dest " << dest << endl;
		cerr << "type " << type << endl;
		cerr << "amount " << amount << endl;
	}

	ostringstream os;
	os << hex;

	os << "{\"payspec\":" JSON_ENDL
	os << "{\"destination\":\"0x" << dest << "\"" JSON_ENDL
	if (type != '0')
		os << ",\"type\":\"0x" << type << "\"" JSON_ENDL
	if (has_amount)
	{
		os << ",\"requested-amount\":\"0x" << amount << "\"" JSON_ENDL
	}
	os << "}}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

CCRESULT compute_address_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	bigint_t chain, dest, paynum;

	key = "destination";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, dest, output, outsize);
	if (rc) return rc;

	key = "destination-chain";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, chain, output, outsize);
	if (rc) return rc;

	key = "payment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_PAYNUM_BITS, 0UL, paynum, output, outsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	// RULE tx output: M_address = zkhash(#dest, dest_chain, #paynum)

	bigint_t address;
	compute_address(dest, BIG64(chain), BIG64(paynum), address);

	//cerr << hex << "address " << address << dec << endl;

	ostringstream os;
	os << hex;

	os << "{\"address\":\"0x" << address << "\"";
	os << "}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

CCRESULT compute_amount_encyption_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	bigint_t commit_iv, dest, paynum;

	key = "commitment-iv";
	if (root.removeMember(key, &value))
	{
		auto rc = parse_int_value(fn, key, value.asString(), TX_COMMIT_IV_BITS, 0UL, commit_iv, output, outsize);
		if (rc) return rc;
	}

	key = "destination";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, dest, output, outsize);
	if (rc) return rc;

	key = "payment-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_PAYNUM_BITS, 0UL, paynum, output, outsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	uint64_t asset_xor, amount_xor;

	compute_amount_pad(commit_iv, dest, BIG64(paynum), asset_xor, amount_xor);

	ostringstream os;
	os << hex;

	os << "{\"asset-encrypt-xor\":\"0x" << asset_xor << "\"";
	os << ",\"amount-encrypt-xor\":\"0x" << amount_xor << "\"";
	os << "}";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

#if SUPPORT_GENERATE_TEST_INPUTS

struct test_input_params
{
	bigint_t __dest;
	TxIn input;
	vector<bigint_t> path;
	array<bigint_t, TX_MERKLE_DEPTH+1> tree;
};

static CCRESULT generate_test_input(const string& fn, Json::Value& root, test_input_params& params, char *output, const uint32_t outsize)
{
	string key;
	Json::Value value;

	memset(&params, 0, sizeof(params));

	key = "destination";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, params.__dest, output, outsize);
	if (rc) return rc;

	rc = tx_input_common_from_json(fn, root, params.input, output, outsize);
	if (rc) return rc;

	compute_commitment(params.input.__M_commitment_iv, params.__dest, params.input.params.addrparams.__paynum,
			params.input.M_pool, params.input.__asset, params.input.__amount_fp, params.input._M_commitment);

	CCRandom(&params.input._M_commitnum, sizeof(uint64_t));
	if (TX_COMMITNUM_BITS < 64)
		params.input._M_commitnum &= ((uint64_t)1 << TX_COMMITNUM_BITS) - 1;
	//cerr << hex << params.input._M_commitnum << dec << endl;

	tx_commit_tree_hash_leaf(params.input._M_commitment, params.input._M_commitnum, params.tree[0]);

	//cerr << "set params[i].tree[0] to " << hex << params.tree[0] << dec << endl;

	return 0;
}

static void generate_tree(test_input_params params[])
{
	// only works when MAX_INPUTS_FOR_TESTING is a power of 2

	vector<CCHashInput> hashin(2);

	hashin[0].mask_higher_bits = (TX_MERKLE_BITS < TX_FIELD_BITS);

	for (unsigned i = 0; i < MAX_INPUTS_FOR_TESTING; ++i)
		params[i].path.resize(TX_MERKLE_DEPTH);

	unsigned width = MAX_INPUTS_FOR_TESTING;

	for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
	{
		//cerr << "level " << j << " width " << width << endl;

		if (width == 1)
		{
			params[1].tree[j].randomize();
			params[1].tree[j] = params[1].tree[j] * bigint_t(1UL);	// modulo prime

			width = 2;
			CCASSERT(MAX_INPUTS_FOR_TESTING >= width);

			//cerr << "set params[1].tree[" << j << "] to random " << hex << params[1].tree[j] << dec << endl;
		}

		for (unsigned i = 0; i < MAX_INPUTS_FOR_TESTING; ++i)
		{
			params[i].path[j] = params[(i/((uint64_t)1 << j)) ^ 1].tree[j];

			//cerr << "set params[" << i << "].path[" << j << "] to " << hex << params[i].path[j] << dec << endl;
		}

		for (unsigned i = 0; i < width/2; ++i)
		{
			tx_commit_tree_hash_node(params[2*i].tree[j], params[2*i + 1].tree[j], params[i].tree[j+1], j < TX_MERKLE_DEPTH - 1);

			//cerr << "set params[" << i << "].tree[" << j + 1 << "] to " << hex << params[i].tree[j+1] << dec << endl;
		}

		width /= 2;
	}

	for (unsigned i = 0; i < MAX_INPUTS_FOR_TESTING; ++i)
	{
		bigint_t root = CCHash::Merkle(params[i].tree[0], TX_MERKLE_BITS, params[i].path, TX_MERKLE_BITS);

		if (root != params[0].tree[TX_MERKLE_DEPTH])
			cerr << "error: params " << i << " merkle " << hex << root << " != " << params[0].tree[TX_MERKLE_DEPTH] << dec << endl;

		CCASSERT(root == params[0].tree[TX_MERKLE_DEPTH]);
	}
}

CCRESULT generate_test_inputs(const string& fn, Json::Value& root, char *output, const uint32_t outsize)
{
	if (!root.isArray())
		return error_not_array(fn, fn, output, outsize);

	if (root.size() > MAX_INPUTS_FOR_TESTING)
		return error_too_many_objs(fn, fn, MAX_INPUTS_FOR_TESTING, output, outsize);

	unique_ptr<test_input_params[]> params(new test_input_params[MAX_INPUTS_FOR_TESTING]);
	CCASSERT(params);

	for (unsigned i = 0; i < MAX_INPUTS_FOR_TESTING; ++i)
	{
		params[i].tree[0].randomize();
		params[i].tree[0] = params[i].tree[0] * bigint_t(1UL);	// modulo prime
	}

	auto echo = root;

	for (unsigned i = 0; i < root.size(); ++i)
	{
		auto rc = generate_test_input(fn, root[i], params[i], output, outsize);
		if (rc) return rc;
	}

	generate_tree(params.get());

	ostringstream os;
	os << hex;
	os << "\"merkle-root\":\"0x" << params[0].tree[TX_MERKLE_DEPTH] << "\"" JSON_ENDL
	os << ",\"inputs\" : [" JSON_ENDL

	for (unsigned i = 0; i < echo.size(); ++i)
	{
		if (i) os << ",";

		//os << "{\"commitment-index\":\"0x" << params[i].input.__M_commitment_index << "\"" JSON_ENDL
		os << "{\"commitment\":\"0x" << params[i].input._M_commitment << "\"" JSON_ENDL
		os << ",\"commitment-number\":\"0x" << params[i].input._M_commitnum << "\"" JSON_ENDL

		auto keys = echo[i].getMemberNames();

		for (unsigned j = 0; j < keys.size(); ++j)
		{
			os << ",\"" << keys[j] << "\":";
			auto value = echo[i][keys[j]];
			if (value.isNull())
				os << "null" JSON_ENDL
			else
			{
				auto sval = value.asString();
				if (!sval.empty() && sval.back() == 'L')
					sval.pop_back();
				os << "\"" << sval << "\"" JSON_ENDL
			}
		}

		os << ",\"merkle-path\":" JSON_ENDL
		os << "[";
		for (unsigned j = 0; j < TX_MERKLE_DEPTH; ++j)
		{
			if (j) os << ",";
			os << "\"0x" << params[i].path[j] << "\"" JSON_ENDL
		}
		os << "]}" JSON_ENDL
	}

	os << "]";

	//cerr << os.str() << endl;

	return copy_result_to_output(fn, os.str(), output, outsize);
}

#endif // SUPPORT_GENERATE_TEST_INPUTS
