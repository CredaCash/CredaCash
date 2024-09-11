/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * exchange.cpp
*/

#include "ccnode.h"
#include "exchange.hpp"
#include "blockchain.hpp"
#include "block.hpp"
#include "witness.hpp"
#include "dbparamkeys.h"

#include <xtransaction-xpay.hpp>
#include <xmatch.hpp>

#define TRACE_EXCHANGE	(g_params.trace_exchange)

Exchange g_exchange;

static uint64_t GetXreqBlockTime(DbConn *dbconn, uint64_t xreqnum)
{
	// example: assume level 51 has next_xreqnum 200
	//	 then level 51 possibly contains xreqnum's < 200
	//	  and level 52 possibly contains xreqnum's >= 200

	// find the max[timestamp(level)] such that xreqnum < next_xreqnum(level)
	// --> requires that next_xreqnum(level-1) <= xreqnum

	uint64_t lower_level = 0;
	uint64_t lower_next_xreqnum = 0;			// <= xreqnum

	uint64_t upper_level = INT64_MAX;
	uint64_t upper_next_xreqnum = UINT64_MAX;	// > xreqnum

	uint64_t blocktime = 0;

	while (true)
	{
		if (g_shutdown)
			return 0;

		uint64_t search_level = (lower_level + upper_level) / 2;	// would not work if max(upper_level) > INT64_MAX

		if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(trace) << "Exchange::GetXreqBlockTime " << lower_level << "=" << lower_next_xreqnum << " / " << search_level << ":" << xreqnum << " / " << upper_level << "=" << upper_next_xreqnum;

		CCASSERT(lower_level <= search_level);
		CCASSERT(search_level < upper_level);

		CCASSERT(lower_next_xreqnum <= xreqnum);
		CCASSERT(xreqnum < upper_next_xreqnum);

		if (search_level == lower_level)
			break;

		uint64_t level, timestamp, next_xreqnum, next_xmatchnum;

		auto rc = dbconn->XcxNumsSelect(search_level, level, timestamp, next_xreqnum, next_xmatchnum);
		if (rc < 0)
			g_blockchain.SetFatalError("Exchange::Init error retrieving exchange request blocktime");

		if (rc)
			next_xreqnum = 0;

		if (next_xreqnum <= xreqnum)
		{
			lower_level = search_level;
			lower_next_xreqnum = next_xreqnum;
		}
		else
		{
			upper_level = level;
			upper_next_xreqnum = next_xreqnum;
			blocktime = timestamp;
		}

		if (upper_level <= lower_level)
			break;
	}

	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "Exchange::GetXreqBlockTime xreqnum " << xreqnum << " returning blocktime " << blocktime;

	CCASSERT(blocktime);

	return blocktime;
}

void Exchange::Init(DbConn *dbconn)
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(trace) << "Exchange::Init";

	uint64_t level, timestamp, next_xreqnum, next_xmatchnum;

	auto rc = dbconn->XcxNumsSelect(INT64_MAX, level, timestamp, next_xreqnum, next_xmatchnum);
	if (rc < 0)
		return (void)g_blockchain.SetFatalError("Exchange::Init error retrieving next exchange request numbers");

	m_next_xreqnum.store(next_xreqnum ? next_xreqnum : 1);
	m_next_xmatchnum.store(next_xmatchnum ? next_xmatchnum : 1);

	BOOST_LOG_TRIVIAL(info) << "Exchange::Init next_xreqnum " << m_next_xreqnum << " next_xmatchnum " << m_next_xmatchnum;
}

void Exchange::DeInit()
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(trace) << "Exchange::DeInit";
}

void Exchange::Restore(DbConn *dbconn)
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(trace) << "Exchange::Restore";

	// recreate Xreqs memory table which is used for matching

	uint64_t next_xreqnum = 0;
	uint64_t expected_mining_sell_xreqnum = 0;

	while (!g_blockchain.HasFatalError() && !g_shutdown)
	{
		Xmatchreq req;

		auto rc = dbconn->XmatchreqSelectMatching(next_xreqnum, req, true);
		if (rc < 0)
			return (void)g_blockchain.SetFatalError("Exchange::Restore error retrieving Xmatchreq's");
		if (rc)
			break;

		CCASSERT(req.xreqnum < m_next_xreqnum.load());

		next_xreqnum = req.xreqnum + 1;

		CCASSERT(req.flags.have_matching);

		if (req.IsClosed() || !req.open_amount)
			continue;

		CCASSERT(req.flags.have_matching);

		Xreq xreq(req.type);

		xreq.amount_bits = TX_AMOUNT_BITS;
		xreq.exponent_bits = TX_AMOUNT_EXPONENT_BITS;
		//xreq.amount_carry_in
		//xreq.amount_carry_out

		xreq.objid = req.objid;
		xreq.expire_time = req.expire_time;
		xreq.base_asset = req.base_asset;
		xreq.quote_asset = req.quote_asset;
		xreq.foreign_asset = req.foreign_asset;
		xreq.min_amount = req.min_amount;
		xreq.max_amount = req.max_amount;
		xreq.net_rate_required = req.net_rate_required;
		xreq.wait_discount = req.wait_discount;
		xreq.base_costs = req.base_costs;
		xreq.quote_costs = req.quote_costs;

		xreq.destination = req.destination;
		xreq.foreign_address = req.foreign_address;

		xreq.flags.add_immediately_to_blockchain = req.flags.add_immediately_to_blockchain;
		xreq.flags.auto_accept_matches = req.flags.auto_accept_matches;
		xreq.flags.no_minimum_after_first_match = req.flags.no_minimum_after_first_match;
		xreq.flags.must_liquidate_crossing_minimum = req.flags.must_liquidate_crossing_minimum;
		xreq.flags.must_liquidate_below_minimum = req.flags.must_liquidate_below_minimum;
		xreq.flags.has_signing_key = req.flags.has_signing_key;

		xreq.consideration_required = req.consideration_required;
		xreq.consideration_offered = req.consideration_offered;
		xreq.pledge = req.pledge;
		xreq.hold_time = req.hold_time;
		xreq.hold_time_required = req.hold_time_required;
		xreq.min_wait_time = req.min_wait_time;
		xreq.accept_time_required = req.accept_time_required;
		xreq.accept_time_offered = req.accept_time_offered;
		xreq.payment_time = req.payment_time;
		xreq.confirmations = req.confirmations;

		xreq.blocktime = GetXreqBlockTime(dbconn, req.xreqnum);

		xreq.xreqnum = req.xreqnum;
		xreq.open_amount = req.open_amount;

		memcpy(&xreq.signing_public_key, &req.signing_public_key, sizeof(xreq.signing_public_key));

		xreq.open_rate_required = xreq.MatchRateRequired(xreq.open_amount);

		xreq.recalc_time = XREQ_RECALC_NEXT;

		xreq.seqnum = g_seqnum[XREQSEQ][VALIDSEQ].NextNum();

		// a linked pair of mining buy and sell reqs always have sequential xreqnums.
		// one of the pair might be missing tho if it has been pruned, so only
		// link both ways if the buy and sell have sequential xreqnums.

		if (xreq.type == CC_TYPE_XCX_MINING_SELL && xreq.xreqnum == expected_mining_sell_xreqnum)
			xreq.linked_seqnum = xreq.seqnum - 1;

		if (xreq.type == CC_TYPE_XCX_MINING_BUY)
		{
			xreq.linked_seqnum = xreq.seqnum + 1;
			expected_mining_sell_xreqnum = xreq.xreqnum + 1;
		}

		//if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "Exchange::Restore read " << req.DebugString();
		if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "Exchange::Restore restoring " << xreq.DebugString();

		rc = dbconn->XreqsInsert(xreq);
		if (rc)
			return (void)g_blockchain.SetFatalError("Exchange::Restore error saving Xreq");
	}

	// if xreq's are pruned after matching, that would need to be included here...

	//if (IsWitness() && m_next_xreqnum.load() > 1)
	//	g_blockchain.DebugStop("Stopping after loading Xreqs");
}

uint64_t Exchange::GetNextXreqnum(bool increment)
{
	if (!increment)
		return m_next_xreqnum.load();

	auto num = m_next_xreqnum.fetch_add(1);

	m_saved.clear();

	//BOOST_LOG_TRIVIAL(info) << "Exchange::GetNextXreqnum returning " << num;

	return num;
}

uint64_t Exchange::GetNextXmatchnum(bool increment)
{
	if (!increment)
		return m_next_xmatchnum.load();

	auto num = m_next_xmatchnum.fetch_add(1);

	m_saved.clear();

	//BOOST_LOG_TRIVIAL(info) << "Exchange::GetNextXmatchnum returning " << num;

	return num;
}

int Exchange::SaveNextNums(DbConn *dbconn, uint64_t level, uint64_t timestamp)
{
	if (m_saved.test_and_set())
		return 0;

	auto next_xreqnum = GetNextXreqnum();
	auto next_xmatchnum = GetNextXmatchnum();

	CCASSERT(next_xreqnum);	// should never get here without at least one xreq

	auto rc = dbconn->XcxNumsInsert(level, timestamp, next_xreqnum, next_xmatchnum);

	return rc;
}

