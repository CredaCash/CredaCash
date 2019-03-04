/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * totals.cpp
*/

#include "ccwallet.h"
#include "totals.hpp"
#include "walletdb.hpp"

#include <transaction.hpp>

#define TRACE_TOTALS	(g_params.trace_totals)

using namespace snarkfront;

Total::Total()
{
	Clear();
}

void Total::Clear()
{
	memset(this, 0, (uintptr_t)&total - (uintptr_t)this);

	type = TOTAL_TYPE_MAX + 1;
	total = 0UL;
}

void Total::Copy(const Total& other)
{
	memcpy(this, &other, (uintptr_t)&total - (uintptr_t)this);

	total = other.total;
}

string Total::DebugString() const
{
	ostringstream out;

	out << "type " << type;
	out << " reference " << reference;
	out << " asset " << asset;
	out << " delaytime " << delaytime;
	out << " blockchain " << blockchain;
	out << " total " << total;

	return out.str();
}

bool Total::TypeIsValid(unsigned type)
{
	return type <= TOTAL_TYPE_MAX;
}

bool Total::IsValid() const
{
	return TypeIsValid(type);
}

int Total::AddBalances(DbConn *dbconn, unsigned type, uint64_t account, uint64_t destination, uint64_t asset, unsigned delaytime, uint64_t blockchain, bool add, const bigint_t& amount)
{
	// call this function only within a BeginWrite

	// adds amount to wallet, account and destination

	if (TRACE_TOTALS) BOOST_LOG_TRIVIAL(debug) << "Total::AddBalances type " << type << " account " << account << " destination " << destination << " asset " << asset << " delaytime " << delaytime << " blockchain " << blockchain << " add " << add << " amount " << amount;

	auto type2 = type & ~TOTAL_TYPE_RB_RECEIVED;

	auto rc = AddBalance(dbconn, type2 | TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, 0, asset, delaytime, blockchain, add, amount);
	if (rc) return rc;

	rc = AddBalance(dbconn, type2 | TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_BALANCE, account, asset, delaytime, blockchain, add, amount);
	if (rc) return rc;

	if (type & TOTAL_TYPE_RB_RECEIVED)
	{
		rc = AddBalance(dbconn, type | TOTAL_TYPE_DA_ACCOUNT, account, asset, delaytime, blockchain, add, amount);
		if (rc) return rc;

		auto type2 = type & ~TOTAL_TYPE_TW_BITS;

		rc = AddBalance(dbconn, type2 | TOTAL_TYPE_DA_DESTINATION, 0, asset, delaytime, blockchain, add, amount);
		if (rc) return rc;

		CCASSERT(destination);

		rc = AddBalance(dbconn, type2 | TOTAL_TYPE_DA_DESTINATION, destination, asset, delaytime, blockchain, add, amount);
		if (rc) return rc;
	}

	return 0;
}

int Total::AddBalance(DbConn *dbconn, unsigned type, uint64_t reference, uint64_t asset, unsigned delaytime, uint64_t blockchain, bool add, const bigint_t& amount)
{
	// call this function only within a BeginWrite

	if (TRACE_TOTALS) BOOST_LOG_TRIVIAL(debug) << "Total::AddBalance type " << type << " reference " << reference << " asset " << asset << " delaytime " << delaytime << " blockchain " << blockchain << " add " << add << " amount " << amount;

	if (!amount)
		return 0;

	Total total;

	total.type = type;
	total.reference = reference;
	total.asset = asset;
	total.delaytime = delaytime;
	total.blockchain = blockchain;

	auto rc = dbconn->TotalSelectMatch(true, total);
	if (rc < 0) return rc;

	amtint_t vi;
	amount_from_bigint(amount, vi);

	if (add)
		total.total += vi;
	else
		total.total -= vi;

	rc = dbconn->TotalInsert(total);

	if (!(type & (TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_RECEIVED)))
	{
		if (TRACE_TOTALS) BOOST_LOG_TRIVIAL(info) << "Total::AddBalance done type " << type << " reference " << reference << " add " << add << " amount " << vi << " new total " << total.total;
		//cerr << "AddBalance type " << type << " add " << add << " amount " << vi << " new total " << total.total << endl;
	}

	return rc;
}

int Total::GetTotalBalance(DbConn *dbconn, bigint_t& balance, unsigned type, bool sum_pc, bool incwatch, uint64_t reference, uint64_t asset, unsigned min_delaytime, unsigned max_delaytime, uint64_t min_blockchain, uint64_t max_blockchain, bool begin_db_read)
{
	balance = 0UL;

	amtint_t balancei;

	auto rc = GetTotalBalance(dbconn, balancei, type, sum_pc, incwatch, reference, asset, min_delaytime, max_delaytime, min_blockchain, max_blockchain, begin_db_read);
	if (rc) return rc;
	if (balancei < 0) return -1;

	amount_to_bigint(balancei, balance);

	return 0;
}

int Total::GetTotalBalance(DbConn *dbconn, amtint_t& balance, unsigned type, bool sum_pc, bool incwatch, uint64_t reference, uint64_t asset, unsigned min_delaytime, unsigned max_delaytime, uint64_t min_blockchain, uint64_t max_blockchain, bool begin_db_read)
{
	// sums the confirmed and pending totals across all blockchains and delaytimes

	if (max_delaytime > TX_DELAYTIME_MASK)
		max_delaytime = TX_DELAYTIME_MASK;

	balance = 0UL;

	if (begin_db_read)
	{
		auto rc = dbconn->BeginRead();
		if (rc)
		{
			dbconn->DoDbFinishTx(-1);

			return -1;
		}
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	if (!begin_db_read)
		finally.Clear();

	for (unsigned pending = 0; pending <= 2U * sum_pc; ++pending)
	{
		if (sum_pc && (type & TOTAL_TYPE_PA_BITS))
		{
			if ((type & TOTAL_TYPE_PA_BITS) == TOTAL_TYPE_PENDING_BIT)
			{
				// sum only 0 and TOTAL_TYPE_PENDING_BIT
				if (pending && pending * TOTAL_TYPE_PA_LOW != TOTAL_TYPE_PENDING_BIT)
					continue;
			}
			else if ((type & TOTAL_TYPE_PA_BITS) == TOTAL_TYPE_ALLOCATED_BIT)
			{
				// sum only 0 and TOTAL_TYPE_ALLOCATED_BIT
				if (pending && pending * TOTAL_TYPE_PA_LOW != TOTAL_TYPE_ALLOCATED_BIT)
					continue;
			}
			else
			{
				// sum only TOTAL_TYPE_PENDING_BIT and TOTAL_TYPE_ALLOCATED_BIT
				if (!pending)
					continue;
			}
		}

		for (unsigned watch = 0; watch <= 2U * incwatch; ++watch)
		{
			if (incwatch && (type & TOTAL_TYPE_TW_BITS))
			{
				if ((type & TOTAL_TYPE_TW_BITS) == TOTAL_TYPE_TRACK_BIT)
				{
					// sum only 0 and TOTAL_TYPE_TRACK_BIT
					if (pending && pending * TOTAL_TYPE_TW_LOW != TOTAL_TYPE_TRACK_BIT)
						continue;
				}
				else if ((type & TOTAL_TYPE_TW_BITS) == TOTAL_TYPE_WATCH_BIT)
				{
					// sum only 0 and TOTAL_TYPE_WATCH_BIT
					if (pending && pending * TOTAL_TYPE_TW_LOW != TOTAL_TYPE_WATCH_BIT)
						continue;
				}
				else
				{
					// sum only TOTAL_TYPE_TRACK_BIT and TOTAL_TYPE_WATCH_BIT
					if (!pending)
						continue;
				}
			}

			for (unsigned delaytime = min_delaytime; delaytime <= max_delaytime; ++delaytime)
			{
				Total total;

				total.type = type | (pending * TOTAL_TYPE_PA_LOW) | (watch * TOTAL_TYPE_TW_LOW);
				total.reference = reference;
				total.asset= asset;
				total.delaytime = delaytime;
				total.blockchain = min_blockchain;

				while (true)
				{
					if (g_shutdown) return -1;

					auto rc = dbconn->TotalSelectMatch(false, total);
					if (rc < 0) return rc;

					if (rc || total.blockchain > max_blockchain)
						break;

					if (sum_pc && (total.type & TOTAL_TYPE_ALLOCATED_BIT))
						balance -= total.total;
					else
						balance += total.total;

					if (total.blockchain == INT64_MAX)
						break;

					++total.blockchain;
				}
			}
		}
	}

	return 0;
}

static bigint_t nowait_amount_pending = 0UL;
static bigint_t nowait_amount_required = 0UL;
static FastSpinLock nowait_amount_mutex;

static bigint_t GetNoWaitNetRequiredWithLock()
{
	bigint_t required = 0UL;

	if (nowait_amount_required > nowait_amount_pending)
		required = nowait_amount_required - nowait_amount_pending;

	if (TRACE_TOTALS) BOOST_LOG_TRIVIAL(debug) << "Total::NoWait net required " << required;

	return required;
}

bigint_t Total::GetNoWaitNetRequired()
{
	lock_guard<FastSpinLock> lock(nowait_amount_mutex);

	return GetNoWaitNetRequiredWithLock();
}

int Total::AddNoWaitAmounts(const bigint_t& pending, bool add_pending, const bigint_t& required, bool add_required)
{
	// adds and/or subtracts from the amount pending and required
	// will only add an amount to required if pending would be >= required after the amounts are added or subtracted
	// returns:
	//		true if amount could not be added to required
	//		false otherwise
	// if the caller uses the return value, then the caller should hold a lock on the db
	//	so no billets are added between searching for a billet and checking the amount pending

	lock_guard<FastSpinLock> lock(nowait_amount_mutex);

	int result = 0;

	if (add_pending)
		nowait_amount_pending = nowait_amount_pending + pending;
	else if (nowait_amount_pending > pending)
		nowait_amount_pending = nowait_amount_pending - pending;
	else
		nowait_amount_pending = 0UL;

	if (add_required)
	{
		auto newval = nowait_amount_required + required;
		if (newval <= nowait_amount_pending)
			nowait_amount_required = newval;
		else
			result = 1;
	}
	else if (nowait_amount_required > required)
		nowait_amount_required = nowait_amount_required - required;
	else
		nowait_amount_required = 0UL;

	if (TRACE_TOTALS) BOOST_LOG_TRIVIAL(debug) << "Total::NoWait pending " << plusminus(add_pending) << pending << " -> " << nowait_amount_pending << "; required " << plusminus(add_required) << required << " -> " << nowait_amount_required << " result " << result;
	//cerr << "Total::NoWait pending " << plusminus(add_pending) << pending << " -> " << nowait_amount_pending << "; required " << plusminus(add_required) << required << " -> " << nowait_amount_required << " result " << result << endl;

	return result;
}
