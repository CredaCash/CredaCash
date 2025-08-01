/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * walletdb-exchange.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"

#include <xtransaction.hpp>
#include <xtransaction-xreq.hpp>
#include <xmatch.hpp>
#include <dblog.h>

#define TRACE_DB_READ	(g_params.trace_db_reads)
#define TRACE_DB_WRITE	(g_params.trace_db_writes)

int DbConn::ExchangeRequestInsert(Xmatchreq& req, bool lock_optional)
{
	CCASSERT(lock_optional || GetTxnState() == SQLITE_TXN_WRITE);

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestInsert " << req.DebugString();
	//BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestInsert " << req.DebugString();

	CCASSERTZ(req.id);
	CCASSERT (req.tx_id || req.xreqnum);

	// ToDo: compress OpenAmount

	//Exchange_Requests:
	// 1:XId integer
	// 2:TxId integer, AddressId integer, Xreqnum integer, QueryXmatchnum integer
	// 6:Type integer, IsBuyer integer
	// 8:Disposition integer, ExpireTime integer, PollTime integer
	//11:BaseAsset integer, QuoteAsset integer, ForeignAsset string
	//14:MinAmount blob, MaxAmount blob
	//16:NetRateRequired float, WaitDiscount float, BaseCosts float, QuoteCosts float
	//20:AddImmediatelyToBlockchain integer, AutoAcceptMatches integer, NoMinimumAfterFirstMatch integer, MustLiquidateCrossingMinimum integer, MustLiquidateBelowMinimum integer
	//25:ConsiderationRequired integer, ConsiderationOffered integer, Pledge integer
	//28:HoldTime integer, HoldTimeRequired integer, MinWaitTime integer
	//31:AcceptTimeRequired integer, AcceptTimeOffered integer
	//33:PaymentTime integer, Confirmations integer
	//35:ForeignAddress blob
	//36:PrivateSigningKey blob
	//37:OpenAmount blob
	if (dblog(sqlite3_bind_null(Exchange_Requests_insert, 1))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_insert, 2, req.tx_id))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_insert, 3, req.address_id))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_insert, 4, req.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_insert, 5, req.query_matchnum))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 6, req.type))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 7, Xtx::TypeIsBuyer(req.type)))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 8, req.disposition))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_insert, 9, req.expire_time))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_insert, 10, req.poll_time))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_insert, 11, req.base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_insert, 12, req.quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(Exchange_Requests_insert, 13, req.foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Exchange_Requests_insert, 14, &req.min_amount, sizeof(req.min_amount), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Exchange_Requests_insert, 15, &req.max_amount, sizeof(req.max_amount), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Exchange_Requests_insert, 16, req.net_rate_required.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Exchange_Requests_insert, 17, req.wait_discount.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Exchange_Requests_insert, 18, req.base_costs.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Exchange_Requests_insert, 19, req.quote_costs.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 20, req.flags.add_immediately_to_blockchain))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 21, req.flags.auto_accept_matches))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 22, req.flags.no_minimum_after_first_match))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 23, req.flags.must_liquidate_crossing_minimum))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 24, req.flags.must_liquidate_below_minimum))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 25, req.consideration_required))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 26, req.consideration_offered))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 27, req.pledge))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 28, req.hold_time))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 29, req.hold_time_required))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 30, req.min_wait_time))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 31, req.accept_time_required))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 32, req.accept_time_offered))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 33, req.payment_time))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_insert, 34, req.confirmations))) return -1;

	if (req.foreign_address.length())
	{
		if (dblog(sqlite3_bind_blob(Exchange_Requests_insert, 35, req.foreign_address.c_str(), req.foreign_address.length(), SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Exchange_Requests_insert, 35))) return -1;
	}

	if (req.flags.has_signing_key && req.tx_id)
	{
		if (dblog(sqlite3_bind_blob(Exchange_Requests_insert, 36, &req.signing_private_key, sizeof(req.signing_private_key), SQLITE_STATIC))) return -1;
	}
	else if (req.flags.has_signing_key)
	{
		if (dblog(sqlite3_bind_blob(Exchange_Requests_insert, 36, &req.signing_public_key, sizeof(req.signing_public_key), SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Exchange_Requests_insert, 36))) return -1;
	}

	if (req.open_amount)
	{
		if (dblog(sqlite3_bind_blob(Exchange_Requests_insert, 37, &req.open_amount, sizeof(req.open_amount), SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Exchange_Requests_insert, 37))) return -1;
	}

	CCASSERT(DB_REQ_COLS == 37);

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Exchange_Requests_insert);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestInsert constraint violation " << req.DebugString();

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (!req.id && changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestInsert sqlite3_changes " << changes << " after insert " << req.DebugString();

		return -1;
	}

	if (!req.id)
		req.id = sqlite3_last_insert_rowid(Wallet_db);

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::ExchangeRequestInsert inserted " << req.DebugString();

	return 0;
}

int DbConn::ExchangeRequestUpdateStatus(Xmatchreq& req, bool lock_optional)
{
	CCASSERT(lock_optional || GetTxnState() == SQLITE_TXN_WRITE);

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestUpdateStatus " << req.DebugString();
	//BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestUpdateStatus " << req.DebugString();

	CCASSERT(req.id);

	// XId integer, Xreqnum integer, QueryXmatchnum integer, Disposition integer, OpenAmount blob
	if (dblog(sqlite3_bind_int64(Exchange_Requests_status_update, 1, req.id))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_status_update, 2, req.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_status_update, 3, req.query_matchnum))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Requests_status_update, 4, req.disposition))) return -1;
	if (req.open_amount)
	{
		if (dblog(sqlite3_bind_blob(Exchange_Requests_status_update, 5, &req.open_amount, sizeof(req.open_amount), SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Exchange_Requests_status_update, 5))) return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestUpdateStatus simulating database error pre-update";

		return -1;
	}

	auto rc = sqlite3_step(Exchange_Requests_status_update);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestUpdateStatus constraint violation " << req.DebugString();

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::ExchangeRequestUpdateStatus changes " << changes << " after update " << req.DebugString();

	return 0;
}

int DbConn::ExchangeRequestUpdatePolling(uint64_t id, bool by_txid, uint64_t poll_time)
{
	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestUpdatePolling id " << id << " by_txid " << by_txid << " poll_time " << poll_time;

	CCASSERT(id);

	auto select = (by_txid ? Exchange_Requests_polling_update_txid : Exchange_Requests_polling_update);

	// XId/TxId integer, PollTime integer
	if (dblog(sqlite3_bind_int64(select, 1, id))) return -1;
	if (dblog(sqlite3_bind_int64(select, 2, poll_time))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestUpdatePolling simulating database error pre-update";

		return -1;
	}

	auto rc = sqlite3_step(select);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::ExchangeRequestUpdatePolling changes " << changes << " after update id " << id << " by_txid " << by_txid << " poll_time " << poll_time;

	return 0;
}

int DbConn::ExchangeRequestSelectInternal(sqlite3_stmt *select, Xmatchreq& req, Transaction *tx, int cs, bool expect_row, uint64_t required_id, uint64_t required_tx_id, uint64_t required_reqnum)
{
	uint64_t id = sqlite3_column_int64(select, cs + 0);
	uint64_t tx_id = sqlite3_column_int64(select, cs + 1);
	uint64_t reqnum = sqlite3_column_int64(select, cs + 3);

	if (required_id && required_id != id)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::ExchangeRequestSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";

		return 1;
	}

	if (required_tx_id && required_tx_id != tx_id)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::ExchangeRequestSelect result tx_id " << tx_id << " != required_tx_id " << required_tx_id << " returning SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelect result tx_id " << tx_id << " != required_tx_id " << required_tx_id << " returning SQLITE_DONE";

		return 1;
	}

	if (required_reqnum && required_reqnum != reqnum)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::ExchangeRequestSelect result reqnum " << reqnum << " != required_reqnum " << required_reqnum << " returning SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelect result reqnum " << reqnum << " != required_reqnum " << required_reqnum << " returning SQLITE_DONE";

		return 1;
	}

	auto foreign_asset_text = sqlite3_column_text(select, cs + 12);
	auto min_blob = sqlite3_column_blob(select, cs + 13);
	auto min_size = sqlite3_column_bytes(select, cs + 13);
	auto max_blob = sqlite3_column_blob(select, cs + 14);
	auto max_size = sqlite3_column_bytes(select, cs + 14);
	auto foreign_address_text = sqlite3_column_text(select, cs + 34);
	auto signing_key_blob = sqlite3_column_blob(select, cs + 35);
	auto signing_key_size = sqlite3_column_bytes(select, cs + 35);
	auto open_amount_blob = sqlite3_column_blob(select, cs + 36);
	auto open_amount_size = sqlite3_column_bytes(select, cs + 36);

	if (!foreign_asset_text)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect foreign_asset_text is null";

		return -1;
	}

	if (!min_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect min amount is null";

		return -1;
	}

	if (min_size != sizeof(req.min_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect returned min amount size " << min_size << " != " << sizeof(req.min_amount);

		return -1;
	}

	if (!max_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect max amount is null";

		return -1;
	}

	if (max_size != sizeof(req.max_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect returned max amount size " << max_size << " != " << sizeof(req.max_amount);

		return -1;
	}

	if (tx_id && signing_key_blob && signing_key_size != sizeof(req.signing_private_key))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect returned signing key size " << signing_key_size << " != " << sizeof(req.signing_private_key);

		return -1;
	}

	if (!tx_id && signing_key_blob && signing_key_size != sizeof(req.signing_public_key))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect returned signing key size " << signing_key_size << " != " << sizeof(req.signing_public_key);

		return -1;
	}

	if (open_amount_blob && open_amount_size != sizeof(req.open_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect returned open_amount size " << open_amount_size << " != " << sizeof(req.open_amount);

		return -1;
	}

	//Exchange_Requests:
	// 0:XId integer
	// 1:TxId integer, AddressId integer, Xreqnum integer, QueryXmatchnum integer
	// 5:Type integer, IsBuyer integer
	// 7:Disposition integer, ExpireTime integer, PollTime integer
	//10:BaseAsset integer, QuoteAsset integer, ForeignAsset string
	//13:MinAmount blob, MaxAmount blob
	//15:NetRateRequired float, WaitDiscount float, BaseCosts float, QuoteCosts float
	//19:AddImmediatelyToBlockchain integer, AutoAcceptMatches integer, NoMinimumAfterFirstMatch integer, MustLiquidateCrossingMinimum integer, MustLiquidateBelowMinimum integer
	//24:ConsiderationRequired integer, ConsiderationOffered integer, Pledge integer
	//27:HoldTime integer, HoldTimeRequired integer, MinWaitTime integer
	//30:AcceptTimeRequired integer, AcceptTimeOffered integer
	//32:PaymentTime integer, Confirmations integer
	//34:ForeignAddress blob
	//35:PrivateSigningKey blob
	//36:OpenAmount blob
	req.id = id;
	req.tx_id = tx_id;
	req.address_id = sqlite3_column_int64(select, cs + 2);
	req.xreqnum = reqnum;
	req.query_matchnum = sqlite3_column_int64(select, cs + 4);
	req.type = sqlite3_column_int(select, cs + 5);
	//req.isbuyer = sqlite3_column_int(select, cs + 6);
	req.disposition = sqlite3_column_int(select, cs + 7);
	req.expire_time = sqlite3_column_int64(select, cs + 8);
	req.poll_time = sqlite3_column_int64(select, cs + 9);
	req.base_asset = sqlite3_column_int64(select, cs + 10);
	req.quote_asset = sqlite3_column_int64(select, cs + 11);
	//req.foreign_asset = sqlite3_column_text(select, cs + 12);
	//req.min_amount = sqlite3_column_blob(select, cs + 13);
	//req.max_amount = sqlite3_column_blob(select, cs + 14);
	req.net_rate_required = sqlite3_column_double(select, cs + 15);
	req.wait_discount = sqlite3_column_double(select, cs + 16);
	req.base_costs = sqlite3_column_double(select, cs + 17);
	req.quote_costs = sqlite3_column_double(select, cs + 18);
	req.flags.add_immediately_to_blockchain = sqlite3_column_int(select, cs + 19);
	req.flags.auto_accept_matches = sqlite3_column_int(select, cs + 20);
	req.flags.no_minimum_after_first_match = sqlite3_column_int(select, cs + 21);
	req.flags.must_liquidate_crossing_minimum = sqlite3_column_int(select, cs + 22);
	req.flags.must_liquidate_below_minimum = sqlite3_column_int(select, cs + 23);
	req.consideration_required = sqlite3_column_int(select, cs + 24);
	req.consideration_offered = sqlite3_column_int(select, cs + 25);
	req.pledge = sqlite3_column_int(select, cs + 26);
	req.hold_time = sqlite3_column_int(select, cs + 27);
	req.hold_time_required = sqlite3_column_int(select, cs + 28);
	req.min_wait_time = sqlite3_column_int(select, cs + 29);
	req.accept_time_required = sqlite3_column_int(select, cs + 30);
	req.accept_time_offered = sqlite3_column_int(select, cs + 31);
	req.payment_time = sqlite3_column_int(select, cs + 32);
	req.confirmations = sqlite3_column_int(select, cs + 33);
	//req.foreign_address_text = sqlite3_column_text(select, cs + 34);
	//req.signing_key_blob = sqlite3_column_blob(select, cs + 35);
	//req.open_amount_blob = sqlite3_column_blob(select, cs + 36);

	CCASSERT(DB_REQ_COLS == 37);

	if (tx && req.tx_id)
	{
		auto rc = TransactionSelectInternal(select, *tx, true, req.tx_id, cs + DB_REQ_COLS);
		if (rc) goto err;

		memcpy(&req.objid, &tx->objid, sizeof(req.objid));

		Xreq::ConvertTradeObjIdToSellObjId(tx->type, req.type, req.objid);
	}

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) goto err;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestSelect simulating database error post-error check";

		goto err;
	}

	memcpy((void*)&req.min_amount, min_blob, sizeof(req.min_amount));
	memcpy((void*)&req.max_amount, max_blob, sizeof(req.max_amount));

	if (foreign_asset_text)				req.foreign_asset = (char*)foreign_asset_text;
	if (foreign_address_text)			req.foreign_address = (char*)foreign_address_text;

	if (open_amount_blob)				memcpy((void*)&req.open_amount, open_amount_blob, sizeof(req.open_amount));

	if (signing_key_blob && req.tx_id)	memcpy(&req.signing_private_key, signing_key_blob, sizeof(req.signing_private_key));
	else if (signing_key_blob)			memcpy(&req.signing_public_key, signing_key_blob, sizeof(req.signing_public_key));

	if (signing_key_blob)				req.flags.has_signing_key = true;


	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::ExchangeRequestSelect returning " << req.DebugString();

	return 0;

err:
	req.Clear();
	if (tx) tx->Clear();

	return -1;
}

int DbConn::ExchangeRequestSelect(sqlite3_stmt *select, Xmatchreq& req, Transaction *tx, bool expect_row, uint64_t required_id, uint64_t required_tx_id, uint64_t required_reqnum)
{
	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::ExchangeRequestSelect returned SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelect returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != DB_REQ_COLS + DB_TX_COLS)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestSelect returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	rc = ExchangeRequestSelectInternal(select, req, tx, 0, expect_row, required_id, required_tx_id, required_reqnum);
	if (rc) return rc;

	return 0;
}

int DbConn::ExchangeRequestSelectId(uint64_t id, Xmatchreq& req, Transaction *tx, bool or_greater)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelectId id " << id << " or_greater " << or_greater;

	CCASSERT(id);

	req.Clear();
	if (tx) tx->Clear();

	// XId >=
	if (dblog(sqlite3_bind_int64(Exchange_Requests_select, 1, id))) return -1;

	return ExchangeRequestSelect(Exchange_Requests_select, req, tx, !or_greater, !or_greater * id);
}

int DbConn::ExchangeRequestSelectIdDescending(uint64_t id, Xmatchreq& req, Transaction *tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelectIdDescending id " << id;

	req.Clear();
	if (tx) tx->Clear();

	// XId <=
	if (dblog(sqlite3_bind_int64(Exchange_Requests_select_id_descending, 1, id))) return -1;

	return ExchangeRequestSelect(Exchange_Requests_select_id_descending, req, tx);
}

int DbConn::ExchangeRequestSelectTxId(uint64_t tx_id, uint64_t id, Xmatchreq& req, Transaction *tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelectTxId tx_id " << tx_id << " id " << id;

	CCASSERT(tx_id);

	req.Clear();
	if (tx) tx->Clear();

	// TxId, >Id
	if (dblog(sqlite3_bind_int64(Exchange_Requests_select_txid, 1, tx_id))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_select_txid, 2, id))) return -1;

	return ExchangeRequestSelect(Exchange_Requests_select_txid, req, tx, false, 0, tx_id);
}

int DbConn::ExchangeRequestSelectXreqnum(uint64_t xreqnum, Xmatchreq& req, Transaction *tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelectXreqnum xreqnum " << xreqnum;

	CCASSERT(xreqnum);

	req.Clear();
	if (tx) tx->Clear();

	// Xreqnum
	if (dblog(sqlite3_bind_int64(Exchange_Requests_select_xreqnum, 1, xreqnum))) return -1;

	return ExchangeRequestSelect(Exchange_Requests_select_xreqnum, req, tx, false, 0, 0, xreqnum);
}

int DbConn::ExchangeRequestSelectNextPoll(uint64_t checktime, Xmatchreq& req, Transaction *tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestSelectNextPoll checktime " << checktime;

	req.Clear();
	if (tx) tx->Clear();

	// PollTime <=
	if (dblog(sqlite3_bind_int64(Exchange_Requests_select_next_poll, 1, checktime))) return -1;

	return ExchangeRequestSelect(Exchange_Requests_select_next_poll, req, tx);
}

int DbConn::ExchangeRequestsSumPending(uint64_t base_asset, uint64_t quote_asset, const string& foreign_asset, double total_pending[54])
{
	memset(total_pending, 0, 4*sizeof(double));

	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeRequestsSumPending base_asset " << base_asset << " quote_asset " << quote_asset << " foreign_asset " << foreign_asset;

	// BaseAsset = ?1 and QuoteAsset = ?2 and ForeignAsset = ?3
	if (dblog(sqlite3_bind_int64(Exchange_Requests_select_pending, 1, base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Requests_select_pending, 2, quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(Exchange_Requests_select_pending, 3, foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;

	while (!g_shutdown)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Exchange_Requests_select_pending), DB_STMT_SELECT))
			return -1;

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeRequestsSumPending simulating database error post-select";

			return -1;
		}

		if (dbresult(rc) == SQLITE_DONE)
			break;

		if (dbresult(rc) != SQLITE_ROW)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestsSumPending select returned " << rc;

			return -1;
		}

		if (sqlite3_data_count(Exchange_Requests_select_pending) != 4)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestsSumPending select returned " << sqlite3_data_count(Exchange_Requests_select_pending) << " columns";

			return -1;
		}

		// IsBuyer, NetRateRequired, OpenAmount, Pledge
		bool isbuyer = sqlite3_column_int(Exchange_Requests_select_pending, 0);
		auto net_rate_required = sqlite3_column_double(Exchange_Requests_select_pending, 1);
		auto open_amount_blob = sqlite3_column_blob(Exchange_Requests_select_pending, 2);
		auto open_amount_size = sqlite3_column_bytes(Exchange_Requests_select_pending, 2);
		auto pledge = sqlite3_column_int(Exchange_Requests_select_pending, 3);

		if (!open_amount_blob || open_amount_size != sizeof(bigint_t))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeRequestsSumPending select returned open_amount size " << open_amount_size << " != " << sizeof(bigint_t);

			return -1;
		}

		const bigint_t& open_amount = *(bigint_t*)open_amount_blob;

		auto amount = Xtx::asFullFloat(base_asset, open_amount);

		total_pending[1 - isbuyer] += (double)amount;						// CredaCash total_pending[0] = buyer may get; total_pending[1] = tied up in unmatched sell reqs
		total_pending[3 - isbuyer] += (double)(amount * net_rate_required);	// Foreign   total_pending[2] = buyer may pay; total_pending[3] = seller may get

		if (isbuyer)
			total_pending[4] += (double)(amount * pledge / 100.0);			// CredaCash total_pending[4] = tied up in unmatched buy reqs

		if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::ExchangeRequestsSumPending found isbuyer " << isbuyer << " net_rate_required " << net_rate_required << " open_amount " << amount << " pledge " << pledge << " totals " << total_pending[0] << " " << total_pending[1] << " " << total_pending[2] << " " << total_pending[3] << " " << total_pending[4];
	}

	return 0;
}

int DbConn::ExchangeMatchesSumPending(uint64_t base_asset, uint64_t quote_asset, const string& foreign_asset, double total_pending[5])
{
	memset(total_pending, 0, 4*sizeof(double));

	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchesSumPending base_asset " << base_asset << " quote_asset " << quote_asset << " foreign_asset " << foreign_asset;

	// BaseAsset = ?1 and QuoteAsset = ?2 and ForeignAsset = ?3
	if (dblog(sqlite3_bind_int64(Exchange_Matches_select_pending, 1, base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_select_pending, 2, quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(Exchange_Matches_select_pending, 3, foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;

	while (!g_shutdown)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Exchange_Matches_select_pending), DB_STMT_SELECT))
			return -1;

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeMatchesSumPending simulating database error post-select";

			return -1;
		}

		if (dbresult(rc) == SQLITE_DONE)
			break;

		if (dbresult(rc) != SQLITE_ROW)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchesSumPending select returned " << rc;

			return -1;
		}

		if (sqlite3_data_count(Exchange_Matches_select_pending) != 5)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchesSumPending select returned " << sqlite3_data_count(Exchange_Matches_select_pending) << " columns";

			return -1;
		}

		// Xbuy.TxId, Xsell.TxId, Rate, BaseAmount, BuyerPledge
		bool isbuyer = sqlite3_column_int(Exchange_Matches_select_pending, 0);
		bool is_seller = sqlite3_column_int(Exchange_Matches_select_pending, 1);
		auto rate = sqlite3_column_double(Exchange_Matches_select_pending, 2);
		auto amount_blob = sqlite3_column_blob(Exchange_Matches_select_pending, 3);
		auto amount_size = sqlite3_column_bytes(Exchange_Matches_select_pending, 3);
		auto pledge = sqlite3_column_int(Exchange_Matches_select_pending, 4);

		if (!amount_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchesSumPending amount is null";

			return -1;
		}

		if (amount_size != sizeof(bigint_t))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchesSumPending returned amount size " << amount_size << " != " << sizeof(bigint_t);

			return -1;
		}

		auto amount = Xtx::asDouble(base_asset, *(const bigint_t*)amount_blob);

		auto forn = amount * rate;

		if (isbuyer)
		{
			total_pending[0] += amount;										// CredaCash total_pending[0] = buyer may get
			if (!is_seller)													//	don't count Foreign amounts buyer may pay to self
			total_pending[2] += forn;										// Foreign   total_pending[2] = buyer may pay
			total_pending[4] += amount * pledge / 100.0;					// CredaCash total_pending[4] = tied up in matched buy reqs
		}

		if (is_seller)
		{
			total_pending[1] += amount;										// CredaCash total_pending[1] = tied up in matched sell reqs
			if (!isbuyer)													//	don't count Foreign amounts seller may get from self
			total_pending[3] += forn;										// Foreign   total_pending[3] = seller may get
		}

		if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchesSumPending found isbuyer " << isbuyer << " is_seller " << is_seller << " rate " << rate << " amount " << amount << " pledge " << pledge << " totals " << total_pending[0] << " " << total_pending[1] << " " << total_pending[2] << " " << total_pending[3] << " " << total_pending[4];
	}

	return 0;
}

int DbConn::ExchangeMatchInsert(const Xmatch& match, bool lock_optional)
{
	CCASSERT(lock_optional || GetTxnState() == SQLITE_TXN_WRITE);

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchInsert " << match.DebugString();

	//Exchange_Matches:
	// 1:Xmatchnum integer
	// 2:BuyXreqnum integer, SellXreqnum integer
	// 4:Type integer, Status integer, NextDeadline integer
	// 7:WalletPaid integer, WalletPaymentForeignTxid text, WalletPollTime integer, WalletReminderTime integer
	// 11:MatchTimestamp integer, AcceptTimestamp integer, FinalTimestamp integer
	// 14:BaseAmount blob, Rate float, AmountPaid float, MiningAmount float
	// 18:AcceptTime integer, BuyerConsideration integer, SellerConsideration integer
	// 21:BuyerPledge integer
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 1, match.xmatchnum))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 2, match.xbuy.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 3, match.xsell.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Matches_insert, 4, match.type))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Matches_insert, 5, match.status))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 6, match.next_deadline))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 7, match.wallet_paid))) return -1;
	if (dblog(sqlite3_bind_text(Exchange_Matches_insert, 8, match.wallet_payment_foreign_txid.c_str(), -1, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 9, match.wallet_polltime))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 10, match.wallet_reminder_time))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 11, match.match_timestamp))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 12, match.accept_timestamp))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_insert, 13, match.final_timestamp))) return -1;
	if (dblog(sqlite3_bind_blob(Exchange_Matches_insert, 14, &match.base_amount, sizeof(match.base_amount), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Exchange_Matches_insert, 15, match.rate.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Exchange_Matches_insert, 16, match.amount_paid.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Exchange_Matches_insert, 17, match.mining_amount.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Matches_insert, 18, match.accept_time))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Matches_insert, 19, match.xbuy.match_consideration))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Matches_insert, 20, match.xsell.match_consideration))) return -1;
	if (dblog(sqlite3_bind_int(Exchange_Matches_insert, 21, match.match_pledge))) return -1;

	CCASSERT(DB_MATCH_COLS == 21);

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeMatchInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Exchange_Matches_insert);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeMatchInsert constraint violation " << match.DebugString();

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::ExchangeMatchInsert changes " << changes << " after insert " << match.DebugString();

	if (0) // for testing
	{
		cerr << "Checking ExchangeMatchInsert --> ExchangeMatchSelectNum xmatchnum " << match.xmatchnum << " buyer xreqnum " << match.xbuy.xreqnum << " seller xreqnum " << match.xsell.xreqnum << endl;
		auto m1 = match.DebugString();
		Xmatch match2;
		Transaction txbuy, txsell;
		ExchangeMatchSelectNum(match.xmatchnum, match2, true, &txbuy, &txsell);
		//match2.Clear();
		auto m2 = match2.DebugString();
		if (m1 != m2)
		{
			cerr << "Xmatch mismatch:" << endl;
			cerr << "inserted: " << m1 << endl;
			cerr << "    read: " << m2 << endl;
		}
	}

	return 0;
}

int DbConn::ExchangeMatchSelectInternal(sqlite3_stmt *select, Xmatch& match, int cs, bool expect_row, uint64_t required_num)
{
	uint64_t xmatchnum = sqlite3_column_int64(select, cs + 0);

	if (required_num && required_num != xmatchnum)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::ExchangeMatchSelect result xmatchnum " << xmatchnum << " != required_num " << required_num << " returning SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchSelect result xmatchnum " << xmatchnum << " != required_num " << required_num << " returning SQLITE_DONE";

		return 1;
	}

	auto wallet_payment_foreign_txid_text = sqlite3_column_text(select, cs + 7);

	auto amount_blob = sqlite3_column_blob(select, cs + 13);
	auto amount_size = sqlite3_column_bytes(select, cs + 13);

	if (!amount_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchSelect amount is null";

		return -1;
	}

	if (amount_size != sizeof(match.base_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchSelect returned amount size " << amount_size << " != " << sizeof(match.base_amount);

		return -1;
	}

	//Exchange_Matches:
	// 0:Xmatchnum integer
	// 1:BuyXreqnum integer, SellXreqnum integer
	// 3:Type integer, Status integer, NextDeadline integer
	// 6:WalletPaid integer, WalletPaymentForeignTxid text, WalletPollTime integer, WalletReminderTime integer
	// 10:MatchTimestamp integer, AcceptTimestamp integer, FinalTimestamp integer
	// 13:BaseAmount blob, Rate float, AmountPaid float, MiningAmount float
	// 17:AcceptTime integer, BuyerConsideration integer, SellerConsideration integer
	// 20:BuyerPledge integer
	match.xmatchnum = xmatchnum;
	match.xbuy.xreqnum = sqlite3_column_int64(select, cs + 1);
	match.xsell.xreqnum = sqlite3_column_int64(select, cs + 2);
	match.type = sqlite3_column_int(select, cs + 3);
	match.status = sqlite3_column_int(select, cs + 4);
	match.next_deadline = sqlite3_column_int64(select, cs + 5);
	match.wallet_paid = sqlite3_column_int64(select, cs + 6);
	//match.wallet_payment_foreign_txid = sqlite3_column_text(select, cs + 7);
	match.wallet_polltime = sqlite3_column_int64(select, cs + 8);
	match.wallet_reminder_time = sqlite3_column_int64(select, cs + 9);
	match.match_timestamp = sqlite3_column_int64(select, cs + 10);
	match.accept_timestamp = sqlite3_column_int64(select, cs + 11);
	match.final_timestamp = sqlite3_column_int64(select, cs + 12);
	//match.base_amount = sqlite3_column_blob(select, cs + 13);
	match.rate = sqlite3_column_double(select, cs + 14);
	match.amount_paid = sqlite3_column_double(select, cs + 15);
	match.mining_amount = sqlite3_column_double(select, cs + 16);
	match.accept_time = sqlite3_column_int(select, cs + 17);
	match.xbuy.match_consideration = sqlite3_column_int(select, cs + 18);
	match.xsell.match_consideration = sqlite3_column_int(select, cs + 19);
	match.match_pledge = sqlite3_column_int(select, cs + 20);

	CCASSERT(DB_MATCH_COLS == 21);

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeMatchSelect simulating database error post-error check";

		return -1;
	}

	memcpy((void*)&match.base_amount, amount_blob, sizeof(match.base_amount));

	if (wallet_payment_foreign_txid_text) match.wallet_payment_foreign_txid = (char*)wallet_payment_foreign_txid_text;

	return 0;
}

int DbConn::ExchangeMatchSelect(sqlite3_stmt *select, Xmatch& match, bool need_reqs, Transaction *txbuy, Transaction *txsell, bool expect_row, uint64_t required_num)
{
	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeMatchSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::ExchangeMatchSelect returned SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchSelect returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchSelect returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != DB_MATCH_COLS + 2*DB_REQ_COLS + 2*DB_TX_COLS)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchSelect returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	rc = ExchangeMatchSelectInternal(select, match, 0, expect_row, required_num);
	if (rc)
	{
		match.Clear();

		return rc;
	}

	if (need_reqs)
	{
		rc = ExchangeRequestSelectInternal(select, match.xbuy, txbuy, DB_MATCH_COLS, true, 0, 0, match.xbuy.xreqnum);
		if (rc) goto err;

		rc = ExchangeRequestSelectInternal(select, match.xsell, txsell, DB_MATCH_COLS + DB_REQ_COLS + DB_TX_COLS, true, 0, 0, match.xsell.xreqnum);
		if (rc) goto err;

		match.have_xreqs = true;
	}

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::ExchangeMatchSelect returning " << match.DebugString();

	return 0;

err:
	match.Clear();
	if (txbuy) txbuy->Clear();
	if (txsell) txsell->Clear();

	return -1;
}

int DbConn::ExchangeMatchSelectNum(uint64_t matchnum, Xmatch& match, bool need_reqs, Transaction *txbuy, Transaction *txsell, bool or_greater)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchSelectNum matchnum " << matchnum << " or_greater " << or_greater << " need_reqs " << need_reqs;

	CCASSERT(matchnum);

	match.Clear();
	if (txbuy) txbuy->Clear();
	if (txsell) txsell->Clear();

	// Xmatchnum >=
	if (dblog(sqlite3_bind_int64(Exchange_Matches_select, 1, matchnum))) return -1;

	return ExchangeMatchSelect(Exchange_Matches_select, match, need_reqs, txbuy, txsell, false, !or_greater * matchnum);
}

int DbConn::ExchangeMatchSelectDeadline(uint64_t next_deadline, uint64_t matchnum, Xmatch& match, bool need_reqs, Transaction *txbuy, Transaction *txsell)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchSelectDeadline next_deadline " << next_deadline << " matchnum " << matchnum << " need_reqs " << need_reqs;

	CCASSERT(matchnum);

	match.Clear();
	if (txbuy) txbuy->Clear();
	if (txsell) txsell->Clear();

	// NextDeadline >=, Xmatchnum >
	if (dblog(sqlite3_bind_int64(Exchange_Matches_select_deadline, 1, next_deadline))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_select_deadline, 2, matchnum))) return -1;

	return ExchangeMatchSelect(Exchange_Matches_select_deadline, match, need_reqs, txbuy, txsell);
}

int DbConn::ExchangeMatchSelectNextPoll(uint64_t lastblocktime, Xmatch& match, bool need_reqs, Transaction *txbuy, Transaction *txsell)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchSelectNextPoll time " << unixtime() << " lastblocktime " << lastblocktime << " need_reqs " << need_reqs;

	// WalletPollTime <= lastblocktime
	if (dblog(sqlite3_bind_int64(Exchange_Matches_select_next_poll, 1, lastblocktime))) return -1;

	match.Clear();
	if (txbuy) txbuy->Clear();
	if (txsell) txsell->Clear();

	return ExchangeMatchSelect(Exchange_Matches_select_next_poll, match, need_reqs, txbuy, txsell);
}

int DbConn::ExchangeMatchSelectStatus(uint64_t reqnum, uint64_t matchnum_start, uint64_t &matchnum)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchSelectStatus reqnum " << reqnum << " matchnum_start " << matchnum_start;

	CCASSERT(reqnum);

	matchnum = 0;

	// Xreqnum, Xmatchnum >=
	if (dblog(sqlite3_bind_int64(Exchange_Matches_select_status, 1, reqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Exchange_Matches_select_status, 2, matchnum_start))) return -1;

	int rc;

	if (dblog(rc = sqlite3_step(Exchange_Matches_select_status), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeMatchSelectStatus simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::ExchangeMatchSelectStatus select returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchSelectStatus select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Exchange_Matches_select_status) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ExchangeMatchSelectStatus select returned " << sqlite3_data_count(Exchange_Matches_select_status) << " columns";

		return -1;
	}

	// 0:Xmatchnum integer
	matchnum = sqlite3_column_int64(Exchange_Matches_select_status, 0);

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) goto err;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ExchangeMatchSelect simulating database error post-error check";

		goto err;
	}

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::ExchangeMatchSelectStatus returning " << matchnum;

	return 0;

err:

	matchnum = 0;

	return -1;
}
