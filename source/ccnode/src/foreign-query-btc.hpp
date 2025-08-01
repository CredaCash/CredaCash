/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * foreign-query-btc.hpp
*/

#pragma once

#include "foreign-query.hpp"

class ForeignQueryBtc
{
public:
	static int QueryBlockHeight(uint64_t blockchain, uint64_t &height);
	static int QueryAddress(uint64_t blockchain, const string& addr);
	static int QueryBlock(uint64_t blockchain, const string& block, const string& addr, string &block_hash, string &script);
	static int QueryPayment(uint64_t blockchain, const string& block, const string& txid, const string& script, ForeignQueryResult &result);
};
