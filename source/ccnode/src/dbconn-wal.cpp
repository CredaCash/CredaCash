/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * dbconn-wal.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"
#include "block.hpp"
#include "blockchain.hpp"

#include <dblog.h>
#include <CCobjects.hpp>

#define TRACE_DBCONN	(g_params.trace_wal_db)

//!#define TEST_FREERUN_CHECKPOINTS		1
//!#define RTEST_CUZZ_WAIT				(1024-1)

#ifndef TEST_FREERUN_CHECKPOINTS
#define TEST_FREERUN_CHECKPOINTS		0	// don't test
#endif

#ifndef RTEST_CUZZ_WAIT
#define RTEST_CUZZ_WAIT					0	// don't test
#endif

void WalDB::WalStartCheckpoint(bool full)
{
	full_checkpoint_pending |= full;

	auto needed = checkpoint_needed.load();

	if (needed)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalStartCheckpoint " << dbname << " already flagged";

		return;
	}

	// must set do_full_checkpoint before checkpoint_needed

	if (!full_checkpoint_pending)
	{
		int32_t dt = unixtime() - last_full_checkpoint_time;

		if (dt >= g_params.db_checkpoint_sec)
			full_checkpoint_pending = true;
		else
			return;	// might work better when db is saved on an sdcard
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalStartCheckpoint " << dbname << " calling notify_one full " << full_checkpoint_pending;

	{
		lock_guard<mutex> lock(checkpoint_mutex);

		do_full_checkpoint.store(full_checkpoint_pending);

		full_checkpoint_pending = false;

		checkpoint_needed.store(true);
	}

	checkpoint_condition_variable.notify_one();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalStartCheckpoint " << dbname << " done";
}

void WalDB::WalWaitForStartCheckpoint()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalWaitForStartCheckpoint " << dbname;

	if (TEST_FREERUN_CHECKPOINTS)
	{
		sleep(1);
		usleep(rand() & (1024*1024-1));

		return;
	}

	unique_lock<mutex> lock(checkpoint_mutex);

	while (!checkpoint_needed.load() && !stop_checkpointing.load() && !g_blockchain.HasFatalError() && !g_shutdown)
	{
		if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalWaitForStartCheckpoint starting wait " << dbname;

		checkpoint_condition_variable.wait(lock);		// lock is acquired before waking up
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalWaitForStartCheckpoint done " << dbname;
}

void WalDB::WalCheckpoint(sqlite3 *db)
{
	if (g_shutdown)
		return;

	if (stop_checkpointing.load())
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpoint " << dbname << " stop_checkpointing is set";

		sleep(1);

		checkpoint_needed.store(false);

		return;
	}

	if (!do_full_checkpoint.load())
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "WalDB::WalCheckpoint " << dbname << " passive";

		dblog(sqlite3_wal_checkpoint_v2(db, NULL, SQLITE_CHECKPOINT_PASSIVE, NULL, NULL));	// note: this can return SQLITE_BUSY if sqlite3_busy_timeout is enabled

		checkpoint_needed.store(false);
	}
	else
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpoint " << dbname << " acquiring mutex...";

		lock_guard<mutex> lock(Wal_db_mutex);

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpoint " << dbname << " mutex acquired";

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "WalDB::WalCheckpoint " << dbname << " truncate";

		dblog(sqlite3_wal_checkpoint_v2(db, NULL, SQLITE_CHECKPOINT_TRUNCATE, NULL, NULL));	// note: this can return SQLITE_BUSY if sqlite3_busy_timeout is enabled

		checkpoint_needed.store(false);

		last_full_checkpoint_time = unixtime();

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpoint " << dbname << " releasing mutex";
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpoint " << dbname << " done";
}

void WalDB::WalCheckpointThreadProc(sqlite3 *db)
{
	BOOST_LOG_TRIVIAL(info) << "WalDB::WalCheckpointThreadProc " << dbname << " start";

	while (!stop_checkpointing.load() && !g_blockchain.HasFatalError() && !g_shutdown)
	{
		WalWaitForStartCheckpoint();

		WalCheckpoint(db);
	}

	BOOST_LOG_TRIVIAL(info) << "WalDB::WalCheckpointThreadProc " << dbname << " end";
}

void WalDB::WalStartCheckpointing(sqlite3 *db)
{
	m_thread = new thread(&WalDB::WalCheckpointThreadProc, this, db);
	CCASSERT(m_thread);
}

void WalDB::WalStopCheckpointing()
{
	if (m_thread)
	{
		{
			lock_guard<mutex> lock(checkpoint_mutex);

			stop_checkpointing.store(true);

			checkpoint_condition_variable.notify_all();
		}

		m_thread->join();

		delete m_thread;

		m_thread = NULL;
	}
}

