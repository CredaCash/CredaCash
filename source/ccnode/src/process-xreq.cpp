/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * process-xreq.cpp
*/

#include "ccnode.h"
#include "process-xreq.hpp"
#include "exchange.hpp"
#include "exchange_mining.hpp"
#include "blockchain.hpp"
#include "witness.hpp"
#include "dbconn.hpp"
#include "dbparamkeys.h"

#include <CCobjects.hpp>
#include <transaction.hpp>
#include <transaction.h>
#include <xtransaction-xreq.hpp>
#include <xmatch.hpp>

//!#define TEST_RECALC_ALL		1	// test matching all Xreqs in witness, not just the ones with recalc set

//#define TEST_WARN_UNLINKED_PAIR	1

//#define RTEST_CUZZ			2

#ifndef TEST_RECALC_ALL
#define TEST_RECALC_ALL		0	// don't test
#endif

#ifndef TEST_WARN_UNLINKED_PAIR
#define TEST_WARN_UNLINKED_PAIR	0	// don't test
#endif

#ifndef RTEST_CUZZ
#define RTEST_CUZZ				0	// don't test
#endif

#define XREQ_MAX_PERSISTENT_COUNT		1200	// to limit match processing time
#define XREQ_MIN_NON_PERSISTENT_COUNT	20000

//#define XREQ_MAX_PERSISTENT_COUNT		40	// for testing
//#define XREQ_MIN_NON_PERSISTENT_COUNT	100	// for testing

//!#define RTEST_CUZZ_WAIT				(1024-1)

#ifndef RTEST_CUZZ_WAIT
#define RTEST_CUZZ_WAIT					0	// don't test
#endif

#define TRACE_PROCESS_XREQ	(g_params.trace_xreq_processing)

ProcessXreqs g_process_xreqs;

int ProcessXreqs::AddPendingRequest(DbConn *dbconn, TxPay& tx, const int64_t seqnum, const ccoid_t *objid)
{
	Xreq xreq(tx.tag_type, IsTestnet(g_params.blockchain));

	xreq.amount_bits = TX_AMOUNT_BITS;
	xreq.exponent_bits = TX_AMOUNT_EXPONENT_BITS;

	try
	{
		auto rc = xreq.FromWire("ProcessXreqs::AddRequest", 0, tx.append_data.data(), tx.append_data_length);
		if (rc) throw exception();
	}
	catch (const exception& e)
	{
		BOOST_LOG_TRIVIAL(error) << "ProcessXreqs::AddRequest error: Xreq::FromWire failed; " << e.what();

		return -1;
	}

	xreq.seqnum = seqnum;
	memcpy(&xreq.objid, objid, sizeof(ccoid_t));

	if (xreq.type != CC_TYPE_XCX_MINING_TRADE)
		return AddRequest(dbconn, xreq);

	Xreq xreq2(xreq);

	xreq.ConvertTradeToBuy();
	xreq2.ConvertTradeToSell();

	xreq2.seqnum = g_seqnum[XREQSEQ][VALIDSEQ].NextNum();

	xreq.linked_seqnum = xreq2.seqnum;

	auto rc = AddRequest(dbconn, xreq);
	if (rc)
		return rc;

	xreq2.linked_seqnum = xreq.seqnum;

	return AddRequest(dbconn, xreq2);
}

int ProcessXreqs::AddRequest(DbConn *dbconn, Xreq& xreq)
{
	xreq.open_amount = xreq.max_amount;
	xreq.open_rate_required = xreq.MatchRateRequired(xreq.open_amount);

	xreq.recalc_time = XREQ_RECALC_NEXT;

	return dbconn->XreqsInsert(xreq);	// deletes existing Xreq with same ObjId (if any), then inserts this Xreq
}

/*

Delete expired requests from the Xreqs table, since they are no longer needed for matching
If the request had been added to the blockchain, then settle any open amount

Notes on Xreq expiration:
	- an Xreq is unexpired as long as block timestamp < expire_time
	- an Xreq expires immediately when a block appears with timestamp >= expire_time

	An Xreq that is not yet in the blockchain is also considered expired if its earliest possible blocktime + hold_time + XREQ_MIN_POSTHOLD_TIME >= expire_time
	TODO: The above rule (expiration of Xreq's not yet in blockchain due to required hold_time) is not currently checked in ExpireXreqs. This should be implemented tho.

	block N+0 timestamp <  Xreq expire_time: Xreq valid in this block and participates in matching
	block N+1 timestamp >= Xreq expire_time: Xreq valid in this block but would expire before matching, so witnesses should avoid this if possible
	block N+2 timestamp >= Xreq expire_time: Xreq NOT valid in this block

	Allowing an expiring Xreq in the first block with timestamp >= Xreq expire_time makes it easier on the witnesses,
	since whether an Xreq is valid in that block is independent of the timestamp a witness assigns to a block.

	Note that expiration needs to be checked in 5 places:
	- Tx validation, to determine if an Xreq not yet in a block is expired
	- Witness tx/msg selection, to ensure Xreq's that are expired are not placed in a block
	- Block validation, to ensure a block does not contain any expired Xreq's
	- Indelible block processing, to mark and/or prune Xreq's that have become expired
	- In matching, as a safeguard to ensure expired Xreq's aren't matched
*/

int ProcessXreqs::ExpireXreq(DbConn *dbconn, Xreq& xreq, TxPay& txbuf)
{
	auto rc = dbconn->XreqsDelete(xreq);
	if (rc) return rc;

	if (xreq.xreqnum && xreq.open_amount)
	{
		unsigned disposition = XMATCH_REQ_DISPOSITION_EXPIRED_ALL;

		if (xreq.open_amount < xreq.max_amount)
			disposition = XMATCH_REQ_DISPOSITION_EXPIRED_REM;

		auto rc = dbconn->XmatchreqUpdate(xreq.xreqnum, disposition);
		if (rc) return rc;

		if (xreq.open_amount && !Xtx::TypeHasBareMsg(xreq.type))
		{
			if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "ProcessXreqs::ExpireXreq " << xreq.DebugString();

			auto expire_amount = xreq.open_amount;

			if (xreq.IsBuyer())
				expire_amount = xreq.open_amount * bigint_t(xreq.pledge) / bigint_t(100UL);	// pledge amounts always rounded down

			// changes PersistentDB
			auto rc = g_blockchain.CreateTxOutputs(dbconn, xreq.base_asset, expire_amount, xreq.destination, g_params.default_domain, txbuf, true, true, 0, false);
			if (rc) return rc;
		}
	}

	return 0;
}

int ProcessXreqs::ExpireXreqs(DbConn *dbconn, const uint64_t block_time, TxPay& txbuf)
{
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::ExpireXreqs block_time " << block_time;

	while (!g_shutdown)
	{
		Xreq xreq;

		auto rc = dbconn->XreqsSelectExpire(block_time, xreq);
		if (rc > 0)
			break;
		if (rc) return rc;

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::ExpireXreqs expiring " << xreq.DebugString();

		CCASSERT(xreq.expire_time <= block_time);

		rc = ExpireXreq(dbconn, xreq, txbuf);
		if (rc) return rc;
	}

	//if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::ExpireXreqs block_time " << block_time << " done";

	return 0;
}

int ProcessXreqs::PruneXreqs(DbConn *dbconn, const uint64_t new_xreqnum, TxPay& txbuf)
{
	//if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::PruneXreqs";

	// Prune order:
	//	0. buys that have already matched & persistent without pending matches
	//	1. persistent
	//	2. non-persistent

	for (unsigned pass = 0; pass < 2; ++pass)
	{
		uint64_t next_xreqnum = 1;

		while (!g_shutdown)
		{
			auto persistent = dbconn->XreqCountPersistent();
			auto pending = dbconn->XreqCountPending();

			//BOOST_LOG_TRIVIAL(info) << "ProcessXreqs::PruneXreqs persistent " << persistent << " of " << XREQ_MAX_PERSISTENT_COUNT << " pending " << pending << " of " << XREQ_MAX_PERSISTENT_COUNT + XREQ_MIN_NON_PERSISTENT_COUNT - persistent;

			bool bpersistent = (persistent > XREQ_MAX_PERSISTENT_COUNT);

			if (!bpersistent && persistent + pending <= XREQ_MAX_PERSISTENT_COUNT + XREQ_MIN_NON_PERSISTENT_COUNT)
				break;

			Xreq xreq;

			auto rc = dbconn->XreqsSelectXreqnum(bpersistent * next_xreqnum, xreq);
			if (rc > 0 && !pass)
				break;
			if (rc) return rc;

			if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::PruneXreqs pass " << pass << " persistent " << persistent << " pending " << pending << " found " << xreq.DebugString();

			CCASSERT(bpersistent == !!xreq.xreqnum);

			next_xreqnum = xreq.xreqnum + 1;

			if (!pass && bpersistent)
			{
				if (xreq.xreqnum >= new_xreqnum)
					break;

				if (xreq.open_amount == xreq.max_amount && xreq.pending_match_rate.asFloat())
					continue;
			}

			rc = ExpireXreq(dbconn, xreq, txbuf);
			if (rc) return rc;

			if (!xreq.linked_seqnum)
				continue;

			rc = dbconn->XreqsSelectSeqnum(xreq.linked_seqnum, false, xreq);
			if (rc < 0) return rc;
			if (rc)
				continue;

			rc = ExpireXreq(dbconn, xreq, txbuf);
			if (rc) return rc;
		}
	}

	//if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::PruneXreqs done";

	return 0;
}

static void DumpXreqs(DbConn *dbconn, uint64_t passnum, const char *suffix)
{
	uint64_t next_xreqnum = 1;

	while (!g_shutdown)
	{
		Xreq xreq;

		auto rc = dbconn->XreqsSelectXreqnum(next_xreqnum, xreq);
		if (rc)
			break;

		next_xreqnum = xreq.xreqnum + 1;

		if (xreq.open_amount) // && xreq.pending_match_order)
			BOOST_LOG_TRIVIAL(info) << "ProcessXreqs::DumpXreqs " << passnum << suffix << " " << xreq.DebugString();
	}
}

// returns true if "other" is better than current best
static bool CompareRates(const Xreq& self, const UniFloat& self_net_rate, const Xreq& other, const UniFloat& other_net_rate, const bigint_t& amount)
{
	// Buyer wants a lower rate
	// Seller wants a higher rate

	if (!self.best_amount)
	{
		bool rv = true;
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::CompareRates IsBuyer " << self.IsBuyer() << " self.xreqnum " << self.xreqnum << " other.xreqnum " << other.xreqnum << " self.best_amount " << self.best_amount << " returning " << rv;
		return rv;
	}

	if (self_net_rate != self.best_net_rate)
	{
		bool rv = (self.IsBuyer() ^ (self_net_rate > self.best_net_rate));
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::CompareRates IsBuyer " << self.IsBuyer() << " self.xreqnum " << self.xreqnum << " other.xreqnum " << other.xreqnum << " self_net_rate " << self_net_rate << " self.best_net_rate " << self.best_net_rate << " returning " << rv;
		return rv;
	}

	// this tie-breaker promotes mutual matches
	if (other_net_rate != self.best_other_net_rate)
	{
		bool rv = (self.IsBuyer() ^ (other_net_rate < self.best_other_net_rate));
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::CompareRates IsBuyer " << self.IsBuyer() << " self.xreqnum " << self.xreqnum << " other.xreqnum " << other.xreqnum << " other_net_rate " << other_net_rate << " self.best_other_net_rate " << self.best_other_net_rate << " returning " << rv;
		return rv;
	}

	if (amount != self.best_amount)
	{
		bool rv = (amount > self.best_amount);
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::CompareRates self.xreqnum " << self.xreqnum << " other.xreqnum " << other.xreqnum << " other.xreqnum " << other.xreqnum << " amount " << amount << " self.best_amount " << self.best_amount << " returning " << rv;
		return rv;
	}

	// this tie-breaker promotes round-robbin matching as the matching_amount decreases
	if (other.matching_amount != self.best_other_matching_amount)
	{
		bool rv = (other.matching_amount > self.best_other_matching_amount);
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::CompareRates self.xreqnum " << self.xreqnum << " other.xreqnum " << other.xreqnum << " other.xreqnum " << other.xreqnum << " other.matching_amount " << other.matching_amount << " self.best_other_matching_amount " << self.best_other_matching_amount << " returning " << rv;
		return rv;
	}

	// first-in tie-breaker, which is a definitive tie-breaker for requests that have an xreqnum (i.e., have been added to the blockchain)
	if (other.xreqnum != self.best_other_xreqnum)
	{
		bool rv = (!self.best_other_xreqnum || (other.xreqnum && other.xreqnum < self.best_other_xreqnum));
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::CompareRates self.xreqnum " << self.xreqnum << " other.xreqnum " << other.xreqnum << " other.xreqnum " << other.xreqnum << " self.best_other_xreqnum " << self.best_other_xreqnum << " returning " << rv;
		return rv;
	}

	// final tie-breaker on seqnum; only applies when both requests are not in the blockchain
	bool rv = (other.seqnum < self.best_other_seqnum);
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::CompareRates self.xreqnum " << self.xreqnum << " other.xreqnum " << other.xreqnum << " other.seqnum " << other.seqnum << " self.best_other_seqnum " << self.best_other_seqnum << " returning " << rv;
	return rv;
}

static inline uint64_t ComputeElapsed(uint64_t xreq_time, uint64_t block_time)
{
	if (block_time <= xreq_time)
		return 0;

	return block_time - xreq_time;
}

static inline uint64_t ComputeHold(uint64_t elapsed, uint64_t required)
{
	if (required <= elapsed)
		return 0;

	return required - elapsed;
}

static uint64_t ComputeNetHold(Xreq& xreq, Xreq& other, uint64_t block_time)
{
	auto elapsed = ComputeElapsed(xreq.blocktime, block_time);

	auto hold1 = ComputeHold(elapsed, xreq.hold_time);

	auto hold2 = ComputeHold(elapsed, other.hold_time_required);

	return max(hold1, hold2);
}

static uint64_t ComputeMatchHold(Xreq& buyer, Xreq& seller, uint64_t block_time)
{
	auto hold1 = ComputeNetHold(buyer, seller, block_time);
	auto hold2 = ComputeNetHold(seller, buyer, block_time);

	return max(hold1, hold2);
}

// computes the discounted rate (in place), and updates the Xreq's recalc_time
static void ComputeDiscount(Xreq& xreq, UniFloat &rate, uint64_t hold, uint64_t block_time)
{
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ComputeDiscount xreq xreqnum " << xreq.xreqnum << " block_time " << block_time << " hold " << hold << " until " << block_time + hold << " min_wait_time " << xreq.min_wait_time << " rate " << rate << " wait_discount " << xreq.wait_discount;

	if (hold <= xreq.min_wait_time || rate == 0 || xreq.wait_discount == 0)
	{
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ComputeDiscount skip recalc_time hold " << hold << " min_wait_time " << xreq.min_wait_time << " rate " << rate << " wait_discount " << xreq.wait_discount;

		return;
	}

	hold -= xreq.min_wait_time - 1;	// min(hold) is now zero

	auto factor = UniFloat::Add(1, -xreq.wait_discount);

	factor = UniFloat::Power(factor, 1 + hold / XREQ_WAIT_DISCOUNT_INTERVAL);

	auto new_rate = rate;

	if (xreq.IsBuyer())
		new_rate = UniFloat::Divide(rate, factor);
	else
		new_rate = UniFloat::Multiply(rate, factor);

	if (new_rate == rate)
	{
		BOOST_LOG_TRIVIAL(warning) << "ComputeDiscount new_rate = rate " << rate << " hold " << hold << " wait_discount " << xreq.wait_discount;

		return;
	}

	auto recalc_time = block_time + XREQ_WAIT_DISCOUNT_INTERVAL - (hold % XREQ_WAIT_DISCOUNT_INTERVAL);

	CCASSERT((int64_t)recalc_time >= XREQ_RECALC_NEXT);

	if (xreq.recalc_time == XREQ_RECALC_NOT || recalc_time < xreq.recalc_time)
	{
		xreq.recalc_time = recalc_time;

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::ComputeDiscount xreq xreqnum " << xreq.xreqnum << " set recalc_time " << recalc_time;

		xreq.changed = true;
	}
	else
	{
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ComputeDiscount skip recalc_time " << recalc_time << " xreq.recalc_time " << xreq.recalc_time << " hold " << hold;
	}

	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::ComputeDiscount net rate " << rate << " hold " << hold << " factor " << factor << " new rate " << new_rate << " ; " << xreq.DebugString();

	//cerr << "ComputeDiscount IsBuyer " << xreq.IsBuyer() << " net rate " << rate << " wait_discount " << xreq.wait_discount << " hold " << hold << " factor " << factor << " new rate " << new_rate << endl;

	rate = new_rate;

	return;
}

static void SetMatch(DbConn *dbconn, const bigint_t& amount, const UniFloat& rate, bool hold, Xreq& self, const UniFloat& self_net_rate, const Xreq& other, const UniFloat& other_net_rate)
{
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::SetMatch setting xreqnum " << self.xreqnum << " best match xreqnum " << other.xreqnum << " hold " << hold;

	self.best_amount = amount;
	self.best_rate = rate;
	self.best_net_rate = self_net_rate;
	self.best_other_seqnum = other.seqnum;
	self.best_other_xreqnum = other.xreqnum;
	self.best_other_matching_amount = other.matching_amount;
	self.best_other_net_rate = other_net_rate;

	if (hold)
	{
		self.recalc_time = XREQ_RECALC_NEXT; // TODO: change hold to absolute time so matches can happen when hold expires instead of recalc'ing every time

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::SetMatch xreq xreqnum " << self.xreqnum << " set recalc_time " << self.recalc_time;
	}

	self.changed = true;

	if (!IsWitness() && !self.recalc && !other.recalc)
	{
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(error) << "ProcessXreqs::SetMatch Exchange matching recalc error " << self.DebugString() << " ; " << other.DebugString();

		g_blockchain.SetFatalError("Exchange matching recalc error");
	}
}

static bool CheckMatch(DbConn *dbconn, Xreq& buyer, Xreq& seller, uint64_t block_time)
{
	// to ensure integrity of mining, only match CC_TYPE_XCX_SIMPLE_BUY and CC_TYPE_XCX_MINING_BUY with CC_TYPE_XCX_SIMPLE_SELL or CC_TYPE_XCX_MINING_SELL
	if ((buyer.type == CC_TYPE_XCX_SIMPLE_BUY || buyer.type == CC_TYPE_XCX_MINING_BUY) && seller.type != CC_TYPE_XCX_SIMPLE_SELL && seller.type != CC_TYPE_XCX_MINING_SELL)
		return false;

	// the database queries should ensure these asserts are true

	if (!TEST_RECALC_ALL)
		CCASSERT(buyer.recalc || seller.recalc);

	CCASSERT(buyer.IsBuyer());
	CCASSERT(seller.IsSeller());

	CCASSERT(seller.matching_amount);
	CCASSERT(seller.matching_amount <= seller.open_amount);

	CCASSERT(seller.BaseAmountAsUniFloat(seller.matching_amount) != 0);

	CCASSERT(buyer.matching_amount);
	CCASSERT(buyer.matching_amount <= buyer.open_amount);

	CCASSERT(buyer.BaseAmountAsUniFloat(buyer.matching_amount) >= buyer.base_costs);

	CCASSERT(seller.matching_rate_required <= buyer.matching_rate_required);

	auto amount = buyer.matching_amount;
	if (amount > seller.matching_amount)
	{
		amount = seller.matching_amount;

		if (buyer.BaseAmountAsUniFloat(amount) <= buyer.base_costs)
			return false;
	}

	auto buyer_rate_req = buyer.matching_rate_required;
	if (amount < buyer.matching_amount)
	{
		buyer_rate_req = buyer.MatchRateRequired(amount);
		if (buyer_rate_req < seller.matching_rate_required)
			return false;
	}

	auto seller_rate_req = seller.matching_rate_required;
	if (amount < seller.matching_amount)
	{
		seller_rate_req = seller.MatchRateRequired(amount);
		if (seller_rate_req > buyer_rate_req)
			return false;
	}

	CCASSERT(seller_rate_req <= buyer_rate_req);

	auto match_rate = UniFloat::Average(buyer_rate_req, seller_rate_req);

	auto buyer_net_rate = buyer.NetRate(amount, match_rate);
	auto seller_net_rate = seller.NetRate(amount, match_rate);

	CCASSERT(amount);
	CCASSERT(match_rate >= 0);
	CCASSERT(buyer_net_rate >= 0);
	CCASSERT(seller_net_rate >= 0);

	auto hold = ComputeMatchHold(buyer, seller, block_time);

	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::CheckMatch block time " << block_time << " hold " << hold << " buyer blocktime " << buyer.blocktime << " expire_time " << buyer.expire_time << " seller blocktime " << seller.blocktime << " expire_time " << seller.expire_time;
	//cerr << "ProcessXreqs::CheckMatch block time " << block_time << " hold " << hold << " buyer blocktime " << buyer.blocktime << " expire_time " << buyer.expire_time << " seller blocktime " << seller.blocktime << " expire_time " << seller.expire_time << endl;

	if (buyer.expire_time <= block_time + hold)
		return false;

	if (seller.expire_time <= block_time + hold)
		return false;

	if (hold)
	{
		ComputeDiscount(buyer, buyer_net_rate, hold, block_time);
		ComputeDiscount(seller, seller_net_rate, hold, block_time);
	}

	bool changed_best = false;

	if (CompareRates(buyer, buyer_net_rate, seller, seller_net_rate, amount))
	{
		SetMatch(dbconn, amount, match_rate, hold, buyer, buyer_net_rate, seller, seller_net_rate);

		changed_best = true;
	}

	if (CompareRates(seller, seller_net_rate, buyer, buyer_net_rate, amount))
	{
		SetMatch(dbconn, amount, match_rate, hold, seller, seller_net_rate, buyer, buyer_net_rate);

		changed_best = true;
	}

	if (buyer.changed)
	{
		buyer.changed = false;

		auto rc = dbconn->XreqsUpdate(buyer);
		if (rc) throw Db_Exception();
	}

	if (seller.changed)
	{
		seller.changed = false;

		auto rc = dbconn->XreqsUpdate(seller);
		if (rc) throw Db_Exception();
	}

	return changed_best;
}

void ProcessXreqs::UpdateMutualMatch(DbConn *dbconn, Xreq& xreq, const Xreq& other, const bigint_t& match_amount, const UniFloat& match_rate, const uint64_t passnum, const uint64_t block_time, const uint64_t hold, const bool for_witness)
{
	if (!xreq.for_witness)
		CCASSERT(xreq.xreqnum);

	CCASSERT(xreq.best_other_seqnum == other.seqnum);
	CCASSERT(xreq.best_other_xreqnum == other.xreqnum);
	CCASSERT(xreq.best_other_net_rate == other.best_net_rate);

	CCASSERT(xreq.best_amount == other.best_amount);
	CCASSERT(xreq.best_rate == other.best_rate);

	CCASSERT(match_amount);
	CCASSERT(match_amount <= xreq.matching_amount);
	CCASSERT(xreq.matching_amount <= xreq.open_amount);
	CCASSERT(xreq.open_amount <= xreq.max_amount);

	CCASSERT(xreq.best_amount);
	CCASSERT(xreq.best_rate >= 0);
	CCASSERT(xreq.best_net_rate >= 0);

	CCASSERT(xreq.open_rate_required >= 0);
	CCASSERT(xreq.matching_rate_required >= 0);
	CCASSERT(match_rate > 0);

	if (xreq.foreign_address.length())
		xreq.matching_amount = 0UL;									// an active foreign_address can only be associated with one match
	else
		xreq.matching_amount = xreq.matching_amount - match_amount;

	xreq.matching_rate_required = xreq.MatchRateRequired(xreq.matching_amount);

	if (xreq.pending_match_epoch != block_time / XCX_MATCHING_SECS_PER_EPOCH)
	{
		xreq.pending_match_epoch = block_time / XCX_MATCHING_SECS_PER_EPOCH;
		xreq.pending_match_amount = match_amount;
		xreq.pending_match_rate = match_rate;
		xreq.pending_match_hold_time = hold;
	}

	xreq.last_matched = passnum; // sets recalc on next pass so next best match is recomputed with new matching_amount

	auto rc = dbconn->XreqsUpdate(xreq);
	if (rc) throw Db_Exception();
}

bool ProcessXreqs::FindMutualMatches(DbConn *dbconn, const uint64_t passnum, uint64_t &next_match_index, const uint64_t block_time, const bool for_witness)
{
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_TRACE) << "ProcessXreqs::FindMutualMatches passnum " << passnum << " next_match_index " << next_match_index << " block_time " << block_time << " for_witness " << for_witness;

	bool have_matches = false;

	Xreq m_major, major, minor;

	dbconn->MatchingInitScan(for_witness, m_major);

	while (!g_shutdown)
	{
		auto rc = dbconn->MatchingSelectMatch(m_major, major, minor);
		if (rc < 0) throw Db_Exception();
		if (dbconn->MatchingCheckRestartScan(rc, major, m_major))
			continue;
		if (rc) break;

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::FindMutualMatches found major " << major.DebugString() << " ; minor " << minor.DebugString();

		have_matches = true;

		auto hold = ComputeMatchHold(major, minor, block_time);

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::FindMutualMatches matched Xreqs for_witness " << for_witness << " hold " << hold << " amount " << major.best_amount << " rate " << major.best_rate << " major xreqnum " << major.xreqnum << " objid " << buf2hex(&major.objid, CC_OID_TRACE_SIZE) << " matching_amount " << major.matching_amount << " minor xreqnum " << minor.xreqnum << " objid " << buf2hex(&minor.objid, CC_OID_TRACE_SIZE) << " matching_amount " << minor.matching_amount;

		// Buyer wants a lower rate
		// Seller wants a higher rate

		CCASSERTZ(for_witness);	// not yet implemented

		// TODO: make major the seller, since sell req's close after one match, all things equal there will be fewer of them and this would therefore make the scan more efficient

		CCASSERT(major.IsBuyer());
		// The following assertion caused the blockchain to halt at approx 17:40 on 2024-09-26, after block level 1299246.
		// This assertion was triggered by floating point round off that caused matching_rate_required to be > major.net_rate_required
		// for Crosschain Simple Buy Request xreqnum 165800 with net_rate_required = 0.00089026568457484245,
		// open_amount = 0.1 and open_rate_required = matching_rate_required = 0.00089026568457484267.
		// To assist in rate checking, the assert was fixed by changing the Xreq::MatchRateRequired() function
		// to make the return value bounded by net_rate_required.
		CCASSERT(major.matching_rate_required <= major.net_rate_required);
		CCASSERT(major.best_amount);
		CCASSERT(major.best_rate <= major.matching_rate_required);
		//commented out because holding changes the net_rate:
		//CCASSERT(major.best_net_rate <= major.net_rate_required);
		//CCASSERT(major.best_rate <= major.best_net_rate);

		CCASSERT(minor.IsSeller());
		CCASSERT(minor.matching_rate_required >= minor.net_rate_required);
		CCASSERT(minor.matching_amount);
		CCASSERT(minor.best_rate >= minor.matching_rate_required);
		//commented out because holding changes the net_rate:
		//CCASSERT(minor.best_net_rate >= minor.net_rate_required);
		//CCASSERT(minor.best_rate >= minor.best_net_rate);

		CCASSERT(major.best_amount == minor.best_amount);
		CCASSERT(major.best_rate == minor.best_rate);

		CCASSERT(major.best_other_matching_amount == minor.matching_amount);
		CCASSERT(minor.best_other_matching_amount == major.matching_amount);

		CCASSERT(minor.base_asset == major.base_asset);
		CCASSERT(minor.quote_asset == major.quote_asset);
		CCASSERT(minor.foreign_asset == major.foreign_asset);
		CCASSERT(minor.consideration_required <= major.consideration_required);
		CCASSERT(minor.consideration_offered >= major.consideration_offered);
		CCASSERT(minor.pledge <= major.pledge);
		CCASSERT(minor.accept_time_required <= major.accept_time_required);
		CCASSERT(minor.accept_time_offered >= major.accept_time_offered);
		CCASSERT(minor.payment_time <= major.payment_time);
		CCASSERT(minor.confirmations <= major.confirmations);

		if (!hold)
		{
			CCASSERT(major.best_net_rate <= major.net_rate_required);
			CCASSERT(UniFloat::CheckLE(major.best_rate, major.best_net_rate));

			CCASSERT(minor.best_net_rate >= minor.net_rate_required);
			CCASSERT(UniFloat::CheckGE(minor.best_rate, minor.best_net_rate));

			// The pending match values are saved in the database in the sell xreqs, which is possible because crosschain sell xreqs can only have one match.
			// This will have to be changed when non-crosschain xreqs are implemented where the sell xreq can have more than one match.

			CCASSERTZ(minor.pending_match_order);
			CCASSERT(minor.pending_match_epoch != block_time / XCX_MATCHING_SECS_PER_EPOCH);

			minor.pending_match_order = next_match_index;
			++next_match_index;
		}

		auto match_amount = major.best_amount;
		auto match_rate = major.best_rate;

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "ProcessXreqs::FindMutualMatches new match for_witness " << for_witness << " block_time " << block_time << " hold " << hold << " ; xbuy " << major.DebugString() << " ; xsell " << minor.DebugString();
		//if (!for_witness && !hold) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs block_time " << block_time << " found match type " << major.type << " buyer " << major.xreqnum << " seller " << minor.xreqnum << " hold " << hold << " asset " << major.quote_asset << " amount " << match_amount << " buyer_net_rate_required " << major.net_rate_required << " buyer_net_rate " << major.best_net_rate << " rate " << match_rate << " seller_net_rate " << minor.best_net_rate << " seller_net_rate_required " << minor.net_rate_required;

		UpdateMutualMatch(dbconn, major, minor, match_amount, match_rate, passnum, block_time, hold, for_witness);
		UpdateMutualMatch(dbconn, minor, major, match_amount, match_rate, passnum, block_time, hold, for_witness);

		g_witness.UpdateExchangeWorkTime(block_time + hold);

		dbconn->MatchingAdvanceScan(major, m_major);
	}

	return have_matches;
}

bool ProcessXreqs::AddMiningMatches(DbConn *dbconn, uint64_t next_match_index, const uint64_t block_time, const uint64_t max_xreqnum)
{
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "ProcessXreqs::AddMiningMatches matching_epoch " << block_time / XCX_MATCHING_SECS_PER_EPOCH << " block_time " << block_time << " max_xreqnum " << max_xreqnum;

	uint64_t next_xreqnum = 1;
	bool changed_best = false;

	const bool for_witness = false;
	const unsigned passnum = 0;

	while (!g_shutdown && next_xreqnum <= max_xreqnum)
	{
		Xreq major, minor;

		auto rc = dbconn->XreqsSelectXreqnum(next_xreqnum, major, CC_TYPE_XCX_MINING_BUY);
		if (rc < 0) throw Db_Exception();
		if (rc) break;

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::AddMiningMatches found major " << major.DebugString();

		next_xreqnum = major.xreqnum + 1;

		if (major.xreqnum > max_xreqnum)
			break;

		if (major.expire_time <= block_time)
			continue;

		if (!major.matching_amount)
			continue;

		if (!major.linked_seqnum)
		{
			if (TEST_WARN_UNLINKED_PAIR) BOOST_LOG_TRIVIAL(warning) << "ProcessXreqs::AddMiningMatches no link for major " << major.DebugString();

			continue;
		}

		CCASSERT(major.type == CC_TYPE_XCX_MINING_BUY);
		CCASSERT(major.min_amount == major.max_amount);

		rc = dbconn->XreqsSelectSeqnum(major.linked_seqnum, for_witness, minor);
		if (rc < 0) throw Db_Exception();
		if (rc)
		{
			// this can happen if the linked xreq has been pruned
			// clear link so this xreq isn't checked again

			if (TEST_WARN_UNLINKED_PAIR)
				continue;

			major.linked_seqnum = 0;

			dbconn->XreqsUpdate(major);

			continue;
		}

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::AddMiningMatches found minor " << minor.DebugString();

		if (minor.xreqnum > max_xreqnum)
			continue;

		if (minor.expire_time <= block_time)
			continue;

		if (!minor.matching_amount)
			continue;

		if (minor.linked_seqnum != major.seqnum)
		{
			if (TEST_WARN_UNLINKED_PAIR) BOOST_LOG_TRIVIAL(warning) << "ProcessXreqs::AddMiningMatches link mismatch for major " << major.DebugString() << " ; minor " << minor.DebugString();

			continue;
		}

		CCASSERT(minor.seqnum == major.linked_seqnum);

		CCASSERT(minor.type == CC_TYPE_XCX_MINING_SELL);
		CCASSERT(minor.min_amount == minor.max_amount);

		CCASSERT(major.max_amount == minor.max_amount);
		CCASSERT(major.net_rate_required == minor.net_rate_required);

		auto hold = ComputeMatchHold(major, minor, block_time);
		if (hold)
			continue;

		major.recalc = true;
		major.best_amount = 0UL;
		minor.best_amount = 0UL;

		auto have_match = CheckMatch(dbconn, major, minor, block_time);

		CCASSERT(have_match);

		CCASSERTZ(minor.pending_match_order);
		CCASSERT(minor.pending_match_epoch != block_time / XCX_MATCHING_SECS_PER_EPOCH);

		minor.pending_match_order = next_match_index;
		++next_match_index;

		auto match_amount = major.best_amount;
		auto match_rate = major.best_rate;

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "ProcessXreqs::AddMiningMatches new match block_time " << block_time << " ; xbuy " << major.DebugString() << " ; xsell " << minor.DebugString();
		//BOOST_LOG_TRIVIAL(debug) << "AddMiningMatches block_time " << block_time << " found match type " << major.type << " buyer " << major.xreqnum << " seller " << minor.xreqnum << " asset " << major.quote_asset << " amount " << match_amount << " buyer_net_rate_required " << major.net_rate_required << " buyer_net_rate " << major.best_net_rate << " rate " << match_rate << " seller_net_rate " << minor.best_net_rate << " seller_net_rate_required " << minor.net_rate_required;

		UpdateMutualMatch(dbconn, major, minor, match_amount, match_rate, passnum, block_time, hold, for_witness);
		UpdateMutualMatch(dbconn, minor, major, match_amount, match_rate, passnum, block_time, hold, for_witness);

		changed_best = true;
	}

	return changed_best;
}

/*

How Xreq matching works:

A round of matching occurs after every persistent block.  Only persistent Xreq's (that have been added to the blockchain
and assigned as xreqnum) are eligible for matching.

One round of matching consists of multiple matching passes, as described below.

In one pass, all buy Xreq's are computed against all sell Xreq's to determine the best potential match for each buyer
and each seller. If there is a mutual best potential match between a buyer and a seller, this becomes a match. If either
the buyer or seller Xreq is on hold due to their wait time, then the match is placed on hold and the match amount is
deducted only from matching_amount; otherwise, the match amount is deducted from both open_amount and matching_amount
and the match is recorded in the persistent db.

The process is repeated with additional passes until no more matches are found.

How recalc works:

Recalc is an optimization to avoid recomputing an Xreq's best potential match if there are no relevant changes. A
potential match is recomputed when either the buyer's OR the seller's recalc flag is set.  If neither recalc flag is
set, then nothing relevant has changed and the potential match is not recomputed.

An Xreq's recalc flag is set at the start of a matching round when:
	- The Xreq first becomes persistent (is assigned an xreqnum).
	- The Xreq's recalc_time has been reached.

An Xreq's recalc flag is set at the start of a matching pass when:
	- The Xreq had a match during the prior pass.
	- If the Xreq's best potential match matched any other Xreq in the prior pass, or has expired.

How the Xreq's last_matched value works:
	- The static integer passnum is incremented to start each pass.
	- Xreq's that have a match in that pass have their last_matched value set to passnum.
	- To test if an Xreq had a match in the prior pass, check if the Xreq's last_matched value = passnum - 1.
	- Note the above could be done by giving each Xreq a boolean value called matched instead of the integer last_matched value.
		The matched value would be set when the Xreq matches and reset at the start of each pass.
		But the integer last_matched provides debugging info and does not need to be reset at the start of each pass.

How Xreq's recalc_time value works:
	- If the Xreq has any potential matches on hold, then recalc_time is set to the time when the Xreq's discount factor will change.
	- When the Xreq first becomes persistent,  (is assigned an xreqnum), recalc_time is set to XREQ_RECALC_NEXT, which causes recalc to be set at the start of the next round of matching.
	- If the Xreq has any actual matches (mutual best potential matches) on hold, then recalc_time is set to XREQ_RECALC_NEXT, which causes recalc to be set at the start of the next round of matching.
		That is necessary because the match on hold was cleared out of the best potential match column to allow the next best potential match to be found.
		Instead of recomputing from scratch, it might be possibe to store a full list of the actual matches on hold.

*/

void ProcessXreqs::MatchReqs(DbConn *dbconn, const uint64_t block_time, const uint64_t max_xreqnum, const bool for_witness)
{
	CCASSERTZ(for_witness);	// not yet implemented

	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "ProcessXreqs::MatchReqs matching_epoch " << block_time / XCX_MATCHING_SECS_PER_EPOCH << " block_time " << block_time << " max_xreqnum " << max_xreqnum << " for_witness " << for_witness;

	bool first_pass = true;
	bool have_matches = true;
	uint64_t next_match_index = 1;

	unsigned inner_count;
	for (inner_count = 0; !g_shutdown && have_matches && inner_count < 1000000; )
	{
		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::MatchReqs loop start block_time " << block_time << " for_witness " << for_witness;

		static uint64_t passnum = 1;

		auto prior_passnum = passnum++;

		have_matches = false;

		bool changed_best = false;

		//DumpXreqs(dbconn, passnum, "-pre-init"); // for debugging

		auto rc = dbconn->MatchingInit(block_time, first_pass, prior_passnum, max_xreqnum, for_witness);
		if (rc) throw Db_Exception();

		first_pass = false;

		//DumpXreqs(dbconn, passnum, "-post-init"); // for debugging

		Xreq m_base, m_pair, m_major, m_minor, base, pair, major, minor;

		dbconn->SearchInitPairBase(for_witness, max_xreqnum, m_base);

		while (!g_shutdown)
		{
			auto rc = dbconn->XreqsSelectPairBase(m_base, base);
			if (rc < 0) throw Db_Exception();
			if (rc) break;

			if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::MatchReqs found base " << base.DebugString();

			CCASSERT( for_witness ||  base.xreqnum);
			CCASSERT(!for_witness || !base.xreqnum);

			dbconn->SearchInitPairQuote(base, m_pair);

			while (!g_shutdown)
			{
				auto rc = dbconn->XreqsSelectPairQuote(m_pair, pair);
				if (rc < 0) throw Db_Exception();
				if (rc) break;

				if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::MatchReqs found pair " << pair.DebugString();

				CCASSERT( for_witness ||  pair.xreqnum);
				CCASSERT(!for_witness || !pair.xreqnum);

				dbconn->MatchingInitMajor(pair, m_major);

				while (!g_shutdown)
				{
					auto rc = dbconn->MatchingSelectMajor(m_major, major);
					if (rc < 0) throw Db_Exception();
					if (rc) break;

					if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::MatchReqs found major " << major.DebugString();

					dbconn->MatchingInitMinor(major, m_minor);

					if (TEST_RECALC_ALL && IsWitness())
						m_minor.recalc = false;

					while (!g_shutdown)
					{
						auto rc = dbconn->MatchingSelectMinor(m_minor, minor);
						if (rc < 0) throw Db_Exception();
						if (rc) break;

						if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::MatchReqs found minor " << minor.DebugString();

						++inner_count;

						changed_best |= CheckMatch(dbconn, major, minor, block_time);

						dbconn->MatchingAdvanceMinor(minor, m_minor);
					}

					dbconn->MatchingAdvanceMajor(major, m_major);
				}

				dbconn->SearchAdvancePairQuote(pair, m_pair);
			}

			dbconn->SearchAdvancePairBase(base, m_base);
		}

		//DumpXreqs(dbconn, passnum, "-pre-find"); // for debugging

		if (changed_best)
			have_matches = FindMutualMatches(dbconn, passnum, next_match_index, block_time, for_witness);

		//DumpXreqs(dbconn, passnum, "-post-find"); // for debugging

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::MatchReqs loop end changed_best " << changed_best << " have_matches " << have_matches;

		//if (have_matches && RandTest(32)) return g_blockchain.DebugStop("Test abort with have_matches true"); // for testing
	}

	if (!for_witness)
		AddMiningMatches(dbconn, next_match_index, block_time, max_xreqnum);

	dbconn->XreqsClearOldPendingMatches(block_time / XCX_MATCHING_SECS_PER_EPOCH, max_xreqnum);

	g_witness.UpdateExchangeWorkTime(0, true);	// notify of pending work time

	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_TRACE) << "ProcessXreqs::MatchReqs done inner_count " << inner_count;
}

void static UpdateOpenAmount(Xreq& xreq, const bigint_t& match_amount)
{
	CCASSERT(xreq.open_amount >= match_amount);

	xreq.open_amount = xreq.open_amount - match_amount;
	xreq.open_rate_required = xreq.MatchRateRequired(xreq.open_amount);

	xreq.recalc_time = XREQ_RECALC_NEXT;
}

int static SaveXreq(DbConn *dbconn, const Xreq& xreq)
{
	int rc;

	if (xreq.open_amount)
		rc = dbconn->XreqsUpdate(xreq);
	else
		rc = dbconn->XreqsDelete(xreq);

	return rc;
}

int ProcessXreqs::MakeMatchesPersistent(DbConn *dbconn, const uint64_t block_time, TxPay& txbuf)
{
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_TRACE) << "ProcessXreqs::MakeMatchesPersistent block_time " << block_time;

	while (!g_shutdown)
	{
		Xreq major, minor;

		auto rc = dbconn->MatchingSelectNextPendingMatch(major, minor);
		if (rc > 0) break;
		if (rc) return rc;

		if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::MakeMatchesPersistent new match block_time " << block_time << " ; xbuy " << major.DebugString() << " ; xsell " << minor.DebugString();

		CCASSERT(major.IsBuyer());
		CCASSERT(minor.IsSeller());

		CCASSERT(major.seqnum == minor.best_other_seqnum);
		CCASSERT(major.xreqnum == minor.best_other_xreqnum);

		CCASSERT(minor.pending_match_order);
		CCASSERTZ(minor.pending_match_hold_time);

		minor.pending_match_order = 0;

		auto match_amount = minor.pending_match_amount;
		auto match_rate = minor.pending_match_rate;

		CCASSERT(match_amount);
		CCASSERT(match_rate > 0);

		UpdateOpenAmount(major, match_amount);
		UpdateOpenAmount(minor, match_amount);

		major.best_other_seqnum = minor.seqnum;

		major.best_amount = minor.best_amount = match_amount;
		major.best_rate = minor.best_rate = match_rate;

		Xmatch match(block_time, major, minor);

		match.xmatchnum = g_exchange.GetNextXmatchnum(true);

		//if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::MakeMatchesPersistent new match block_time " << block_time << " ; " << match.DebugString();
		BOOST_LOG_TRIVIAL(info) << "ProcessXreqs new match xmatchnum " << match.xmatchnum << " buy xreqnum " << match.xbuy.xreqnum << " objid " << buf2hex(&match.xbuy.objid, CC_OID_TRACE_SIZE) << " sell xreqnum " << match.xsell.xreqnum << " objid " << buf2hex(&match.xsell.objid, CC_OID_TRACE_SIZE);
		//cerr << "ProcessXreqs new match xmatchnum " << (match.xmatchnum < 10 ? " " : "") << match.xmatchnum << " buy xreqnum " << match.xbuy.xreqnum << " sell xreqnum " << match.xsell.xreqnum << endl;

		if (match.xsell.disposition == XMATCH_REQ_DISPOSITION_MATCHED_PART)
		{
			// close this request, since an active foreign_address can only be associated with one match

			Xmatchreq& xreq = match.xsell;

			if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_DEBUG) << "ProcessXreqs::MakeMatchesPersistent closing " << xreq.DebugString();

			// changes PersistentDB -- note all calls to CreateTxOutputs must be in the same order on all nodes
			auto rc = g_blockchain.CreateTxOutputs(dbconn, xreq.base_asset, xreq.open_amount, xreq.destination, g_params.default_domain, txbuf, true, true, 0, false);
			if (rc) return rc;

			CCASSERT(minor.IsSeller());

			minor.open_amount = xreq.open_amount = 0UL;
		}

		// !!!!! TODO: close xbuy if open_amount < min_amount

		g_exchange_mining.SetMiningAmount(match);

		rc = dbconn->XmatchInsert(match); // changes PersistentDB
		if (rc) return rc;

		rc = SaveXreq(dbconn, major);
		if (rc) return rc;

		rc = SaveXreq(dbconn, minor);
		if (rc) return rc;
	}

	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_TRACE) << "ProcessXreqs::MakeMatchesPersistent done";

	return 0;
}

void ProcessXreqs::WaitForCondition(int condition)
{
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::WaitForCondition " << condition;

	unique_lock<mutex> lock(m_matching_mutex);

	while (m_matching_state != condition && m_matching_state >= 0 && !g_shutdown)
	{
		if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

		m_matching_condition_variable.wait(lock);
	}

	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::WaitForCondition done";
}

void ProcessXreqs::SetCondition(int condition)
{
	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::SetCondition " << condition;

	{
		lock_guard<mutex> lock(m_matching_mutex);

		m_matching_state = condition;
	}

	m_matching_condition_variable.notify_one();
}

void ProcessXreqs::MatchingThread()
{
	BOOST_LOG_TRIVIAL(info) << "ProcessXreqs::MatchingThread start";

	auto dbconn = new DbConn;

	while (true)
	{
		WaitForCondition(MATCHING_STATE_START);

		if (RandTest(RTEST_CUZZ)) ccsleep(rand() % XCX_MATCHING_SECS_PER_EPOCH);

		if (g_shutdown)
			break;

		try
		{
			MatchReqs(dbconn, m_matching_block_time, m_matching_max_xreqnum);	// nothing in here is allowed to change the PersistentDb
		}
		catch (const Db_Exception&)
		{
			g_blockchain.SetFatalError("ProcessXreqs::MatchingThread error in exchange matching");

			break;
		}

		SetCondition(MATCHING_STATE_IDLE);
	}

	delete dbconn;

	BOOST_LOG_TRIVIAL(info) << "ProcessXreqs::MatchingThread done";
}

int ProcessXreqs::Init(DbConn *dbconn, const uint64_t block_level, const uint64_t block_time)
{
	m_last_matching_epoch = block_time / XCX_MATCHING_SECS_PER_EPOCH;
	m_matching_block_time = m_last_matching_epoch * XCX_MATCHING_SECS_PER_EPOCH;

	BOOST_LOG_TRIVIAL(trace) << "ProcessXreqs::Init block_level " << block_level << " block_time " << block_time << " epoch " << m_last_matching_epoch;

	auto rc = dbconn->ParameterSelect(DB_KEY_XMATCHING, 0, &m_matching_max_xreqnum, sizeof(m_matching_max_xreqnum));
	if (rc < 0)
		return rc;

	BOOST_LOG_TRIVIAL(info) << "ProcessXreqs::Init block_level " << block_level << " block_time " << block_time << " epoch " << m_last_matching_epoch << " m_matching_block_time " << m_matching_block_time << " m_matching_max_xreqnum " << m_matching_max_xreqnum;

	//DumpXreqs(dbconn, passnum, "-post-init"); // for debugging
	(void)DumpXreqs;	// avoid unused function warning

	m_matching_state = MATCHING_STATE_START;

	m_thread = new thread(&ProcessXreqs::MatchingThread, this);
	CCASSERT(m_thread);

	return 0;
}

void ProcessXreqs::DeInit()
{
	BOOST_LOG_TRIVIAL(info) << "ProcessXreqs::DeInit";

	SetCondition(-1); // locks m_matching_mutex

	m_matching_condition_variable.notify_all();

	if (m_thread)
	{
		m_thread->join();
		delete m_thread;
		m_thread = NULL;
	}

	BOOST_LOG_TRIVIAL(info) << "ProcessXreqs::DeInit done";
}

void ProcessXreqs::CheckUpdateWitnessWorkTime(const uint64_t block_time)
{
	// if any xreq's have become persistent in this matching epoch, then we ask for a new block to trigger matching at the start of the next epoch
	// that's not quite ideal-- it would be better if the witness decided for itself to make a block because there would be actual matches, not just potential matches
	// TODO: move this to the witness to produce a block only if the block would result in actual exchange matches (including matches on hold, so the wallet can get accurate info)

	auto next_xreqnum = g_exchange.GetNextXreqnum();
	CCASSERT(next_xreqnum);

	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(debug) << "ProcessXreqs::CheckUpdateWitnessWorkTime block_time " << block_time << " next_xreqnum " << next_xreqnum << " m_matching_max_xreqnum " << m_matching_max_xreqnum;

	if (m_matching_max_xreqnum != next_xreqnum - 1)
		g_witness.UpdateExchangeWorkTime(block_time, true);
}

// This function is called from the main blockchain thread with a write transaction opened on the PersistentDb
// Every XCX_MATCHING_SECS_PER_EPOCH seconds (synchoronized to the persistent blocks), it fetches new matches from
// the baackground thread (waiting for the background thread to finish if necessary), adds them to the PersistentDb,
// and then starts the background thread on a new round of matching

int ProcessXreqs::SynchronizeMatching(DbConn *dbconn, const uint64_t block_level, const uint64_t block_time, const uint64_t new_xreqnum, TxPay& txbuf)
{
	if (m_matching_state < 0)
		return 0;

	// Blocks have a timestamp with an accuracy of 1 second.
	// This timestamp is rounded down to the nearest multiple of XCX_MATCHING_SECS_PER_EPOCH
	// for the purpose of matching exchange requests and expiring requests and matches.

	auto epoch = block_time / XCX_MATCHING_SECS_PER_EPOCH;

	if (epoch == m_last_matching_epoch)
	{
		CheckUpdateWitnessWorkTime(block_time);

		return 0;
	}

	m_last_matching_epoch = epoch;
	g_witness.ResetExchangeWorkTime(true);	// freeze until matching starts

	if (TRACE_PROCESS_XREQ) BOOST_LOG_TRIVIAL(LOG_SYNC_TRACE) << "ProcessXreqs::SynchronizeMatching block_level " << block_level << " block_time " << block_time << " new epoch " << epoch;

	// finish prior matching

	WaitForCondition(MATCHING_STATE_IDLE);

	// make pending matches persistent [may create TxOutputs]

	auto rc = MakeMatchesPersistent(dbconn, m_matching_block_time, txbuf);
	if (rc) return rc;

	g_exchange_mining.UpdateMiningTime(m_matching_block_time);

	// last round of matching is now done; prepare for next round

	m_last_matched_block_time = m_matching_block_time;

	m_matching_block_time = epoch * XCX_MATCHING_SECS_PER_EPOCH;

	// close xreq's that have expired (blocktime >= expire_time) [may create TxOutputs]

	rc = ExpireXreqs(dbconn, m_matching_block_time, txbuf);
	if (rc) return rc;

	// limit # of xreq's (closing the oldest ones before expiration if necessary) so matching doesn't bog down [may create TxOutputs]

	rc = PruneXreqs(dbconn, new_xreqnum, txbuf);
	if (rc) return rc;

	// start a new round of matching

	auto next_xreqnum = g_exchange.GetNextXreqnum();
	CCASSERT(next_xreqnum);
	m_matching_max_xreqnum = next_xreqnum - 1;

	rc = dbconn->ParameterInsert(DB_KEY_XMATCHING, 0, &m_matching_max_xreqnum, sizeof(m_matching_max_xreqnum));
	if (rc) return rc;

	g_witness.ResetExchangeWorkTime();

	SetCondition(MATCHING_STATE_START);

	return 0;
}
