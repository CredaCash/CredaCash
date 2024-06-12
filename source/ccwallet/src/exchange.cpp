/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * exchange.cpp
*/

#include "ccwallet.h"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "exchange.hpp"
#include "polling.hpp"
#include "txquery.hpp"
#include "walletdb.hpp"
#include "rpc_errors.hpp"

#include <xmatch.hpp>
#include <xtransaction.hpp>
#include <xtransaction-xreq.hpp>
#include <BlockChainStatus.hpp>

//#define TEST_RANDOM_POLLTIMES	600

//!#define RTEST_CUZZ			32

#ifndef TEST_RANDOM_POLLTIMES
#define TEST_RANDOM_POLLTIMES	0	// don't test
#endif

#ifndef RTEST_CUZZ
#define RTEST_CUZZ				0	// don't test
#endif

#define TRACE_EXCHANGE	(g_params.trace_exchange)

int ExchangeRequest::BeginAndReadXmatchreq(DbConn *dbconn, Xmatchreq &xreq, Transaction *tx, uint64_t id, bool or_greater)
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(trace) << "ExchangeRequest::BeginAndReadXmatchreq id " << id << " or_greater " << or_greater;

	auto rc = dbconn->BeginRead();
	if (rc)
	{
		xreq.Clear();
		if (tx) tx->Clear();

		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	return ReadXmatchreq(dbconn, xreq, tx, id, or_greater);
}

int ExchangeRequest::ReadXmatchreq(DbConn *dbconn, Xmatchreq &xreq, Transaction *tx, uint64_t id, bool or_greater)
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(trace) << "ExchangeRequest::ReadXmatchreq id " << id << " or_greater " << or_greater;

	xreq.Clear();
	if (tx) tx->Clear();

	auto rc = dbconn->ExchangeRequestSelectId(id, xreq, tx, or_greater);
	if (rc > 0 && or_greater)
	{
		BOOST_LOG_TRIVIAL(trace) << "ExchangeRequest::ReadXmatchreq id " << id << " returned " << rc;

		return 1;
	}
	else if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::ReadXmatchreq error reading id " << id;

		return -1;
	}

	return 0;
}

int ExchangeRequest::UpdatePollTime(DbConn *dbconn, uint64_t tx_id, uint64_t poll_time)
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeRequest::UpdatePollTime tx_id " << tx_id << " poll_time " << poll_time;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	if (poll_time == (uint64_t)(-1))	// -1 = no polling
		poll_time = 0;
	else if (poll_time != 1)
	{
		if (TEST_RANDOM_POLLTIMES && (rand() & 1))
			poll_time = (rand() % (TEST_RANDOM_POLLTIMES + 1)) + 1;

		poll_time = unixtime() + (poll_time ? poll_time : g_params.exchange_poll_time); // 0 = normal polling; else specified polling
		if (!poll_time)
			poll_time = 1;
	}

	return dbconn->ExchangeRequestUpdatePolling(tx_id, poll_time);
}

int ExchangeRequest::PollAddress(DbConn *dbconn, TxQuery& txquery, uint64_t address_id)
{
	CCASSERT(address_id);

	Secret address;

	auto rc = dbconn->SecretSelectId(address_id, address);
	if (rc) return rc;

	return address.PollAddress(dbconn, txquery);
}

int ExchangeRequest::PollXmatchreq(DbConn *dbconn, TxQuery& txquery, Xmatchreq &xreq, const Transaction &tx, BlockChainStatus& blockchain_status)
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeRequest::PollXmatchreq " << xreq.DebugString() << " ; tx " << tx.DebugString();

	blockchain_status.Clear();

	bool save_xreq = false;
	bool poll_address = false;

	QueryXmatchreqResults results;
	results.more_results = true;
	unsigned more_count = 0;

	while (results.more_results && !g_shutdown)
	{
		if (++more_count > 5000)
		{
			BOOST_LOG_TRIVIAL(warning) << "ExchangeRequest::PollXmatchreq excess more_results; transaction server appears broken";

			return -1;
		}

		if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeRequest::PollXmatchreq querying objid " << buf2hex(&xreq.objid, CC_OID_TRACE_SIZE) << " xreqnum " << xreq.xreqnum << " query_matchnum " << xreq.query_matchnum << " more_count " << more_count;

		auto rc = txquery.QueryXmatchreq(tx.blockchain, xreq.objid, xreq.xreqnum, xreq.query_matchnum, results);
		if (rc) return rc;

		blockchain_status.Copy(results.blockchain_status);

		if (results.xreqnum && results.xreqnum != xreq.xreqnum)
		{
			// TODO: wallet tx for buy xreq needs to be set to TX_STATUS_CLEARED

			xreq.xreqnum = results.xreqnum;
			save_xreq = true;
		}

		if (results.disposition)
		{
			if (xreq.disposition != results.disposition)
			{
				xreq.disposition = results.disposition;
				save_xreq = true;

				if (xreq.IsClosed() && xreq.ExpectingFunds())
					poll_address = true;
			}

			if (xreq.open_amount != results.open_amount)
			{
				xreq.open_amount = results.open_amount;
				save_xreq = true;
			}
		}
		else if (xreq.IsOpen() && results.blockchain_status.last_matching_start_block_time >= xreq.expire_time) // this test must not expire an xreq sooner than the ccnode test to call ProcessXreqs::ExpireXreqs
		{
			if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeRequest::PollXmatchreq last_matching_start_block_time " << results.blockchain_status.last_matching_start_block_time << " closing expired " << xreq.DebugString();

			if (xreq.HasMatches())
				xreq.disposition = XMATCH_REQ_DISPOSITION_EXPIRED_REM;
			else
				xreq.disposition = XMATCH_REQ_DISPOSITION_EXPIRED_ALL;

			save_xreq = true;

			if (xreq.ExpectingFunds())
				poll_address = true;
		}

		if (results.more_results && !results.nresults)
			BOOST_LOG_TRIVIAL(warning) << "ExchangeRequest::PollXmatchreq bad QueryXmatchreq results: nresults zero with more_results set";

		for (unsigned i = 0; i < results.nresults; ++i)
		{
			Xmatch& xmatch = results.xmatches[i];

			if (xmatch.xmatchnum < xreq.query_matchnum)
				BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::PollXmatchreq results not sorted; xmatch.xmatchnum " << xmatch.xmatchnum << " < xreq.query_matchnum " << xreq.query_matchnum;

			xreq.query_matchnum = xmatch.xmatchnum + 1;
			save_xreq = true;

			if ((results.xreqnum == xmatch.xbuy.xreqnum) ^ (results.xreqnum != xmatch.xsell.xreqnum))
			{
				BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::PollXmatchreq unexpected xreqnum; " << xmatch.DebugString();

				continue;
			}

			if (Xtx::TypeIsBuyer(xmatch.xbuy.type) ^ !Xtx::TypeIsSeller(xmatch.xsell.type))
			{
				BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::PollXmatchreq xreq type mismatch; " << xmatch.DebugString();
				//cerr << "ExchangeRequest::PollXmatchreq xreq type mismatch" << endl;

				continue;
			}

			Xmatchreq& matching = (results.xreqnum != xmatch.xbuy.xreqnum ? xmatch.xbuy : xmatch.xsell);

			if (!matching.xreqnum)
			{
				BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::PollXmatchreq matching xreqnum is zero; " << xmatch.DebugString();

				continue;
			}

			// TODO? adjust match deadline before saving

			auto rc = dbconn->BeginWrite();
			if (rc)
			{
				dbconn->DoDbFinishTx(-1);

				return -1;
			}

			Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

			// check to add matching request to wallet

			Transaction matching_tx;

			while (true) // break to exit
			{
				Xmatchreq matching_xreq;

				auto rc = dbconn->ExchangeRequestSelectXreqnum(matching.xreqnum, matching_xreq, &matching_tx);
				if (rc < 0) return rc;
				if (!rc) break;	// already in wallet

				// if xmatch.have_xreqs is true, then check matching.objid

				if (!xmatch.have_xreqs)
					break;

				rc = dbconn->TransactionSelectObjIdDescendingId(matching.objid, INT64_MAX, matching_tx);
				if (rc < 0) return rc;
				if (!rc) break;	// already in wallet

				// add matching request to wallet

				rc = dbconn->ExchangeRequestInsert(matching, true);
				(void)rc;

				break;
			}

			// add/update match

			ExchangeMatch::UpdatePollTime(xmatch, results.blockchain_status.last_matching_completed_block_time);

			bool isbuyer = (Xtx::TypeIsBuyer(xreq.type) || matching_tx.id); // true if the buy req came from this wallet

			if (xmatch.IsClosed())
				xmatch.wallet_polltime = 1;			// poll match addresses
			else if (!isbuyer)
				xmatch.wallet_reminder_time = -1;	// this wallet is not the buyer, so don't set reminder

			if (isbuyer)
			{
				auto rc = ExchangeMatch::UpdateTotalMined(dbconn, xmatch);	// this wallet is the buyer, so update total mined
				if (rc)
				{
					if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(trace) << "ExchangeRequest::PollXmatchreq UpdateTotalMined returned " << rc;

					return rc;
				}
			}

			rc = dbconn->ExchangeMatchInsert(xmatch);
			if (rc) return rc;

			// commit db writes

			if (RandTest(RTEST_CUZZ)) sleep(1);

			if (g_shutdown)
				break;

			rc = dbconn->Commit();
			if (rc)
			{
				BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::PollXmatchreq error committing db transaction";

				return -1;
			}

			dbconn->DoDbFinishTx();

			finally.Clear();

			if (0) // for testing
			{
				Xmatch match2;
				dbconn->ExchangeMatchSelectNum(xmatch.xmatchnum, match2);
			}
		}
	}

	BOOST_LOG_TRIVIAL(trace) << "ExchangeRequest::PollXmatchreq xreq IsOpen " << xreq.IsOpen() << " poll_address " << poll_address << " save_xreq " << save_xreq << " ; " << xreq.DebugString();

	if (poll_address && !g_shutdown)
	{
		// TODO: test polling error

		if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeRequest::PollXmatchreq polling address of " << xreq.DebugString();

		auto rc = PollAddress(dbconn, txquery, xreq.address_id);
		if (rc < 0) return rc;
	}

	if (!xreq.IsOpen() && !g_shutdown)
	{
		if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeRequest::PollXmatchreq ending polling of " << xreq.DebugString();

		auto rc = ExchangeRequest::UpdatePollTime(dbconn, xreq.tx_id, -1);
		(void)rc;

		if (!xreq.xreqnum)
		{
			if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeRequest::PollXmatchreq abandoning " << tx.DebugString();

			try
			{
				Transaction::AbandonTx(dbconn, tx.id, tx.blockchain);
			}
			catch (const RPC_Exception& e)
			{
				BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::PollXmatchreq error abandoning tx code " << e.code << " " << e.what() << "; " << tx.DebugString();
			}
		}
	}

	if (save_xreq && !g_shutdown)
	{
		auto rc = dbconn->ExchangeRequestUpdateStatus(xreq, true);
		if (rc) return rc;
	}

	//cerr << "PollXmatchreq ok" << endl;

	return 0;
}

int ExchangeMatch::UpdateTotalMined(DbConn *dbconn, const Xmatch& xmatch)
{
	// If this is the first time the wallet has seen this match with status XMATCH_STATUS_PAID, then update the wallet's total amount mined
	// To prevent double counting, must be called inside a transaction

	//BOOST_LOG_TRIVIAL(info) << "ExchangeMatch::UpdateTotalMined " << xmatch.DebugString(false);

	if (xmatch.status != XMATCH_STATUS_PAID)	// not mined
		return 0;

	if (xmatch.mining_amount == 0)				// not mined
		return 0;

	// sanity checks on match with mining_amount > 0
	// TODO: sanity check mining_amount

	if (xmatch.type != CC_TYPE_XCX_SIMPLE_BUY)
	{
		BOOST_LOG_TRIVIAL(warning) << "ExchangeRequest::UpdateTotalMined match type " << xmatch.type << " != " << CC_TYPE_XCX_SIMPLE_BUY;

		return 0;
	}

	// check if mining_amount has already been counted, i.e., if match is saved in wallet with status XMATCH_STATUS_PAID

	while (true) // break to exit
	{
		Xmatch saved_match;

		auto rc = dbconn->ExchangeMatchSelectNum(xmatch.xmatchnum, saved_match, true);

		//BOOST_LOG_TRIVIAL(info) << "ExchangeMatch::UpdateTotalMined ExchangeMatchSelectNum xmatchnum " << xmatch.xmatchnum << " returned " << rc << " ; " << saved_match.DebugString(false);

		if (rc < 0) return rc;
		if (rc) break;	// match is not yet saved in the wallet

		if (saved_match.status == XMATCH_STATUS_PAID)
			return 0;	// mining_amount has already been counted

		// sanity checks

		if (saved_match.IsClosed())
		{
			BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::UpdateTotalMined xmatchnum " << xmatch.xmatchnum << " saved_match status " << saved_match.status << " is closed";

			return 0;
		}

		if (!saved_match.xbuy.tx_id) // UpdateTotalMined should only be called for matches where the buy req came from this wallet
		{
			BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::UpdateTotalMined xmatchnum " << xmatch.xmatchnum << " saved_match.xbuy.tx_id " << saved_match.xbuy.tx_id << " = 0";

			return 0;
		}

		if (saved_match.xbuy.type != CC_TYPE_XCX_SIMPLE_BUY)
		{
			BOOST_LOG_TRIVIAL(warning) << "ExchangeRequest::UpdateTotalMined xmatchnum " << xmatch.xmatchnum << " buy type " << saved_match.xbuy.type << " != " << CC_TYPE_XCX_SIMPLE_BUY;

			return 0;
		}

		if (saved_match.xbuy.quote_asset != XREQ_BLOCKCHAIN_BCH)
		{
			BOOST_LOG_TRIVIAL(warning) << "ExchangeRequest::UpdateTotalMined xmatchnum " << xmatch.xmatchnum << " quote asset " << saved_match.xbuy.quote_asset << " != " << XREQ_BLOCKCHAIN_BCH;

			return 0;
		}

		if (saved_match.xbuy.base_asset)
		{
			BOOST_LOG_TRIVIAL(warning) << "ExchangeRequest::UpdateTotalMined xmatchnum " << xmatch.xmatchnum << " base asset " << saved_match.xbuy.base_asset << " != 0";

			return 0;
		}

		break;
	}

	// Add match mining_amount to the wallet's total amount mined

	bigint_t mined, total;
	packed_unsigned_amount_t packed_amount;

	auto rc = amount_from_float(MINED_ASSET, (amtfloat_t)xmatch.mining_amount.asFullString(), mined);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "ExchangeRequest::UpdateTotalMined xmatchnum " << xmatch.xmatchnum << " error converting amount " << xmatch.mining_amount;

		return 0;
	}

	rc = dbconn->ParameterSelect(DB_KEY_TOTAL_MINED, 0, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES);
	if (rc < 0)
		return rc;
	else if (rc)
		total = 0UL;
	else
		unpack_unsigned_amount(packed_amount, total);

	total = total + mined;

	CCASSERTZ(pack_unsigned_amount(total, packed_amount));

	rc = dbconn->ParameterInsert(DB_KEY_TOTAL_MINED, 0, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES);
	if (rc)
		return rc;

	BOOST_LOG_TRIVIAL(info) << "ExchangeMatch::UpdateTotalMined total " << total << " ; " << xmatch.DebugString();

	return 0;
}

int Exchange::GetTotalMined(DbConn *dbconn, bigint_t& total_mined)
{
	total_mined = 0UL;

	packed_unsigned_amount_t packed_amount;

	auto rc = dbconn->ParameterSelect(DB_KEY_TOTAL_MINED, 0, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES);
	if (rc)
		return rc;

	unpack_unsigned_amount(packed_amount, total_mined);

	return 0;
}

void ExchangeMatch::UpdatePollTime(Xmatch &xmatch, uint64_t last_matching_completed_time, uint64_t delay)
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(trace) << "ExchangeMatch::UpdatePollTime time " << unixtime() << " last_matching_completed_time " << last_matching_completed_time << " delay " << delay << " ; " << xmatch.DebugString();

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	if (xmatch.IsClosed())
	{
		xmatch.wallet_polltime = 0;

		return;
	}

	if (!last_matching_completed_time)
		last_matching_completed_time = Polling::EstimatedBlocktime(unixtime());

	if (!delay && last_matching_completed_time < xmatch.next_deadline)
	{
		delay = xmatch.next_deadline - last_matching_completed_time;
		delay /= 3;

		if (delay < (unsigned)g_params.exchange_poll_time)
			delay = g_params.exchange_poll_time;

		if (TEST_RANDOM_POLLTIMES && (rand() & 1))
			delay = (rand() % (TEST_RANDOM_POLLTIMES + 1)) + 1;
	}

	xmatch.wallet_polltime = last_matching_completed_time + delay;

	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeMatch::UpdatePollTime delay " << delay << " time " << unixtime() << " last_matching_completed_time " << last_matching_completed_time << " ; " << xmatch.DebugString();
}

int ExchangeMatch::PollXmatch(DbConn *dbconn, TxQuery& txquery, const Xmatch &xmatch, BlockChainStatus& blockchain_status)
{
	if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeMatch::PollXmatch time " << unixtime() << " ; " << xmatch.DebugString();

	blockchain_status.Clear();

	QueryXmatchResults results;

	if (xmatch.IsClosed())
		results.xmatch = xmatch;
	else
	{
		auto rc = txquery.QueryXmatch(g_params.blockchain, xmatch.xmatchnum, results);
		if (rc) return rc;

		blockchain_status.Copy(results.blockchain_status);

		if (results.xmatch.xmatchnum != xmatch.xmatchnum)
		{
			BOOST_LOG_TRIVIAL(info) << "ExchangeMatch::PollXmatch result xmatchnum " << results.xmatch.xmatchnum << " != " << xmatch.xmatchnum;

			return -1;
		}

		if (results.xmatch.xbuy.xreqnum != xmatch.xbuy.xreqnum)
		{
			BOOST_LOG_TRIVIAL(info) << "ExchangeMatch::PollXmatch result xbuy.xreqnum " << results.xmatch.xbuy.xreqnum << " != " << xmatch.xbuy.xreqnum;

			return -1;
		}

		if (results.xmatch.xsell.xreqnum != xmatch.xsell.xreqnum)
		{
			BOOST_LOG_TRIVIAL(info) << "ExchangeMatch::PollXmatch result xsell.xreqnum " << results.xmatch.xsell.xreqnum << " != " << xmatch.xsell.xreqnum;

			return -1;
		}

		if (results.xmatch.IsOpen() || g_shutdown)
			return 0;
	}

	results.xmatch.wallet_polltime = 0;	// no need to poll again

	// poll addresses to get settlement amounts

	CCASSERT(xmatch.have_xreqs);

	if (xmatch.xbuy.address_id)
	{
		if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeMatch::PollXmatch address of xbuy " << xmatch.xbuy.DebugString();

		auto rc = ExchangeRequest::PollAddress(dbconn, txquery, xmatch.xbuy.address_id);
		if (rc < 0) return rc;
	}

	if (xmatch.xsell.address_id)
	{
		if (TRACE_EXCHANGE) BOOST_LOG_TRIVIAL(debug) << "ExchangeMatch::PollXmatch address of xsell " << xmatch.xsell.DebugString();

		auto rc = ExchangeRequest::PollAddress(dbconn, txquery, xmatch.xsell.address_id);
		if (rc < 0) return rc;
	}

	while (!g_shutdown) // use break to exit
	{
		auto rc = dbconn->BeginWrite();
		if (rc)
		{
			dbconn->DoDbFinishTx(-1);

			return -1;
		}

		Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

		if (xmatch.xbuy.tx_id)
		{
			rc = ExchangeMatch::UpdateTotalMined(dbconn, results.xmatch);
			if (rc) return rc;
		}

		rc = dbconn->ExchangeMatchInsert(results.xmatch);
		if (rc) return rc;

		// commit db writes

		if (RandTest(RTEST_CUZZ)) sleep(1);

		if (g_shutdown)
			break;

		rc = dbconn->Commit();
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "ExchangeMatch::PollXmatch error committing db transaction";

			return -1;
		}

		dbconn->DoDbFinishTx();

		finally.Clear();

		break;
	}

	//cerr << "PollXmatch ok" << endl;

	return 0;
}
