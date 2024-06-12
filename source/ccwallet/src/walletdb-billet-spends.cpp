/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * walletdb-billet-spends.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"
#include "billets.hpp"

#include <dblog.h>
#include <CCparams.h>

#define TRACE_DB_READ	(g_params.trace_db_reads)
#define TRACE_DB_WRITE	(g_params.trace_db_writes)

using namespace snarkfront;

int DbConn::BilletSpendInsert(uint64_t id, uint64_t bill_id, const void *hashkey, uint64_t tx_commitnum)
{
	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSpendInsert tx " << id << " bill " << bill_id << " hashkey " << buf2hex(hashkey, TX_HASHKEY_BYTES) << " tx_commitnum " << tx_commitnum;

	CCASSERT(id);
	CCASSERT(bill_id);
	CCASSERT(hashkey);

	// SpendTx, Billet, Hashkey, TxCommitnum
	if (dblog(sqlite3_bind_int64(Billet_Spends_insert, 1, id))) return -1;
	if (dblog(sqlite3_bind_int64(Billet_Spends_insert, 2, bill_id))) return -1;
	if (dblog(sqlite3_bind_blob(Billet_Spends_insert, 3, hashkey, TX_HASHKEY_BYTES, SQLITE_STATIC))) return -1;
	if (tx_commitnum)
	{
		if (dblog(sqlite3_bind_int64(Billet_Spends_insert, 4, tx_commitnum))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Billet_Spends_insert, 4))) return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::BilletSpendInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Billet_Spends_insert);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSpendInsert constraint violation tx " << id << " bill " << bill_id << " hashkey " << buf2hex(hashkey, TX_HASHKEY_BYTES) << " tx_commitnum " << tx_commitnum;

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSpendInsert sqlite3_changes " << changes << " after insert tx " << id << " bill " << bill_id << " hashkey " << buf2hex(hashkey, TX_HASHKEY_BYTES) << " tx_commitnum " << tx_commitnum;

		return -1;
	}

	if (TRACE_DB_WRITE) BOOST_LOG_TRIVIAL(debug) << "DbConn::BilletSpendInsert inserted tx " << id << " bill " << bill_id << " hashkey " << buf2hex(hashkey, TX_HASHKEY_BYTES) << " tx_commitnum " << tx_commitnum;

	return 0;
}

int DbConn::BilletSpendSelect(sqlite3_stmt *select, uint64_t &tx_id, uint64_t &bill_id, bigint_t *hashkey, uint64_t *tx_commitnum, bool expect_row, uint64_t required_id)
{
	int rc;

	if (hashkey)
		hashkey->clear();
	if (tx_commitnum)
		*tx_commitnum = 0;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::BilletSpendSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSpendSelect returned SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSpendSelect returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSpendSelect returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSpendSelect returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	// SpendTx, Billet, Hashkey, TxCommitnum
	uint64_t _tx_id = sqlite3_column_int64(select, 0);
	uint64_t _bill_id = sqlite3_column_int64(select, 1);

	if (required_id && required_id != _bill_id)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSpendSelect result bill_id " << _bill_id << " != required_id " << required_id << " returning SQLITE_DONE";
		else if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSpendSelect result bill_id " << _bill_id << " != required_id " << required_id << " returning SQLITE_DONE";

		return 1;
	}

	auto hashkey_blob = sqlite3_column_blob(select, 2);
	auto hashkey_size = sqlite3_column_bytes(select, 2);
	uint64_t _tx_commitnum = sqlite3_column_int64(select, 3);

	if (!hashkey_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::BilletSpendSelect hashkey is null";

		return -1;
	}

	if (hashkey_size != TX_HASHKEY_BYTES)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::BilletSpendSelect returned hashkey size " << hashkey_size << " != " << TX_HASHKEY_BYTES;

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::BilletSpendSelect simulating database error post-error check";

		return -1;
	}

	tx_id = _tx_id;
	bill_id = _bill_id;
	if (hashkey)
		memcpy(hashkey, hashkey_blob, TX_HASHKEY_BYTES);
	if (tx_commitnum)
		*tx_commitnum = _tx_commitnum;

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(debug) << "DbConn::BilletSpendSelect returning tx_id " << tx_id << " bill_id " << bill_id << " hashkey " << buf2hex(hashkey_blob, TX_HASHKEY_BYTES) << " tx_commitnum " << _tx_commitnum;

	return 0;
}

int DbConn::BilletSpendSelectBillet(uint64_t bill_id, uint64_t &tx_id, bigint_t *hashkey, uint64_t *tx_commitnum)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DB_READ) BOOST_LOG_TRIVIAL(trace) << "DbConn::BilletSpendSelectBillet bill_id " << bill_id << " tx_id " << tx_id;

	// Billet, SpendTx >=
	if (dblog(sqlite3_bind_int64(Billet_Spends_select_billet, 1, bill_id))) return -1;
	if (dblog(sqlite3_bind_int64(Billet_Spends_select_billet, 2, tx_id))) return -1;

	auto _tx_id = tx_id;
	uint64_t _bill_id = 0;

	auto rc = BilletSpendSelect(Billet_Spends_select_billet, tx_id, _bill_id, hashkey, tx_commitnum, false, bill_id);

	if (!rc)
	{
		CCASSERT(_bill_id == bill_id);
		CCASSERT(tx_id >= _tx_id);
	}

	return rc;
}
