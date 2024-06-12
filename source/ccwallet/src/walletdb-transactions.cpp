/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
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

#define TRACE_DB_READ	(g_params.trace_db_reads)
#define TRACE_DB_WRITE	(g_params.trace_db_writes)

void DbConn::TransactionInitDb()
{
	// insert a null tx with Id = 1, since Parent = 1 is used to mark a tx that is a parent of another tx
	auto rc = sqlite3_exec(Wallet_db, "insert or ignore into Transactions (Id, Parent, Type, Status, CreateTime) values (1, 1, 0, 0, strftime('%s','now'));", 0, 0, 0);
	if (dbiserr(rc) && dbresult(rc) != SQLITE_CONSTRAINT)
		throw runtime_error("Error saving null transaction");
}

int DbConn::TransactionInsert(Transaction& tx, bool lock_optional)
{
	CCASSERT(lock_optional || GetTxnState() == SQLITE_TXN_WRITE);

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionInsert " << tx.DebugString();

	CCASSERT(tx.IsValid());

	if (!tx.create_time)
		tx.create_time = unixtime();

	// Donation is stored in its wire format (40 bit floating point) -- ToDo: compress Donation by omitting leading zeros?

	// Id, Parent, Type, Status, RefId, Blockchain, ParamLevel, CreateTime, BtcBlockLevel, Donation, ObjId, Body
	if (tx.id)
	{
		if (dblog(sqlite3_bind_int64(Transactions_insert, 1, tx.id))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Transactions_insert, 1))) return -1;
	}
	if (dblog(sqlite3_bind_int64(Transactions_insert, 2, tx.parent_id))) return -1;
	if (dblog(sqlite3_bind_int(Transactions_insert, 3, tx.type))) return -1;
	if (dblog(sqlite3_bind_int(Transactions_insert, 4, tx.status))) return -1;
	if (tx.ref_id.length())
	{
		if (dblog(sqlite3_bind_text(Transactions_insert, 5, tx.ref_id.c_str(), -1, SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Transactions_insert, 5))) return -1;
	}
	if (dblog(sqlite3_bind_int64(Transactions_insert, 6, tx.blockchain))) return -1;
	if (dblog(sqlite3_bind_int64(Transactions_insert, 7, tx.param_level))) return -1;
	if (dblog(sqlite3_bind_int64(Transactions_insert, 8, tx.create_time))) return -1;
	if (dblog(sqlite3_bind_int64(Transactions_insert, 9, tx.btc_block))) return -1;
	if (dblog(sqlite3_bind_blob(Transactions_insert, 10, &tx.donation, TX_AMOUNT_DECODED_BYTES, SQLITE_STATIC))) return -1;
	if (tx.have_objid)
	{
		if (dblog(sqlite3_bind_blob(Transactions_insert, 11, &tx.objid, sizeof(tx.objid), SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Transactions_insert, 11))) return -1;
	}
	if (dblog(sqlite3_bind_blob(Transactions_insert, 12, tx.txbody.data(), tx.txbody.size(), SQLITE_STATIC))) return -1;

	CCASSERT(DB_TX_COLS == 12);

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::TransactionInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Transactions_insert);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::TransactionInsert constraint violation " << tx.DebugString();

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

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::TransactionInsert inserted " << tx.DebugString();

	return 0;
}

int DbConn::TransactionSelectInternal(sqlite3_stmt *select, Transaction& tx, bool expect_row, uint64_t required_id, int cs)
{
	auto base = (sqlite3_data_count(select) == DB_TX_COLS);

	uint64_t id = sqlite3_column_int64(select, cs + 0);

	if (required_id && required_id != id)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::TransactionSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";

		return 1;
	}

	if (id < TX_ID_MINIMUM && (id || base))	// if !base then id can be zero
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::TransactionSelect id < min, returning SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelect id < min, returning SQLITE_DONE";

		return 1;
	}

	// Id, Parent, Type, Status, RefId, Blockchain, ParamLevel, CreateTime, BtcBlockLevel, Donation, ObjId, Body
	uint64_t parent_id = sqlite3_column_int64(select, cs + 1);
	unsigned type = sqlite3_column_int(select, cs + 2);
	unsigned status = sqlite3_column_int(select, cs + 3);
	auto ref_id = sqlite3_column_text(select, cs + 4);
	uint64_t blockchain = sqlite3_column_int64(select, cs + 5);
	uint64_t param_level = sqlite3_column_int64(select, cs + 6);
	uint64_t create_time = sqlite3_column_int64(select, cs + 7);
	uint64_t btc_block = sqlite3_column_int64(select, cs + 8);
	auto donation_blob = sqlite3_column_blob(select, cs + 9);
	auto donation_size = sqlite3_column_bytes(select, cs + 9);
	auto objid_blob = sqlite3_column_blob(select, cs + 10);
	auto objid_size = sqlite3_column_bytes(select, cs + 10);
	auto txbody_blob = sqlite3_column_blob(select, cs + 11);
	auto txbody_size = sqlite3_column_bytes(select, cs + 11);

	CCASSERT(DB_TX_COLS == 12);

	if (base && !Transaction::StatusIsValid(status))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect returned id " << id << " invalid status " << status;

		return -1;
	}

	if (base && !donation_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect returned id " << id << " donation_blob null";

		return -1;
	}

	if (donation_blob && donation_size != TX_AMOUNT_DECODED_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect returned id " << id << " donation size " << donation_size << " != " << TX_AMOUNT_DECODED_BYTES;

		return -1;
	}

	if (base && objid_blob && objid_size != sizeof(tx.objid))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect returned id " << id << " objid size " << objid_size << " != " << sizeof(tx.objid);

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
	if (ref_id)
		tx.ref_id = (char*)ref_id;
	else
		tx.ref_id.clear();
	tx.blockchain = blockchain;
	tx.param_level = param_level;
	tx.create_time = create_time;
	tx.btc_block = btc_block;

	if (donation_blob)
		memcpy((void*)&tx.donation, donation_blob, TX_AMOUNT_DECODED_BYTES);

	if (objid_blob)
	{
		memcpy(&tx.objid, objid_blob, sizeof(tx.objid));
		tx.have_objid = true;
	}
	else
		tx.have_objid = false;

	if (txbody_blob && txbody_size)
	{
		tx.txbody.resize(txbody_size);
		memcpy(tx.txbody.data(), txbody_blob, txbody_size);
	}
	else
		tx.txbody.clear();

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::TransactionSelect returning " << tx.DebugString();

	return 0;
}

int DbConn::TransactionSelect(sqlite3_stmt *select, Transaction& tx, bool expect_row, uint64_t required_id, int cs)
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
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::TransactionSelect returned SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelect returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != DB_TX_COLS)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TransactionSelect returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	return TransactionSelectInternal(select, tx, expect_row, required_id, cs);
}

int DbConn::TransactionSelectId(uint64_t id, Transaction& tx, bool or_greater)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectId id " << id << " or_greater " << or_greater;

	CCASSERT(id);

	tx.Clear();

	// Id >=
	if (dblog(sqlite3_bind_int64(Transactions_select, 1, id))) return -1;

	return TransactionSelect(Transactions_select, tx, !or_greater, !or_greater * id);
}

int DbConn::TransactionSelectRefId(const char* ref_id, Transaction& tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectRefId ref_id " << ref_id;

	CCASSERT(ref_id);

	tx.Clear();

	// RefId
	if (dblog(sqlite3_bind_text(Transactions_select_refid, 1, ref_id, -1, SQLITE_STATIC))) return -1;

	return TransactionSelect(Transactions_select_refid, tx);
}

int DbConn::TransactionSelectIdDescending(uint64_t id, Transaction& tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectIdDescending id " << id;

	tx.Clear();

	// Id <=
	if (dblog(sqlite3_bind_int64(Transactions_select_id_descending, 1, id))) return -1;

	return TransactionSelect(Transactions_select_id_descending, tx);
}

int DbConn::TransactionSelectObjIdDescendingId(const ccoid_t& objid, uint64_t id, Transaction& tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectObjIdDescendingId id " << id << " objid " << buf2hex(&objid, CC_OID_TRACE_SIZE);

	tx.Clear();

	// ObjId, Id <=
	if (dblog(sqlite3_bind_blob(Transactions_select_objid_descending_id, 1, &objid, sizeof(objid), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Transactions_select_objid_descending_id, 2, id))) return -1;

	return TransactionSelect(Transactions_select_objid_descending_id, tx);
}

int DbConn::TransactionSelectLevel(uint64_t level, uint64_t last_id, Transaction& tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectLevel level " << level << " last_id " << last_id;

	tx.Clear();

	// (BtcBlockLevel, Id) > (?1, ?2)
	if (dblog(sqlite3_bind_int64(Transactions_select_level, 1, level))) return -1;
	if (dblog(sqlite3_bind_int64(Transactions_select_level, 2, last_id))) return -1;

	return TransactionSelect(Transactions_select_level, tx);
}

int DbConn::TransactionSelectLevelDescending(uint64_t level, uint64_t id, Transaction& tx)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::TransactionSelectLevelDescending level " << level << " id " << id;

	tx.Clear();

	// (BtcBlockLevel, Id) < (?1, ?2)
	if (dblog(sqlite3_bind_int64(Transactions_select_level_descending, 1, level))) return -1;
	if (dblog(sqlite3_bind_int64(Transactions_select_level_descending, 2, id))) return -1;

	return TransactionSelect(Transactions_select_level_descending, tx);
}
