/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * txquery.cpp
*/

#include "cclib.h"
#include "txquery.h"
#include "jsonutil.h"
#include "CCproof.h"
#include "xtransaction.hpp"
#include "xtransaction-xreq.hpp"

#include <CCobjects.hpp>
#include <unifloat.hpp>

static const uint8_t zero_pow[TX_POW_SIZE] = {};

CCRESULT tx_query_parameters_create(const string& fn, char *binbuf, const uint32_t binsize)
{
	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_PARAMS;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);

	if (bufpos > binsize)
		return 1;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}

static CCRESULT tx_query_parameters_json_create(const string& fn, const string& query, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	auto rc = tx_query_parameters_create(fn, binbuf, binsize);

	if (rc > 0)
		return error_buffer_overflow(fn, output, outsize);
	if (rc)
		return error_unexpected(fn, output, outsize);

	return 0;
}

CCRESULT tx_query_address_create(const string& fn, uint64_t blockchain, const bigint_t& address, const uint64_t commitstart, const uint16_t maxret, char *binbuf, const uint32_t binsize)
{
	//cerr << "address " << address << endl;
	//cerr << "commitstart " << commitstart << endl;
	//cerr << "maxret " << maxret << endl;

	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_ADDRESS;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);
	copy_to_buf(blockchain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);

	copy_to_buf(address, TX_ADDRESS_BYTES, bufpos, binbuf, binsize, bhex);
	copy_to_buf(commitstart, sizeof(commitstart), bufpos, binbuf, binsize, bhex);
	copy_to_buf(maxret, sizeof(maxret), bufpos, binbuf, binsize, bhex);

	if (bufpos > binsize)
		return 1;

	//cerr << "tx_query_address_create nbytes " << bufpos << endl;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}

static CCRESULT tx_query_address_json_create(const string& fn, const string& query, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	uint64_t blockchain;
	bigint_t address, bigval;
	uint64_t commitstart;
	uint16_t maxret;

	string key;
	Json::Value value;

	key = "blockchain";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	blockchain = BIG64(bigval);

	key = "address";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_ADDRESS_BITS, 0UL, address, output, outsize);
	if (rc) return rc;

	key = "commitment-number-start";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	rc = parse_int_value(fn, key, value.asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	commitstart = BIG64(bigval);

	key = "results-limit";
	if (root.removeMember(key, &value))
	{
		rc = parse_int_value(fn, key, value.asString(), 16, 0UL, bigval, output, outsize);
		if (rc) return rc;
		maxret = BIG64(bigval);
	}
	else
		maxret = -1;

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	rc = tx_query_address_create(fn, blockchain, address, commitstart, maxret, binbuf, binsize);

	if (rc > 0)
		return error_buffer_overflow(fn, output, outsize);
	if (rc)
		return error_unexpected(fn, output, outsize);

	return 0;
}

CCRESULT tx_query_inputs_create(const string& fn, uint64_t blockchain, const uint64_t *commitnum, const unsigned ncommits, char *binbuf, const uint32_t binsize)
{
	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_INPUTS;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);
	copy_to_buf(blockchain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);

	for (unsigned i = 0; i < ncommits; ++i)
	{
		//cerr << "commitnum " << commitnum << endl;

		copy_to_buf(commitnum[i], sizeof(commitnum[i]), bufpos, binbuf, binsize, bhex);
	}

	if (bufpos > binsize)
		return 1;

	//cerr << "tx_query_input_create nbytes " << bufpos << endl;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}

static CCRESULT tx_query_inputs_json_create(const string& fn, const string& query, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	uint64_t blockchain;
	uint64_t commitnum[TX_MAXINPATH];
	bigint_t bigval;

	string key;
	Json::Value value;

	key = "blockchain";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	blockchain = BIG64(bigval);

	key = "inputs";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);

	if (!value.isArray())
		return error_not_array_objs(fn, key, output, outsize);

	unsigned ncommits = value.size();

	if (ncommits > TX_MAXINPATH)
		return error_too_many_objs(fn, key, TX_MAXINPATH, output, outsize);

	for (unsigned i = 0; i < ncommits; ++i)
	{
		auto rc = parse_int_value(fn, query, value[i].asString(), TX_COMMITNUM_BITS, 0UL, bigval, output, outsize);
		if (rc) return rc;

		//cerr << "commitnum " << commitnum << endl;

		commitnum[i] = BIG64(bigval);
	}

	rc = tx_query_inputs_create(fn, blockchain, commitnum, ncommits, binbuf, binsize);

	if (rc > 0)
		return error_buffer_overflow(fn, output, outsize);
	if (rc)
		return error_unexpected(fn, output, outsize);

	return 0;
}

CCRESULT tx_query_serialnum_create(const string& fn, uint64_t blockchain, const bigint_t *serialnums, unsigned nserials, char *binbuf, const uint32_t binsize)
{
	//cerr << "serialnum " << serialnum << endl;

	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_SERIAL;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);
	copy_to_buf(blockchain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);

	for (unsigned i = 0; i < nserials; ++i)
		copy_to_buf(serialnums[i], TX_SERIALNUM_BYTES, bufpos, binbuf, binsize, bhex);

	if (bufpos > binsize)
		return 1;

	//cerr << "tx_query_serialnum_create nbytes " << bufpos << endl;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}

static CCRESULT tx_query_serialnum_json_create(const string& fn, const string& query, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	unsigned nserials = 0;
	uint64_t blockchain;
	bigint_t serialnums[TX_MAXIN];
	bigint_t bigval;

	string key;
	Json::Value value;

	key = "blockchain";
	if (!root.removeMember(key, &value))
		return error_missing_key(fn, key, output, outsize);
	auto rc = parse_int_value(fn, key, value.asString(), TX_CHAIN_BITS, 0UL, bigval, output, outsize);
	if (rc) return rc;
	blockchain = BIG64(bigval);

	key = "serial-numbers";
	if (root.removeMember(key, &value))
	{
		if (!value.isArray())
			return error_not_array_objs(fn, key, output, outsize);

		nserials = value.size();

		if (nserials < 1)
			return error_num_values(fn, key, 1, output, outsize);

		if (nserials > TX_MAXIN)
			return error_too_many_objs(fn, key, TX_MAXIN, output, outsize);

		for (unsigned i = 0; i < nserials; ++i)
		{
			auto rc = parse_int_value(fn, key, value[i].asString(), 0, TX_FIELD_MAX, serialnums[i], output, outsize);
			if (rc) return rc;
		}
	}

	key = "serial-number";
	if (!nserials && root.removeMember(key, &value))
	{
		nserials = 1;

		auto rc = parse_int_value(fn, key, value.asString(), 0, TX_FIELD_MAX, serialnums[0], output, outsize);
		if (rc) return rc;
	}
	else if (!nserials)
		return error_missing_key(fn, key, output, outsize);

	if (!root.empty())
		return error_unexpected_key(fn, root.begin().name(), output, outsize);

	rc = tx_query_serialnum_create(fn, blockchain, serialnums, nserials, binbuf, binsize);

	if (rc > 0)
		return error_buffer_overflow(fn, output, outsize);
	if (rc)
		return error_unexpected(fn, output, outsize);

	return 0;
}

CCRESULT tx_query_from_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize)
{
	if (!binbuf)
		return error_requires_binary_buffer(fn, output, outsize);

	*(uint32_t*)binbuf = 0;

	if (root.size() != 1)
		return copy_error_to_output(fn, "error: json tx query must contain exactly one object", output, outsize);

	auto it = root.begin();
	auto key = it.name();
	root = *it;

	if (key == "tx-parameters-query")
		return tx_query_parameters_json_create(fn, key, root, output, outsize, binbuf, binsize);

	if (key == "tx-address-query")
		return tx_query_address_json_create(fn, key, root, output, outsize, binbuf, binsize);

	if (key == "tx-input-query")
		return tx_query_inputs_json_create(fn, key, root, output, outsize, binbuf, binsize);

	if (key == "tx-serial-number-query")
		return tx_query_serialnum_json_create(fn, key, root, output, outsize, binbuf, binsize);

	return copy_error_to_output(fn, string("error: unrecognized tx query type \"") + key + "\"", output, outsize);
}

CCRESULT tx_query_xreqs_create(const string& fn, const unsigned xcx_type, const bigint_t& min_amount, const bigint_t& max_amount, const double& min_rate, const uint64_t base_asset, const uint64_t quote_asset, const string& foreign_asset, const uint16_t maxret, const uint16_t offset, unsigned flags, char *binbuf, const uint32_t binsize)
{
	uint32_t bufpos = 0;
	const bool bhex = false;

	// seller rounds down to offer a better rate; buyer rounds up to offer a better rate
	int rounding = -Xreq::RateSign(Xtx::TypeIsBuyer(xcx_type));
	if (!min_rate) rounding = 0;
	auto rate_fp = UniFloat::WireEncode(min_rate, rounding);

	//cerr << "tx_query_xreqs_create min_rate " << min_rate << " rounding " << rounding << " WireDecode " << UniFloat::WireDecode(rate_fp) << " diff " <<  min_rate - UniFloat::WireDecode(rate_fp).asFloat() << endl;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_XREQS;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);
	copy_to_buf(xcx_type, 1, bufpos, binbuf, binsize, bhex);
	copy_to_buf(min_amount, sizeof(min_amount), bufpos, binbuf, binsize, bhex);
	copy_to_buf(max_amount, sizeof(max_amount), bufpos, binbuf, binsize, bhex);
	copy_to_buf(rate_fp, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
	copy_to_buf(base_asset, sizeof(base_asset), bufpos, binbuf, binsize, bhex);
	copy_to_buf(quote_asset, sizeof(quote_asset), bufpos, binbuf, binsize, bhex);
	copy_to_buf(maxret, sizeof(maxret), bufpos, binbuf, binsize, bhex);
	copy_to_buf(offset, sizeof(offset), bufpos, binbuf, binsize, bhex);
	copy_to_buf(flags, 1, bufpos, binbuf, binsize, bhex);
	copy_to_bufp(foreign_asset.data(), foreign_asset.size(), bufpos, binbuf, binsize, bhex);

	if (bufpos > binsize)
		return 1;

	//cerr << "tx_query_xreqs_create nbytes " << bufpos << endl;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}

CCRESULT tx_query_xmatch_objid_create(const string& fn, uint64_t blockchain, const ccoid_t& objid, const uint16_t maxret, char *binbuf, const uint32_t binsize)
{
	//cerr << "objid " << buf2hex(&objid, CC_OID_TRACE_SIZE) << endl;
	//cerr << "maxret " << maxret << endl;

	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_XMATCH_OBJID;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);
	copy_to_buf(blockchain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);
	copy_to_buf(objid, sizeof(objid), bufpos, binbuf, binsize, bhex);
	copy_to_buf(maxret, sizeof(maxret), bufpos, binbuf, binsize, bhex);

	if (bufpos > binsize)
		return 1;

	//cerr << "tx_query_xmatch_objid_create nbytes " << bufpos << endl;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}

// TODO: add an array of xmatchnum's to this function
CCRESULT tx_query_xmatch_xreqnum_create(const string& fn, uint64_t blockchain, uint64_t xreqnum, uint64_t xmatchnum_start, const uint16_t maxret, char *binbuf, const uint32_t binsize)
{
	//cerr << "objid " << buf2hex(&objid, CC_OID_TRACE_SIZE) << endl;
	//cerr << "maxret " << maxret << endl;

	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_XMATCH_REQNUM;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);
	copy_to_buf(blockchain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);
	copy_to_buf(xreqnum, sizeof(xreqnum), bufpos, binbuf, binsize, bhex);
	copy_to_buf(maxret, sizeof(maxret), bufpos, binbuf, binsize, bhex);
	copy_to_buf(xmatchnum_start, sizeof(xmatchnum_start), bufpos, binbuf, binsize, bhex);

	if (bufpos > binsize)
		return 1;

	//cerr << "tx_query_xmatch_reqnum_create nbytes " << bufpos << endl;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}

CCRESULT tx_query_xmatch_xmatchnum_create(const string& fn, uint64_t blockchain, uint64_t xmatchnum, char *binbuf, const uint32_t binsize)
{
	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_XMATCH_MATCHNUM;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);
	copy_to_buf(blockchain, TX_CHAIN_BYTES, bufpos, binbuf, binsize, bhex);
	copy_to_buf(xmatchnum, sizeof(xmatchnum), bufpos, binbuf, binsize, bhex);

	if (bufpos > binsize)
		return 1;

	//cerr << "tx_query_xmatch_xmatchnum_create nbytes " << bufpos << endl;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}

CCRESULT tx_query_xmining_info_create(const string& fn, char *binbuf, const uint32_t binsize)
{
	uint32_t bufpos = 0;
	const bool bhex = false;

	copy_to_buf(bufpos, sizeof(bufpos), bufpos, binbuf, binsize, bhex);  // save space for size word

	uint32_t tag = CC_TAG_TX_QUERY_XMINING_INFO;
	copy_to_buf(tag, sizeof(tag), bufpos, binbuf, binsize, bhex);

	CCASSERT(bufpos == sizeof(CCObject::Header));

	copy_to_buf(zero_pow, sizeof(zero_pow), bufpos, binbuf, binsize, bhex);

	if (bufpos > binsize)
		return 1;

	//cerr << "tx_query_xmining_info_create nbytes " << bufpos << endl;

	memcpy(binbuf, &bufpos, sizeof(bufpos));

	return 0;
}
