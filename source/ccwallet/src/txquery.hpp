/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * txquery.hpp
*/

#pragma once

#include "txconn.hpp"

#include <CCbigint.hpp>
#include <CCparams.h>

#include <jsoncpp/json/json.h>

#define WALLET_MAX_QUERY_ADDRESS_RESULTS	8

class TxParams;
struct TxPay;

#define SERIALNUM_STATUS_UNSPENT	0
#define SERIALNUM_STATUS_PENDING	1
#define SERIALNUM_STATUS_SPENT		2

enum PowType
{
	PowType_None = 0,
	PowType_Query,
	PowType_Tx,
};

struct QueryAddressResult
{
	uint64_t blockchain;
	uint32_t pool;
	uint16_t asset_bits;
	uint16_t amount_bits;
	uint16_t exponent_bits;
	uint16_t encrypted;
	uint64_t asset;
	uint64_t amount_fp;
	uint64_t commitnum;
	snarkfront::bigint_t commit_iv;
	snarkfront::bigint_t commitment;
};

struct QueryAddressResults
{
	unsigned nresults;
	bool more_results;
	QueryAddressResult results[WALLET_MAX_QUERY_ADDRESS_RESULTS];
};

struct QueryInputResults
{
	uint64_t param_level;
	uint64_t param_time;
	snarkfront::bigint_t merkle_root;
	snarkfront::bigint_t merkle_paths[TX_MAXINPATH][TX_MERKLE_DEPTH];
};

class TxQuery : public TxConnection
{
	int TryQuery(PowType powtype, bool is_retry, vector<char> *pquery = NULL);
	int SubmitQuery(PowType powtype, bool is_retry, Json::Value *root = NULL, vector<char> *pquery = NULL);

	int ParseParams(Json::Value& root, TxParams& txparams);
	int ParseInputParams(Json::Value& root, TxParams& txparams);
	int ParseQueryAddressQueryResults(const snarkfront::bigint_t& address, const uint64_t commitstart, Json::Value root, QueryAddressResults &results);

	unsigned m_host_index;

public:
	bool m_possibly_sent;

	static CCServer::ConnectionFactoryInstantiation<TxQuery> txconnfac;
	static CCServer::ConnectionManagerBase nullconnmgr;

	TxQuery(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	TxConnection(manager, io_service, connfac)
	{
		ClearHost();
	}

	bool WasPossiblySent() const
	{
		return m_possibly_sent;
	}

	static int ReadHostsFile(const wstring &path);
	void ClearHost();
	const string& GetHost();

	int SubmitTx(const TxPay& ts, uint64_t& next_commitnum);

	int QueryParams(TxParams& txparams, vector<char> &querybuf);
	int QueryAddress(uint64_t blockchain, const snarkfront::bigint_t& address, const uint64_t commitstart, QueryAddressResults &results);
	int QuerySerialnums(uint64_t blockchain, const snarkfront::bigint_t *serialnums, unsigned nserials, uint16_t *statuses, snarkfront::bigint_t *hashkeys, uint64_t *tx_commitnums);
	int QueryInputs(const uint64_t *commitnum, const unsigned ncommits, TxParams& txparams, QueryInputResults &inputs);
};
