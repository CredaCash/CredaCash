/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * totals.hpp
*/

#pragma once

#include "amounts.h"

#include <CCbigint.hpp>

#define TOTAL_TYPE_DA_BIT			(1 << 5)
#define TOTAL_TYPE_DA_DESTINATION	0
#define TOTAL_TYPE_DA_ACCOUNT		TOTAL_TYPE_DA_BIT

// note: balance reported by wallet is sum of (cleared - allocated + pending to a trusted and spendable destination)

// note: ALLOCATED	= 8
// note: PENDING	= 16

#define TOTAL_TYPE_PENDING_BIT		(1 << 4)
#define TOTAL_TYPE_ALLOCATED_BIT	(1 << 3)
#define TOTAL_TYPE_PA_BITS			(TOTAL_TYPE_PENDING_BIT | TOTAL_TYPE_ALLOCATED_BIT)
#define TOTAL_TYPE_PA_LOW			TOTAL_TYPE_ALLOCATED_BIT

#define TOTAL_TYPE_RB_BIT			(1 << 2)
#define TOTAL_TYPE_RB_BALANCE		0
#define TOTAL_TYPE_RB_RECEIVED		TOTAL_TYPE_RB_BIT

#define TOTAL_TYPE_TRACK_BIT		(1 << 1)
#define TOTAL_TYPE_WATCH_BIT		(1 << 0)
#define TOTAL_TYPE_TW_BITS			(TOTAL_TYPE_TRACK_BIT | TOTAL_TYPE_WATCH_BIT)
#define TOTAL_TYPE_TW_LOW			TOTAL_TYPE_WATCH_BIT

#define TOTAL_TYPE_MAX				(TOTAL_TYPE_DA_BIT | TOTAL_TYPE_PENDING_BIT | TOTAL_TYPE_RB_BALANCE)

#define	TX_MAXVAL_BITS				128
#define TX_MAXVAL_BYTES				((int)((TX_MAXVAL_BITS + 7) / 8))

//#define TEST_LOG_BALANCE	1

#ifndef TEST_LOG_BALANCE
#define TEST_LOG_BALANCE	0	// don't test
#endif

class DbConn;

class Total
{
public:

	unsigned type;
	uint64_t reference;		// account or destination
	uint64_t asset;
	unsigned delaytime;
	uint64_t blockchain;
	amtint_t total;			// the constructor memset's to zero all class members above this one

	Total();

	void Clear();
	void Copy(const Total& other);
	string DebugString() const;

	static bool TypeIsValid(unsigned type)
	{
		return type <= TOTAL_TYPE_MAX;
	}

	bool IsValid() const;

	static int AddBalances(DbConn *dbconn, bool rpc_throw, unsigned type, uint64_t account, uint64_t destination, uint64_t asset, unsigned delaytime, uint64_t blockchain, bool add, const snarkfront::bigint_t& amount);
	static int AddBalance(DbConn *dbconn, bool rpc_throw, unsigned type, uint64_t reference, uint64_t asset, unsigned delaytime, uint64_t blockchain, bool add, const snarkfront::bigint_t& amount);

	static int GetTotalBalance(DbConn *dbconn, bool rpc_throw, snarkfront::bigint_t& balance, unsigned type, bool sum_pc, bool incwatch, uint64_t reference = 0, uint64_t asset = 0, unsigned min_delaytime = 0, unsigned max_delaytime = -1, uint64_t min_blockchain = 0, uint64_t max_blockchain = -1, bool begin_db_read = true);
	static int GetTotalBalance(DbConn *dbconn, bool rpc_throw, amtint_t& balance, unsigned type, bool sum_pc, bool incwatch, uint64_t reference = 0, uint64_t asset = 0, unsigned min_delaytime = 0, unsigned max_delaytime = -1, uint64_t min_blockchain = 0, uint64_t max_blockchain = -1, bool begin_db_read = true);

	static snarkfront::bigint_t GetNoWaitNetRequired();
	static int AddNoWaitAmounts(const snarkfront::bigint_t& pending, bool add_pending, const snarkfront::bigint_t& required, bool add_required);
};
