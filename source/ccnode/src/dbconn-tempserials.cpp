/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * dbconn-tempserials.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"

#include <dblog.h>

#define TRACE_DBCONN	(g_params.trace_pending_serialnum_db)

static mutex Temp_Serials_db_mutex;	// to avoid inconsistency problems with shared cache

DbConnTempSerials::DbConnTempSerials()
{
	ClearDbPointers();

	lock_guard<mutex> lock(Temp_Serials_db_mutex);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::DbConnTempSerials dbconn " << (uintptr_t)this;

	OpenDb();

	CCASSERTZ(dblog(sqlite3_prepare_v2(Temp_Serials_db, "insert into Temp_Serials (Serialnum, Blockp) values (?1, ?2);", -1, &Temp_Serials_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Temp_Serials_db, "select Blockp from Temp_Serials where Serialnum = ?1 and Blockp > ?2 order by Blockp;", -1, &Temp_Serials_select, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Temp_Serials_db, "update Temp_Serials set Blockp = ?1, Level = ?2 where Blockp = ?3 and Level = 0;", -1, &Temp_Serials_update, NULL)));
	//CCASSERTZ(dblog(sqlite3_prepare_v2(Temp_Serials_db, "delete from Temp_Serials where Serialnum = ?1 and Blockp = ?2;", -1, &Temp_Serials_delete, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Temp_Serials_db, "delete from Temp_Serials where Blockp = ?1 and Level = 0;", -1, &Temp_Serials_clear, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Temp_Serials_db, "delete from Temp_Serials where Level > 0 and Level < ?1;", -1, &Temp_Serials_prune, NULL)));

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::DbConnTempSerials dbconn done " << (uintptr_t)this;
}

DbConnTempSerials::~DbConnTempSerials()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::~DbConnTempSerials dbconn " << (uintptr_t)this;

	static bool explain = TEST_EXPLAIN_DB_QUERIES;

#if TEST_EXPLAIN_DB_QUERIES
	unique_lock<mutex> elock(g_db_explain_lock);

	if (!explain)
		elock.unlock();

	lock_guard<mutex> lock(Temp_Serials_db_mutex);
#endif

	//if (explain)
	//	CCASSERTZ(dbexec(Temp_Serials_db, "analyze;"));

	DbFinalize(Temp_Serials_insert, explain);
	DbFinalize(Temp_Serials_select, explain);
	DbFinalize(Temp_Serials_update, explain);
	//DbFinalize(Temp_Serials_delete, explain);
	DbFinalize(Temp_Serials_clear, explain);
	DbFinalize(Temp_Serials_prune, explain);

	explain = false;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::~DbConnTempSerials done dbconn " << (uintptr_t)this;
}

void DbConnTempSerials::DoTempSerialsFinish()
{
	if (RandTest(RTEST_DELAY_DB_RESET)) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::DoTempSerialsFinish dbconn " << uintptr_t(this);

	sqlite3_reset(Temp_Serials_insert);
	sqlite3_reset(Temp_Serials_select);
	sqlite3_reset(Temp_Serials_update);
	//sqlite3_reset(Temp_Serials_delete);
	sqlite3_reset(Temp_Serials_clear);
	sqlite3_reset(Temp_Serials_prune);
}

int DbConnTempSerials::TempSerialnumInsert(const void *serialnum, unsigned serialnum_size, const void* blockp)
{
	lock_guard<mutex> lock(Temp_Serials_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnTempSerials::DoTempSerialsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::TempSerialnumInsert serialnum " << buf2hex(serialnum, serialnum_size) << " blockp " << (uintptr_t)blockp;

	// Serialnum, Blockp
	if (dblog(sqlite3_bind_blob(Temp_Serials_insert, 1, serialnum, serialnum_size, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Temp_Serials_insert, 2, &blockp, sizeof(blockp), SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnTempSerials::TempSerialnumInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Temp_Serials_insert);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnTempSerials::TempSerialnumInsert failed; already in database blockp " << (uintptr_t)blockp << " serialnum " << buf2hex(serialnum, serialnum_size);

		return 1;
	}

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Temp_Serials_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnTempSerials::TempSerialnumInsert sqlite3_changes " << changes << " after insert serialnum " << buf2hex(serialnum, serialnum_size) << " blockp " << (uintptr_t)blockp;

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnTempSerials::TempSerialnumInsert inserted serialnum " << buf2hex(serialnum, serialnum_size) << " blockp " << (uintptr_t)blockp;

	return 0;
}

int DbConnTempSerials::TempSerialnumUpdate(const void* old_blockp, const void* new_blockp, uint64_t level)
{
	lock_guard<mutex> lock(Temp_Serials_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnTempSerials::DoTempSerialsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::TempSerialnumUpdate old blockp " << (uintptr_t)old_blockp << " new blockp " << (uintptr_t)new_blockp << " level " << level;

	// Blockp, Level, Old Blockp
	if (dblog(sqlite3_bind_blob(Temp_Serials_update, 1, &new_blockp, sizeof(new_blockp), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Temp_Serials_update, 2, level))) return -1;
	if (dblog(sqlite3_bind_blob(Temp_Serials_update, 3, &old_blockp, sizeof(old_blockp), SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnTempSerials::TempSerialnumUpdate simulating database error pre-update";

		return -1;
	}

	auto rc = sqlite3_step(Temp_Serials_update);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Temp_Serials_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnTempSerials::TempSerialnumUpdate changes " << changes << " after update old blockp " << (uintptr_t)old_blockp << " new blockp " << (uintptr_t)new_blockp << " level " << level;

	return 0;
}

int DbConnTempSerials::TempSerialnumSelect(const void *serialnum, unsigned serialnum_size, const void* last_blockp, void *output[], unsigned bufsize)
{
	lock_guard<mutex> lock(Temp_Serials_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnTempSerials::DoTempSerialsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::TempSerialnumSelect serialnum " << buf2hex(serialnum, serialnum_size) << " last blockp " << (uintptr_t)last_blockp;

	// Serialnum, last Blockp
	if (dblog(sqlite3_bind_blob(Temp_Serials_select, 1, serialnum, serialnum_size, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Temp_Serials_select, 2, &last_blockp, sizeof(last_blockp), SQLITE_STATIC))) return -1;

	unsigned bufpos = 0;

	while (true)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Temp_Serials_select), DB_STMT_SELECT)) return -1;

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnTempSerials::TempSerialnumSelect simulating database error post-select";

			return -1;
		}

		if (dbresult(rc) == SQLITE_DONE)
			break;

		if (bufpos == bufsize)
		{
			bufpos++;	// we have more
			break;
		}

		if (dbresult(rc) != SQLITE_ROW)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnTempSerials::TempSerialnumSelect select returned " << rc;

			return -1;
		}

		if (sqlite3_data_count(Temp_Serials_select) != 1)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnTempSerials::TempSerialnumSelect select returned " << sqlite3_data_count(Temp_Serials_select) << " columns";

			break;
		}

		// Blockp
		auto bufp_blob = sqlite3_column_blob(Temp_Serials_select, 0);

		if (sqlite3_column_bytes(Temp_Serials_select, 0) != sizeof(void*))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnTempSerials::TempSerialnumSelect Bufp serialnum_size " << sqlite3_column_bytes(Temp_Serials_select, 0) << " != " << sizeof(void*);

			return -1;
		}

		auto bufp = *(void**)bufp_blob;

		if (!bufp)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnTempSerials::TempSerialnumSelect bufp is null";

			return -1;
		}

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::TempSerialnumSelect bufp " << (uintptr_t)bufp;

		if (dblog(sqlite3_extended_errcode(Temp_Serials_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnTempSerials::TempSerialnumSelect simulating database error post-error check";

			return -1;
		}

		output[bufpos++] = bufp;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::TempSerialnumSelect returning " << bufpos << " entries";

	return bufpos;
}

#if 0	// not used
int DbConnTempSerials::TempSerialnumDelete(const void *serialnum, unsigned serialnum_size, const void* blockp)
{
	lock_guard<mutex> lock(Temp_Serials_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnTempSerials::DoTempSerialsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::TempSerialnumDelete serialnum " << buf2hex(serialnum, serialnum_size) << " blockp " << (uintptr_t)blockp;

	// Serialnum, Blockp
	if (dblog(sqlite3_bind_blob(Temp_Serials_delete, 1, serialnum, serialnum_size, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Temp_Serials_delete, 2, &blockp, sizeof(blockp), SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnTempSerials::TempSerialnumDelete simulating database error pre-delete";

		return -1;
	}

	auto rc = sqlite3_step(Temp_Serials_delete);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Temp_Serials_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnTempSerials::TempSerialnumDelete sqlite3_changes " << changes << " after delete serialnum " << buf2hex(serialnum, serialnum_size) << " blockp " << (uintptr_t)blockp;

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnTempSerials::TempSerialnumInsert deleted serialnum " << buf2hex(serialnum, serialnum_size) << " blockp " << (uintptr_t)blockp;

	return 0;
}
#endif

int DbConnTempSerials::TempSerialnumClear(const void* blockp)
{
	lock_guard<mutex> lock(Temp_Serials_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnTempSerials::DoTempSerialsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::TempSerialnumClear blockp " << (uintptr_t)blockp;

	// Blockp
	if (dblog(sqlite3_bind_blob(Temp_Serials_clear, 1, &blockp, sizeof(blockp), SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnTempSerials::TempSerialnumClear simulating database error pre-delete";

		return -1;
	}

	auto rc = sqlite3_step(Temp_Serials_clear);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Temp_Serials_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnTempSerials::TempSerialnumClear changes " << changes << " after delete blockp " << (uintptr_t)blockp;

	return 0;
}

// note: make sure blockp stays valid until it is pruned

int DbConnTempSerials::TempSerialnumPruneLevel(uint64_t level)
{
	lock_guard<mutex> lock(Temp_Serials_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnTempSerials::DoTempSerialsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnTempSerials::TempSerialnumPrune level " << level;

	// Level
	if (dblog(sqlite3_bind_int64(Temp_Serials_prune, 1, level))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnTempSerials::TempSerialnumPrune simulating database error pre-delete";

		return -1;
	}

	auto rc = sqlite3_step(Temp_Serials_prune);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Temp_Serials_db);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnTempSerials::TempSerialnumPrune changes " << changes << " after prune level " << level;

	return 0;
}
