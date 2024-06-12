/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * foreign-query.hpp
*/

#pragma once

#include <unifloat.hpp>

class ForeignQueryResult
{
public:

	UniFloat amount;
	uint64_t blocktime;
	unsigned confirmations;

	string memo;

	string DebugString() const;

	void Clear()
	{
		memset((void*)this, 0, (uintptr_t)&memo - (uintptr_t)this);

		memo.clear();
	}
};

class ForeignQuery
{
public:
	static int QueryBlockHeight(uint64_t blockchain, uint64_t &height);
	static int QueryAddress(uint64_t blockchain, const string& addr);
	static int QueryPayment(uint64_t blockchain, const string& block, const string& addr, const string& txid, ForeignQueryResult &result);
};
