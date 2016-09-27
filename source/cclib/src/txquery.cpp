/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * txquery.cpp
*/

#include "CCdef.h"
#include "txquery.h"
#include "jsonutil.h"
#include "CCproof.h"

#include <CCobjects.hpp>
#include <CCUtil.h>

static const uint8_t zero_pow[TX_POW_SIZE] = {};

static CCRESULT tx_query_parameters_create(const string& fn, Json::Value& root, char *output, const uint32_t bufsize, const bool bhex = false)
{
	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	uint32_t bufpos = 0;

	copy_to_buf(&bufpos, sizeof(bufpos), bufpos, output, bufsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_PARAMS;
	copy_to_buf(&tag, sizeof(tag), bufpos, output, bufsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(&zero_pow, sizeof(zero_pow), bufpos, output, bufsize, bhex);

	if (bufpos > bufsize)
		return copy_error_to_output(fn + " error: output buffer overflow", output, bufsize);

	memcpy(output, &bufpos, sizeof(bufpos));

	return 0;
}

static CCRESULT tx_query_address_create(const string& fn, Json::Value& root, char *output, const uint32_t bufsize, const bool bhex = false)
{
	bigint_t address, commitstart;

	string key;
	Json::Value value;

	key = "address";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, address, output, bufsize);
	if (rc) return rc;

	key = "commitment-number-start";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	rc = parse_int_value(fn, key, value.asString(), TX_MERKLE_LEAFINDEX_BITS, 0UL, commitstart, output, bufsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	//cerr << "address " << address << endl;
	//cerr << "commitstart " << commitstart << endl;

	uint32_t bufpos = 0;

	copy_to_buf(&bufpos, sizeof(bufpos), bufpos, output, bufsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_ADDRESS;
	copy_to_buf(&tag, sizeof(tag), bufpos, output, bufsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(&zero_pow, sizeof(zero_pow), bufpos, output, bufsize, bhex);
	copy_to_buf(&address, sizeof(address), bufpos, output, bufsize, bhex);
	copy_to_buf(&BIG64(commitstart), sizeof(BIG64(commitstart)), bufpos, output, bufsize, bhex);

	if (bufpos > bufsize)
		return copy_error_to_output(fn + " error: output buffer overflow", output, bufsize);

	//cerr << "tx_query_address_create nbytes " << bufpos << endl;

	memcpy(output, &bufpos, sizeof(bufpos));

	return 0;
}

static CCRESULT tx_query_input_create(const string& fn, Json::Value& root, char *output, const uint32_t bufsize, const bool bhex = false)
{
	uint32_t bufpos = 0;

	copy_to_buf(&bufpos, sizeof(bufpos), bufpos, output, bufsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_INPUTS;
	copy_to_buf(&tag, sizeof(tag), bufpos, output, bufsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(&zero_pow, sizeof(zero_pow), bufpos, output, bufsize, bhex);

	bigint_t address, commitstart;

	string key;
	Json::Value value;

	//cerr << "txpay_outputs_from_json " << root.size()<< endl;

	if (!root.isArray()) // no longer enforced: || root.size() < 1)
		return error_not_array_objs(fn, key, output, bufsize);

	if (root.size() > TX_MAXINPATH)
		return error_too_many_objs(fn, key, TX_MAXINPATH, output, bufsize);

	for (unsigned i = 0; i < root.size(); ++i)
	{
		if (!root[i].isObject())
			return error_not_array_objs(fn, key, output, bufsize);

		key = "address";
		if (!root[i].removeMember(key, &value))
			return error_missing_key(fn, key, output, bufsize);
		auto rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, address, output, bufsize);
		if (rc) return rc;

		key = "commitment-number-start";
		if (!root[i].removeMember(key, &value))
			return error_missing_key(fn, key, output, bufsize);
		rc = parse_int_value(fn, key, value.asString(), TX_MERKLE_LEAFINDEX_BITS, 0UL, commitstart, output, bufsize);
		if (rc) return rc;

		if (!root[i].empty())
			return error_unexpected_key(fn, root[i].begin().name(), output, bufsize);

		//cerr << "address " << address << endl;
		//cerr << "commitstart " << commitstart << endl;

		copy_to_buf(&address, sizeof(address), bufpos, output, bufsize, bhex);
		copy_to_buf(&BIG64(commitstart), sizeof(BIG64(commitstart)), bufpos, output, bufsize, bhex);
	}

	if (bufpos > bufsize)
		return copy_error_to_output(fn + " error: output buffer overflow", output, bufsize);

	//cerr << "tx_query_input_create nbytes " << bufpos << endl;

	memcpy(output, &bufpos, sizeof(bufpos));

	return 0;
}

static CCRESULT tx_query_serialnum_create(const string& fn, Json::Value& root, char *output, const uint32_t bufsize, const bool bhex = false)
{
	bigint_t serialnum;

	string key;
	Json::Value value;

	key = "serial-number";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, bufsize);
	auto rc = parse_int_value(fn, key, value.asString(), 0, TX_INPUT_MAX, serialnum, output, bufsize);
	if (rc) return rc;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, bufsize);

	//cerr << "serialnum " << serialnum << endl;

	uint32_t bufpos = 0;

	copy_to_buf(&bufpos, sizeof(bufpos), bufpos, output, bufsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_SERIAL;
	copy_to_buf(&tag, sizeof(tag), bufpos, output, bufsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(&zero_pow, sizeof(zero_pow), bufpos, output, bufsize, bhex);
	copy_to_buf(&serialnum, sizeof(serialnum), bufpos, output, bufsize, bhex);

	if (bufpos > bufsize)
		return copy_error_to_output(fn + " error: output buffer overflow", output, bufsize);

	//cerr << "tx_query_serialnum_create nbytes " << bufpos << endl;

	memcpy(output, &bufpos, sizeof(bufpos));

	return 0;
}

CCRESULT tx_query_from_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize)
{
	if (root.size() != 1)
		return copy_error_to_output(fn + ": json tx query must contain exactly one object", output, bufsize);

	string key;
	Json::Value value;

	auto it = root.begin();
	key = it.name();
	root = *it;

	if (key == "tx-parameters-query")
		return tx_query_parameters_create(fn, root, output, bufsize);

	if (key == "tx-address-query")
		return tx_query_address_create(fn, root, output, bufsize);

	if (key == "tx-input-query")
		return tx_query_input_create(fn, root, output, bufsize);

	if (key == "tx-serial-number-query")
		return tx_query_serialnum_create(fn, root, output, bufsize);

	return copy_error_to_output(fn + " error: unrecognized tx query type \"" + key + "\"", output, bufsize);
}
