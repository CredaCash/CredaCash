/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * foreign-query.cpp
*/

#include "ccnode.h"
#include "foreign-query.hpp"
#include "foreign-query-btc.hpp"
#include "foreign-rpc.hpp"

#include <xtransaction-xreq.hpp>

string ForeignQueryResult::DebugString() const
{
	ostringstream out;

	out << "ForeignQueryResult";
	out << " blocktime " << blocktime;
	out << " confirmations " << confirmations;
	out << " amount " << amount;
	out << " memo " << memo;

	return out.str();
}

int ForeignQuery::QueryBlockHeight(uint64_t blockchain, uint64_t &height)
{
	height = 0;

	if (g_foreignrpc_client.NoQuery(blockchain))
		return -1;

	switch (blockchain)
	{
	case XREQ_BLOCKCHAIN_BTC:
	case XREQ_BLOCKCHAIN_BCH:
		return ForeignQueryBtc::QueryBlockHeight(blockchain, height);
	}

	return -1;
}

int ForeignQuery::QueryAddress(uint64_t blockchain, const string& addr)
{
	if (g_foreignrpc_client.NoQuery(blockchain))
		return -1;

	switch (blockchain)
	{
	case XREQ_BLOCKCHAIN_BTC:
	case XREQ_BLOCKCHAIN_BCH:
		return ForeignQueryBtc::QueryAddress(blockchain, addr);
	}

	return -1;
}

int ForeignQuery::QueryPayment(uint64_t blockchain, const string& block, const string& addr, const string& txid, ForeignQueryResult &result)
{
	result.Clear();

	if (g_foreignrpc_client.NoQuery(blockchain))
		return -1;

	string block_hash, addr_script;

	switch (blockchain)
	{
	case XREQ_BLOCKCHAIN_BTC:
	case XREQ_BLOCKCHAIN_BCH:
	{
		auto rc = ForeignQueryBtc::QueryBlock(blockchain, block, addr, block_hash, addr_script);
		if (rc) return rc;

		return ForeignQueryBtc::QueryPayment(blockchain, block_hash, txid, addr_script, result);
	}
	}

	return -1;
}
