/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * walletdb-parameters.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"

#include <dblog.h>

#define TRACE_DBCONN	(g_params.trace_db)

int DbConn::ParameterInsert(int key, int subkey, void *value, unsigned valsize, bool lock_optional)
{
	CCASSERT(TestDebugWriteLocking(lock_optional));

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::ParameterInsert key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

	// Key, Subkey, Value
	if (dblog(sqlite3_bind_int(Parameters_insert, 1, key))) return -1;
	if (dblog(sqlite3_bind_int(Parameters_insert, 2, subkey))) return -1;
	if (dblog(sqlite3_bind_blob(Parameters_insert, 3, value, valsize, SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ParameterInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Parameters_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ParameterInsert sqlite3_changes " << changes << " after insert key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::ParameterInsert inserted key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

	return 0;
}

int DbConn::ParameterSelect(int key, int subkey, void *value, unsigned bufsize, bool add_terminator, unsigned *retsize)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::ParameterSelect key " << key << " subkey " << subkey << " bufsize " << bufsize << " add_terminator " << add_terminator;

	memset(value, 0, bufsize);
	if (retsize)
		*retsize = 0;

	int rc;

	// Key, Subkey
	if (dblog(sqlite3_bind_int(Parameters_select, 1, key))) return -1;
	if (dblog(sqlite3_bind_int(Parameters_select, 2, subkey))) return -1;

	if (dblog(rc = sqlite3_step(Parameters_select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ParameterSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::ParameterSelect select " << key << " subkey " << subkey << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ParameterSelect select " << key << " subkey " << subkey << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Parameters_select) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ParameterSelect select returned " << sqlite3_data_count(Parameters_select) << " columns";

		return -1;
	}

	// Value
	auto data_blob = sqlite3_column_blob(Parameters_select, 0);
	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ParameterSelect Data is null";

		return -1;
	}

	unsigned datasize = sqlite3_column_bytes(Parameters_select, 0);

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::ParameterSelect simulating database error post-error check";

		return -1;
	}

	if (datasize + add_terminator > bufsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ParameterSelect key " << key << " subkey " << subkey << " data size " << datasize << " > " << bufsize;

		return -1;
	}

	if (!add_terminator && !retsize && datasize != bufsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::ParameterSelect key " << key << " subkey " << subkey << " data size " << datasize << " != " << bufsize;

		return -1;
	}

	memcpy(value, data_blob, datasize);		// terminator added by initial memset

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::ParameterSelect key " << key << " subkey " << subkey << " returning obj size " << datasize << " value " << buf2hex(value, (datasize < 16 ? datasize : 16));

	if (retsize)
		*retsize = datasize;

	return 0;
}
