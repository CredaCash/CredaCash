/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * walletdb-accounts.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"
#include "accounts.hpp"

#include <dblog.h>

#define TRACE_DBCONN	(g_params.trace_db)

int DbConn::AccountInsert(Account& account, bool lock_optional)
{
	CCASSERT(TestDebugWriteLocking(lock_optional));

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::AccountInsert " << account.DebugString();

	sqlite3_stmt *insert_update = NULL;

	// Id, Name
	if (account.id)
	{
		insert_update = Accounts_update;

		if (dblog(sqlite3_bind_int64(insert_update, 1, account.id))) return -1;
	}
	else
	{
		insert_update = Accounts_insert;

		if (dblog(sqlite3_bind_null(insert_update, 1))) return -1;
	}
	if (account.name[0])
	{
		if (dblog(sqlite3_bind_text(insert_update, 2, account.name, -1, SQLITE_STATIC))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(insert_update, 2))) return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::AccountInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(insert_update);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::AccountInsert constraint violation " << account.DebugString();

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::AccountInsert sqlite3_changes " << changes << " after insert " << account.DebugString();

		return -1;
	}

	if (!account.id)
		account.id = sqlite3_last_insert_rowid(Wallet_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::AccountInsert inserted " << account.DebugString();

	return 0;
}

int DbConn::AccountSelect(sqlite3_stmt *select, Account& account, bool expect_row, uint64_t required_id)
{
	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::AccountSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::AccountSelect select returned SQLITE_DONE";
		else if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::AccountSelect select returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::AccountSelect select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != 2)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::AccountSelect select returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	// Id, Name
	uint64_t id = sqlite3_column_int64(select, 0);

	if (required_id && required_id != id)
	{
		if (expect_row) BOOST_LOG_TRIVIAL(warning) << "DbConn::AccountSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";
		else if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::AccountSelect result id " << id << " != required_id " << required_id << " returning SQLITE_DONE";

		return 1;
	}

	auto name_text = sqlite3_column_text(select, 1);
	unsigned name_size = sqlite3_column_bytes(select, 1);

	if (name_size >= sizeof(account.name))
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::AccountSelect select returned name size " << name_size << " >= " << sizeof(account.name);

		name_size = sizeof(account.name) - 1;
	}

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::AccountSelect simulating database error post-error check";

		return -1;
	}

	account.id = id;
	memcpy(account.name, name_text, name_size);
	account.name[name_size] = 0;	// ensure null terminated

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::AccountSelect returning " << account.DebugString();

	return 0;
}

int DbConn::AccountSelectId(uint64_t id, Account& account, bool or_greater)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::AccountSelectId id " << id << " or_greater = " << or_greater;

	CCASSERT(id);

	account.Clear();

	// >= Id
	if (dblog(sqlite3_bind_int64(Accounts_select, 1, id))) return -1;

	return AccountSelect(Accounts_select, account, !or_greater, !or_greater * id);
}

int DbConn::AccountSelectName(const char *name, unsigned size, Account& account)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	account.Clear();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::AccountSelectName name " << name;

	// Name
	if (dblog(sqlite3_bind_text(Accounts_select_name, 1, name, -1, SQLITE_STATIC))) return -1;

	return AccountSelect(Accounts_select_name, account);
}
