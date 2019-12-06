/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * walletdb-totals.cpp
*/

#include "ccwallet.h"
#include "walletdb.hpp"
#include "totals.hpp"

#include <dblog.h>
#include <CCparams.h>

#define TRACE_DBCONN	(g_params.trace_db)

using namespace snarkfront;

int DbConn::TotalInsert(const Total& total, bool lock_optional)
{
	CCASSERT(TestDebugWriteLocking(lock_optional));

	//lock_guard<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TotalInsert " << total.DebugString();

	CCASSERT(total.IsValid());

	bigint_t packed_total;
	amount_to_bigint(total.total, packed_total);

	//packed_total = 0UL; // for testing

	// Type, Reference, Asset, DelayTime, Blockchain, Total
	if (dblog(sqlite3_bind_int(Totals_insert, 1, total.type))) return -1;
	if (dblog(sqlite3_bind_int64(Totals_insert, 2, total.reference))) return -1;
	if (dblog(sqlite3_bind_int64(Totals_insert, 3, total.asset))) return -1;
	if (dblog(sqlite3_bind_int(Totals_insert, 4, total.delaytime))) return -1;
	if (dblog(sqlite3_bind_int64(Totals_insert, 5, total.blockchain))) return -1;
	if (dblog(sqlite3_bind_blob(Totals_insert, 6, &packed_total, bigint_bytes_in_use(packed_total), SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::TotalInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Totals_insert);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConn::TotalInsert constraint violation " << total.DebugString();

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Wallet_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TotalInsert sqlite3_changes " << changes << " after insert " << total.DebugString();

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConn::TotalInsert inserted " << total.DebugString();

	return 0;
}

int DbConn::TotalSelect(sqlite3_stmt *select, Total& total)
{
	int rc;

	bigint_t packed_total;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::TotalSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		//BOOST_LOG_TRIVIAL(trace) << "DbConn::TotalSelect select returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TotalSelect select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != 6)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TotalSelect select returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	// Type, Reference, Asset, DelayTime, Blockchain, Total
	unsigned type = sqlite3_column_int(select, 0);
	uint64_t reference = sqlite3_column_int64(select, 1);
	uint64_t asset = sqlite3_column_int64(select, 2);
	unsigned delaytime = sqlite3_column_int(select, 3);
	uint64_t blockchain = sqlite3_column_int64(select, 4);
	auto total_blob = sqlite3_column_blob(select, 5);
	unsigned total_size = sqlite3_column_bytes(select, 5);

	if (!Total::TypeIsValid(type))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TotalSelect select returned invalid type " << type;

		return -1;
	}

	if (total_size > sizeof(packed_total))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::TotalSelect select returned total size " << total_size << " > " << sizeof(packed_total);

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Wallet_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConn::TotalSelect simulating database error post-error check";

		return -1;
	}

	total.type = type;
	total.reference = reference;
	total.asset = asset;
	total.delaytime = delaytime;
	total.blockchain = blockchain;
	packed_total = 0UL;
	if (total_blob)
		memcpy((void*)&packed_total, total_blob, total_size);
	amount_from_bigint(packed_total, total.total);

	if (total.total && TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TotalSelect returning " << total.DebugString();

	return 0;
}

int DbConn::TotalSelectMatch(bool exact, Total& total)
{
	//boost::shared_lock<boost::shared_mutex> lock(db_mutex);
	Finally finally(boost::bind(&DbConn::DoDbFinish, this));

	total.total = 0UL;

	if (0 && TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TotalSelectMatch exact " << exact << " " << total.DebugString();

	auto match_total = total;

	// Type, Reference, Asset, DelayTime, >= Blockchain
	if (dblog(sqlite3_bind_int(Totals_select, 1, total.type))) return -1;
	if (dblog(sqlite3_bind_int64(Totals_select, 2, total.reference))) return -1;
	if (dblog(sqlite3_bind_int64(Totals_select, 3, total.asset))) return -1;
	if (dblog(sqlite3_bind_int(Totals_select, 4, total.delaytime))) return -1;
	if (dblog(sqlite3_bind_int64(Totals_select, 5, total.blockchain))) return -1;

	auto rc = TotalSelect(Totals_select, total);

	if (exact && !rc &&
		(	total.type != match_total.type
		 ||	total.reference != match_total.reference
		 ||	total.asset != match_total.asset
		 ||	total.delaytime != match_total.delaytime
		 ||	total.blockchain != match_total.blockchain
		))
	{
			total.total = 0UL;
			rc = 1;
	}

	if (rc < 0) BOOST_LOG_TRIVIAL(error) << "DbConn::TotalSelectMatch exact " << exact << " returning " << rc << " " << total.DebugString();
	else if (!rc && TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConn::TotalSelectMatch exact " << exact << " returning " << rc << " " << total.DebugString();

	return rc;
}
