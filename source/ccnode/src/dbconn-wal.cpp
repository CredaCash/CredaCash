/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dbconn-wal.cpp
*/

#include "CCdef.h"
#include "dbconn.hpp"
#include "block.hpp"
#include "blockchain.hpp"

#include <dblog.h>
#include <CCobjects.hpp>
#include <Finally.hpp>
#include <CCutil.h>

#define TRACE_DBCONN	(g_params.trace_wal_db)

static const uint32_t g_full_checkpoint_time = 20;	// !!! make this configurable?

//#define TEST_FREERUN_CHECKPOINTS	1	// for testing

#ifndef TEST_FREERUN_CHECKPOINTS
#define TEST_FREERUN_CHECKPOINTS	0	// don't test
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
		uint32_t dt = time(NULL) - last_full_checkpoint_time;

		if (dt >= g_full_checkpoint_time)
			full_checkpoint_pending = true;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalStartCheckpoint " << dbname << " calling notify_one full " << full_checkpoint_pending;

	lock_guard<mutex> lock(checkpoint_mutex);

	do_full_checkpoint.store(full_checkpoint_pending);

	full_checkpoint_pending = false;

	checkpoint_needed.store(true);

	checkpoint_condition_variable.notify_one();	// need to hold lock so notify isn't missed by a thread that is just about to enter wait
}

void WalDB::WalWaitForStartCheckpoint()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalWaitForStartCheckpoint " << dbname;

	if (TEST_FREERUN_CHECKPOINTS)
	{
		usleep(1000000 + rand()*(1000000.0/RAND_MAX));

		return;
	}

	unique_lock<mutex> lock(checkpoint_mutex);

	while (!checkpoint_needed.load() && !stop_checkpointing.load() && !g_blockchain.HasFatalError() && !g_shutdown)
	{
		checkpoint_condition_variable.wait(lock);		// lock is acquired before waking up
	}
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

		last_full_checkpoint_time = time(NULL);

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpoint " << dbname << " releasing mutex";
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpoint " << dbname << " done";
}

void WalDB::WalWaitForFullCheckpoint()
{
	if (!checkpoint_needed.load() || !do_full_checkpoint.load())
		return;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalWaitForFullCheckpoint " << dbname;

	uint32_t t0 = time(NULL);

	while (!g_shutdown)
	{
		{
			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalWaitForFullCheckpoint " << dbname << " acquiring mutex...";

			lock_guard<mutex> lock(Wal_db_mutex);	// if checkpoint is started, will wait until checkpoint is done

			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalWaitForFullCheckpoint " << dbname << " mutex acquired and released";
		}

		if (!checkpoint_needed.load())
			return;

		usleep(10*1000);

		uint32_t dt = time(NULL) - t0;

		if (dt > 5*60 && dt > 3 * g_full_checkpoint_time)
		{
			BOOST_LOG_TRIVIAL(error) << "WalDB::WalWaitForFullCheckpoint " << dbname << " checkpointing thread appears to be hung or crashed";

			t0 = time(NULL);
		}
	}
}

void WalDB::WalCheckpointThreadProc(sqlite3 *db)
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpointThreadProc " << dbname << " start";

	while (!stop_checkpointing.load() && !g_blockchain.HasFatalError() && !g_shutdown)
	{
		WalWaitForStartCheckpoint();

		WalCheckpoint(db);
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "WalDB::WalCheckpointThreadProc " << dbname << " end";
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

