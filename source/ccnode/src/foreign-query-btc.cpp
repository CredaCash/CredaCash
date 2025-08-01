/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * foreign-query-btc.cpp
*/

#include "ccnode.h"
#include "foreign-query-btc.hpp"
#include "foreign-rpc.hpp"

#include <xtransaction.hpp>
#include <xtransaction-xpay.hpp>
#include <jsonutil.h>

#define TRACE_FORN_RPC		g_params.trace_foreign_rpc

#define tx_query_retries	4

static int ParseBlockHeight(Json::Value& root, uint64_t &height)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::ParseBlockHeight ";

	height = 0;

	string key;
	Json::Value value;

	key = "result";
	if (!root.removeMember(key, &value))
		goto missing_key;

	if (!value.isIntegral() || !value.isConvertibleTo(Json::uintValue))
	{
		BOOST_LOG_TRIVIAL(debug) << "ForeignQueryBtc::ParseBlockHeight result not an uintValue";

		return -1;
	}

	height = value.asUInt64();

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(debug) << "ForeignQueryBtc::ParseBlockHeight result " << height;

	return 0;

missing_key:

	BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseBlockHeight error missing key " << key;

	return -1;
}

static int ParseBlockHash(Json::Value& root, string &block_hash)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::ParseBlockHash ";

	block_hash.clear();

	string key;
	Json::Value value;

	key = "result";
	if (!root.removeMember(key, &value))
		goto missing_key;

	if (!value.isString())
	{
		BOOST_LOG_TRIVIAL(debug) << "ForeignQueryBtc::ParseBlockHash result not string";

		return -1;
	}

	block_hash = value.asString();

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(debug) << "ForeignQueryBtc::ParseBlockHash result " << block_hash;

	return 0;

missing_key:

	BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseBlockHash error missing key " << key;

	return -1;
}

static int ParseAddress(Json::Value& root, const string& addr, string &script)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::ParseAddress " << addr;

	script.clear();

	string key;
	Json::Value value;

	key = "result";
	if (!root.removeMember(key, &value))
		goto missing_key;

	root = value;

	if (root.isNull())
	{
		BOOST_LOG_TRIVIAL(debug) << "ForeignQueryBtc::ParseAddress null result";

		return -1;
	}

	key = "isvalid";
	if (!root.removeMember(key, &value))
		goto missing_key;

	if (!value.isBool())
	{
		BOOST_LOG_TRIVIAL(debug) << "ForeignQueryBtc::ParseAddress " << key << " not boolean " << value.asString();

		return -1;
	}

	if (!value.asBool())
	{
		BOOST_LOG_TRIVIAL(debug) << "ForeignQueryBtc::ParseAddress " << key << " not true";

		return 1;
	}

	key = "address";
	if (!root.removeMember(key, &value))
		goto missing_key;
	key = value.asString();

	{
		auto pos = key.find(':');
		if (pos != string::npos)
			key.erase(0, pos + 1);
	}

	if (key != addr)
	{
		BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseAddress address mismatch " << value.asString() << " != " << addr;

		return -1;
	}

	key = "scriptPubKey";
	if (!root.removeMember(key, &value))
		goto missing_key;

	script = value.asString();

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(debug) << "ForeignQueryBtc::ParseAddress " << addr << " found " << key << " " << script;

	return 0;

missing_key:

	BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseAddress error missing key " << key;

	return -1;
}

static int ParseTx(const char* json, Json::Value& root, const string& block, const string& txid, const string& script, ForeignQueryResult &result)
{
	result.Clear();

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::ParseTx block " << block << " txid " << txid << " script " << script;

	string key;
	Json::Value value, value2;

	key = "result";
	if (!root.removeMember(key, &value))
		goto missing_key;

	root = value;

	if (root.isNull())
	{
		BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseTx null result block " << block << " txid " << txid << " script " << script;

		return -1;
	}

	key = "blockhash";
	if (!root.removeMember(key, &value))
		goto missing_key;

	if (value.asString() != block)
	{
		BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseTx block mismatch " << value.asString() << " != " << block;

		return -1;
	}

	key = "txid";
	if (!root.removeMember(key, &value))
		goto missing_key;

	if (value.asString() != txid)
	{
		BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseTx txid mismatch " << value.asString() << " != " << txid;

		return -1;
	}

	key = "confirmations";
	if (!root.removeMember(key, &value))
		goto missing_key;

	if (!value.isIntegral())
		goto not_numeric;

	if (!value.isConvertibleTo(Json::uintValue) || value.asInt() <= 0)
	{
		BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::ParseTx confirmations " << value.asString();

		return -1;
	}

	result.confirmations = value.asUInt();

	key = "blocktime";
	if (!root.removeMember(key, &value))
		goto missing_key;

	if (!value.isIntegral() || !value.isConvertibleTo(Json::uintValue))
		goto not_numeric;

	result.blocktime = value.asUInt64();

	// note: there's no easy RPC to get a tx's fee (would have to get all inputs and subtract all outputs?)

	key = "vout";
	if (!root.removeMember(key, &value))
		goto missing_key;

	root = value;

	if (!root.isArray())
	{
		BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseTx " << key << " not an arry";

		return -1;
	}

	result.amount = 0;

	for (unsigned i = 0; i < root.size(); ++i)
	{
		key = "scriptPubKey";
		if (!root[i].removeMember(key, &value))
			goto missing_key;

		key = "hex";
		if (!value.removeMember(key, &value2))
			goto missing_key;

		if (value2.asString() != script)
			continue;

		key = "value";
		if (!root[i].removeMember(key, &value))
			goto missing_key;

		if (!value.isNumeric())
			goto not_numeric;

		amtfloat_t amountf;
		auto rc = parse_float_value(json, value, amountf);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseTx amount parse error " << value.asString();

			return -1;
		}

		auto amount = UniFloat((double)amountf);

		if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::ParseTx block " << block << " txid " << txid << " script " << script << " found txout amount " << amount;

		amount = UniFloat::Multiply(amount, SATOSHI_PER_BITCOIN);
		amount = UniFloat::Round(amount);
		result.amount = UniFloat::Add(result.amount, amount);
	}

	result.amount = UniFloat::Divide(result.amount, SATOSHI_PER_BITCOIN);

	return 0;

missing_key:

	BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseTx error missing key " << key;

	return -1;

not_numeric:

	BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::ParseTx " << key << " not numeric " << value.asString();

	return -1;
}

int ForeignQueryBtc::QueryBlockHeight(uint64_t blockchain, uint64_t &height)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::QueryBlockHeight blockchain";

	if (blockchain > XREQ_BLOCKCHAIN_MAX)
		return -1;
	auto port = g_foreignrpc_client.rpc_port[blockchain];
	auto auth = g_foreignrpc_client.rpc_auth[blockchain];
	if (!port)
		return -1;

	int result_code = -1;

	string query = "{\"method\":\"getblockcount\",\"params\":[]}";

	for (int i = 0; i <= tx_query_retries && !g_shutdown; ++i)
	{
		auto pconn = g_foreignrpc_client.GetConnection();
		if (!pconn)
			return -1;

		Json::Value root;

		auto rc = pconn->SubmitQuery(port, auth, query, &root);
		if (rc) continue;

		result_code = ParseBlockHeight(root, height);
		if (result_code < 0) continue;

		break;
	}

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::QueryBlockHeight result code " << result_code;

	return result_code;
}

int ForeignQueryBtc::QueryAddress(uint64_t blockchain, const string& addr)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::QueryAddress blockchain " << blockchain << " addr " << addr;

	if (blockchain > XREQ_BLOCKCHAIN_MAX)
		return -1;
	auto port = g_foreignrpc_client.rpc_port[blockchain];
	auto auth = g_foreignrpc_client.rpc_auth[blockchain];
	if (!port)
		return -1;

	int result_code = -1;

	string query = "{\"method\":\"validateaddress\",\"params\":[\"" + addr + "\"]}";

	for (int i = 0; i <= tx_query_retries && !g_shutdown; ++i)
	{
		auto pconn = g_foreignrpc_client.GetConnection();
		if (!pconn)
			return -1;

		Json::Value root;
		string script;

		auto rc = pconn->SubmitQuery(port, auth, query, &root);
		if (rc) continue;

		result_code = ParseAddress(root, addr, script);
		if (result_code < 0) continue;

		break;
	}

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::QueryAddress result code " << result_code;

	return result_code;
}

int ForeignQueryBtc::QueryBlock(uint64_t blockchain, const string& block, const string& addr, string &block_hash, string &script)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::QueryBlock blockchain " << blockchain << " block " << block << " addr " << addr;

	if (blockchain > XREQ_BLOCKCHAIN_MAX)
		return -1;
	auto port = g_foreignrpc_client.rpc_port[blockchain];
	auto auth = g_foreignrpc_client.rpc_auth[blockchain];
	if (!port)
		return -1;

	int result_code = -1;

	string query =	"[{\"method\":\"getblockhash\",\"params\":[" + block + "]},"
					 "{\"method\":\"validateaddress\",\"params\":[\"" + addr + "\"]}]";	// validate address in this query so second query returns less data

	for (int i = 0; i <= tx_query_retries && !g_shutdown; ++i)
	{
		block_hash.clear();
		script.clear();

		auto pconn = g_foreignrpc_client.GetConnection();
		if (!pconn)
			return -1;

		Json::Value root;

		auto rc = pconn->SubmitQuery(port, auth, query, &root);
		if (rc) continue;

		if (!root.isArray())
		{
			BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::QueryBlock error root not an array";
			continue;
		}

		if (root.size() != 2)
		{
			BOOST_LOG_TRIVIAL(info) << "ForeignQueryBtc::QueryBlock error root size " << root.size();
			continue;
		}

		result_code = ParseBlockHash(root[0], block_hash);
		if (result_code < 0) continue;

		result_code = ParseAddress(root[1], addr, script);
		if (result_code < 0) continue;

		break;
	}

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::QueryBlock result code " << result_code << " block_hash " << block_hash << " script " << script;

	return result_code;
}

int ForeignQueryBtc::QueryPayment(uint64_t blockchain, const string& block, const string& txid, const string& script, ForeignQueryResult &result)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::QueryPayment blockchain " << blockchain << " block " << block << " txid " << txid << " script " << script;

	if (blockchain > XREQ_BLOCKCHAIN_MAX)
		return -1;
	auto port = g_foreignrpc_client.rpc_port[blockchain];
	auto auth = g_foreignrpc_client.rpc_auth[blockchain];
	if (!port)
		return -1;

	int result_code = -1;

	string query = "{\"method\":\"getrawtransaction\",\"params\":[\"" + txid + "\",true,\"" + block + "\"]}";

	//query = "{\"method\":\"uptime\",\"params\":[]}";	// for testing

	for (int i = 0; i <= tx_query_retries && !g_shutdown; ++i)
	{
		result.Clear();

		auto pconn = g_foreignrpc_client.GetConnection();
		if (!pconn)
			return -1;

		const char* json;
		Json::Value root;

		auto rc = pconn->SubmitQuery(port, auth, query, &root, &json);
		if (rc) continue;

		result_code = ParseTx(json, root, block, txid, script, result);
		if (result_code < 0) continue;

		break;
	}

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << "ForeignQueryBtc::QueryPayment blockchain " << blockchain << " block " << block << " txid " << txid << " script " << script << " result code " << result_code << " " << result.DebugString();

	return result_code;
}
