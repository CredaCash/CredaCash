/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * walletdb-transactions.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"

#include <dblog.h>

#define TRACE_DBCONN	(g_params.trace_db)

void DbConn::TransactionInitDb()
{
	auto rc = sqlite3_exec(Wallet_db, "insert or ignore into Transactions (Id, Parent, Type, Status, CreateTime) values (1, 1, 0, 0, strftime('%s','now'));", 0, 0, 0);
	if (dbiserr(rc) && dbresult(rc) != SQLITE_CONSTRAINT)
		throw runtime_error("Error saving null transaction");
}

int DbConn::TransactionInsert(Transaction& tx, bool lock_optional)
{
	CCASSERT(TestDebugWriteLocking(lock_optional));

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionInsert " << tx.DebugString();

	CCASSERT(tx.IsValid());

	if (!tx.create_time)
		tx.create_time = time(NULL);

	sqlite3_stmt *insert_update = NULL;

	// Id, Parent, Type, Status, CreateTime, BtcBlockLevel, Donation, Body
	if (tx.id)
	{
		insert_update = Transactions_update;

		if (dblog(sqlite3_bind_int64(insert_update, 1, tx.id))) return -1;
	}
	else
	{
		insert_update = Transactions_insert;

		if (dblog(sqlite3_bind_null(insert_update, 1))) return -1;
	}
	if (dblog(sqlite3_bind_int64(insert_update, 2, tx.parent_id))) return -1;
	if (dblog(sqlite3_bind_int(insert_update, 3, tx.type))) return -1;
	if (dblog(sqlite3_bind_int(insert_update, 4, tx.status))) return -1;
	if (dblog(sqlite3_bind_int64(insert_update, 5, tx.create_time))) return -1;
	if (dblog(sqlite3_bind_int64(insert_update, 6, tx.btc_block))) return -1;
	if (dblog(sqlite3_bind_blob(insert_update, 7, &tx.donation, TX_AMOUNT_DECODED_BYTES, SQLITE_STATIC))) return -1;
	if (tx.body.size())
	{
		if (dblog(sqlite3_bind_text(insert_update, 8, tx.body.c_str(), tx.body.size(), SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(insert_update, 8))) return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::TransactionInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(insert_update);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::TransactionInsert constraint violation";

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionInsert sqlite3_changes " << changes << " after insert " << tx.DebugString();

		return -1;
	}

	if (!tx.id)
		tx.id = sqlite3_last_insert_rowid(Wallet_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::TransactionInsert inserted " << tx.DebugString();

	return 0;
}

int DbConn::TransactionSelect(sqlite3_stmt *select, Transaction& tx, bool expect_row, uint64_t required_id)
{
	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::TransactionSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::TransactionSelect select returned SQLITE_DONE";
		else if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelect select returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != 8)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect select returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	// Id, Parent, Type, Status, CreateTime, BtcBlockLevel, Donation, Body
	uint64_t id = sqlite3_column_int64(select, 0);

	if (required_id && required_id != id)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::TransactionSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";
		else if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";

		return 1;
	}

	uint64_t parent_id = sqlite3_column_int64(select, 1);
	unsigned type = sqlite3_column_int(select, 2);
	unsigned status = sqlite3_column_int(select, 3);
	uint64_t create_time = sqlite3_column_int64(select, 4);
	uint64_t btc_block = sqlite3_column_int64(select, 5);
	auto donation_blob = sqlite3_column_blob(select, 6);
	auto body = sqlite3_column_text(select, 7);

	if (id < TX_ID_MINIMUM)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::TransactionSelect id < min, returning SQLITE_DONE";
		else if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelect id < min, returning SQLITE_DONE";

		return 1;
	}

	if (!Transaction::StatusIsValid(status))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect select returned invalid status " << status;

		return -1;
	}

	if (!donation_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect donation_blob is null";

		return -1;
	}

	unsigned donation_size = sqlite3_column_bytes(select, 6);
	if (donation_size != TX_AMOUNT_DECODED_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect select returned donation size " << donation_size << " != " << TX_AMOUNT_DECODED_BYTES;

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::TransactionSelect simulating database error post-error check";

		return -1;
	}

	tx.id = id;
	tx.parent_id = parent_id;
	tx.type = type;
	tx.status = status;
	tx.create_time = create_time;
	tx.btc_block = btc_block;
	memcpy(&tx.donation, donation_blob, TX_AMOUNT_DECODED_BYTES);
	if (body)
		tx.body = (char*)body;
	else
		tx.body.clear();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::TransactionSelect returning " << tx.DebugString();

	return 0;
}

int DbConn::TransactionSelectId(uint64_t id, Transaction& tx, bool or_greater)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectId id " << id << " or_greater = " << or_greater;

	CCASSERT(id);

	tx.Clear();

	// >= Id
	if (dblog(sqlite3_bind_int64(Transactions_select, 1, id))) return -1;

	return TransactionSelect(Transactions_select, tx, !or_greater, !or_greater * id);
}

int DbConn::TransactionSelectIdDescending(uint64_t id, Transaction& tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectIdDescending id " << id;

	tx.Clear();

	// <= Id
	if (dblog(sqlite3_bind_int64(Transactions_id_descending_select, 1, id))) return -1;

	return TransactionSelect(Transactions_id_descending_select, tx);
}

int DbConn::TransactionSelectLevel(uint64_t level, uint64_t id, Transaction& tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectLevel level " << level << " id " << id;

	tx.Clear();

	// >= Level, >= Id
	if (dblog(sqlite3_bind_int64(Transactions_level_select, 1, level))) return -1;
	if (dblog(sqlite3_bind_int64(Transactions_level_select, 2, id))) return -1;

	return TransactionSelect(Transactions_level_select, tx);
}

int DbConn::TransactionSelectLevelDescending(uint64_t level, uint64_t id, Transaction& tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectLevelDescending level " << level << " id " << id;

	tx.Clear();

	// <= Level, <= Id
	if (dblog(sqlite3_bind_int64(Transactions_level_descending_select, 1, level))) return -1;
	if (dblog(sqlite3_bind_int64(Transactions_level_descending_select, 2, id))) return -1;

	return TransactionSelect(Transactions_level_descending_select, tx);
}
