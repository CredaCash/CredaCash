/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * txquery.hpp
*/

#pragma once

#include "txconn.hpp"

#include <CCobjdefs.h>
#include <CCbigint.hpp>
#include <CCparams.h>
#include <BlockChainStatus.hpp>
#include <xmatch.hpp>

#include <jsoncpp/json/json.h>

#define WALLET_QUERY_ADDRESS_MAX_RESULTS		8
#define WALLET_QUERY_XREQS_MAX_RESULTS			200
#define WALLET_QUERY_XMATCHREQ_MAX_MATCHNUMS	6
#define WALLET_QUERY_XMATCHREQ_MAX_RESULTS		6

#define SERIALNUM_STATUS_UNSPENT	0
#define SERIALNUM_STATUS_PENDING	1
#define SERIALNUM_STATUS_SPENT		2

#define CCXFLOAT_STRING_PREFIX		"ccxfloat:"

class TxParams;
struct TxPay;

enum PowType
{
	PowType_None = 0,
	PowType_Query,
	PowType_Tx
};

class QueryAddressResult
{
public:
	uint64_t blockchain;
	uint32_t domain;
	uint16_t is_special_domain;
	uint16_t asset_bits;
	uint16_t amount_bits;
	uint16_t exponent_bits;
	uint16_t encrypted;
	uint64_t asset;
	uint64_t amount_fp;
	uint64_t commitnum;
	snarkfront::bigint_t commit_iv;
	snarkfront::bigint_t commitment;

	void Clear()
	{
		memset((void*)this, 0, sizeof(*this));
	}
};

class QueryAddressResults
{
public:
	unsigned nresults;
	bool more_results;
	QueryAddressResult results[WALLET_QUERY_ADDRESS_MAX_RESULTS];

	void Clear()
	{
		memset((void*)this, 0, sizeof(*this));
	}
};

class QueryInputResults
{
public:
	uint64_t param_level;
	uint64_t param_time;
	snarkfront::bigint_t merkle_root;
	snarkfront::bigint_t merkle_paths[TX_MAXINPATH][TX_MERKLE_DEPTH];

	void Clear()
	{
		memset((void*)this, 0, sizeof(*this));
	}
};

class QueryXreqsResults
{
public:
	uint64_t server_timestamp;
	uint64_t blockchain;
	BlockChainStatus blockchain_status;
	unsigned nresults;
	bool more_results;
	Json::Value json;

	void Clear()
	{
		memset((void*)this, 0, (uintptr_t)&json - (uintptr_t)this);

		json.clear();
	}
};

class QueryXmatchreqResults
{
public:
	uint64_t xreqnum;
	uint64_t server_timestamp;
	BlockChainStatus blockchain_status;
	unsigned disposition;
	snarkfront::bigint_t open_amount;
	unsigned nresults;
	bool more_results;
	Xmatch xmatches[WALLET_QUERY_XMATCHREQ_MAX_RESULTS];

	void Clear()
	{
		memset((void*)this, 0, (uintptr_t)&xmatches - (uintptr_t)this);

		for (unsigned i = 0; i < WALLET_QUERY_XMATCHREQ_MAX_RESULTS; ++i)
			xmatches[i].Clear();
	}
};

class QueryXmatchResults
{
public:
	uint64_t server_timestamp;
	BlockChainStatus blockchain_status;
	Xmatch xmatch;

	void Clear()
	{
		memset((void*)this, 0, (uintptr_t)&xmatch - (uintptr_t)this);

		xmatch.Clear();
	}
};

class QueryXreqsMiningInfoResults
{
public:
	uint64_t blockchain;
	uint64_t server_timestamp;
	Json::Value json;

	void Clear()
	{
		memset((void*)this, 0, (uintptr_t)&json - (uintptr_t)this);

		json.clear();
	}
};

class TxQuery : public TxConnection
{
	int TryQuery(PowType powtype, vector<char> *pquery = NULL);
	int SubmitQuery(PowType powtype, uint64_t expire_time, bool is_retry, Json::Value *root, vector<char> *pquery = NULL, bool skip_prepare = false, bool debug = false);

	int TxToWire(TxPay& ts);
	int DoSubmitTx(uint64_t expire_time, uint64_t& next_commitnum, vector<char>& wire, bool skip_prepare, bool debug);

	int ParseParams(Json::Value& root, TxParams& txparams);
	int ParseBlockChainStatus(Json::Value& root, BlockChainStatus& blockchain_status);
	int ParseInputParams(Json::Value& root, TxParams& txparams);
	int ParseQueryAddressResults(const snarkfront::bigint_t& address, const uint64_t commitstart, Json::Value root, QueryAddressResults &results);
	int ParseQueryXreqsResults(const unsigned xcx_type, const snarkfront::bigint_t& min_amount, const snarkfront::bigint_t& max_amount, const double& min_rate, const double& base_costs, const double& quote_costs, const uint64_t base_asset, const uint64_t quote_asset, const string& foreign_asset, unsigned maxret, unsigned offset, QueryXreqsResults &results);
	int ParseQueryXmatchreq(Json::Value root, Xmatch &match, Xmatchreq &matchreq);
	int ParseQueryXmatch(Json::Value root, Xmatch &match);
	int ParseQueryXmatchreqResults(uint64_t blockchain, const ccoid_t& objid, uint64_t reqnum, uint64_t matchnum_start, Json::Value root, QueryXmatchreqResults &results);
	int ParseQueryXmatchResults(uint64_t blockchain, uint64_t matchnum, Json::Value root, QueryXmatchResults &results);
	int ParseQueryXminingInfoResults(QueryXreqsMiningInfoResults &results);

	unsigned m_host_index[2];

public:
	bool m_possibly_sent;

	static CCServer::ConnectionFactoryInstantiation<TxQuery> txconnfac;
	static CCServer::ConnectionManagerBase nullconnmgr;

	TxQuery(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	TxConnection(manager, io_service, connfac)
	{
		m_host_index[0] = -1;
		m_host_index[1] = -1;
	}

	bool WasPossiblySent() const
	{
		return m_possibly_sent;
	}

	static int ReadHostsFile(bool brelay, const wstring& path);
	void AdvanceHost(PowType powtype, bool retry);
	const string& GetHost(PowType powtype);

	int PrepareTx(TxPay& ts, uint64_t expire_time, vector<char>& wire);
	int PrepareQuery(PowType powtype, uint64_t expire_time, bool is_retry, vector<char> *pquery = NULL);

	int SubmitTx(TxPay& ts, uint64_t expire_time, uint64_t& next_commitnum, bool debug = false);
	int SubmitPreparedTx(uint64_t& next_commitnum, vector<char>& wire, bool debug = false);

	int QueryParams(TxParams& txparams, vector<char> &querybuf);
	int QueryAddress(uint64_t blockchain, const snarkfront::bigint_t& address, const uint64_t commitstart, QueryAddressResults &results);
	int QuerySerialnums(uint64_t blockchain, const snarkfront::bigint_t *serialnums, unsigned nserials, uint16_t *statuses, snarkfront::bigint_t *hashkeys, uint64_t *tx_commitnums);
	int QueryInputs(const uint64_t *commitnum, const unsigned ncommits, TxParams& txparams, QueryInputResults &inputs);
	int QueryXreqs(const unsigned xcx_type, const snarkfront::bigint_t& min_amount, const snarkfront::bigint_t& max_amount, const double& min_rate, const double& base_costs, const double& quote_costs, const uint64_t base_asset, const uint64_t quote_asset, const string& foreign_asset, unsigned maxret, unsigned offset, unsigned flags, QueryXreqsResults &results);
	int QueryXmatchreq(uint64_t blockchain, const ccoid_t& objid, uint64_t reqnum, uint64_t matchnum_start, QueryXmatchreqResults &results);
	int QueryXmatch(uint64_t blockchain, uint64_t matchnum, QueryXmatchResults &results);
	int QueryXminingInfo(QueryXreqsMiningInfoResults &results);
};
