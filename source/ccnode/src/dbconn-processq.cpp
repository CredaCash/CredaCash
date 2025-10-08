/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * dbconn-processq.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"

#include <dblog.h>
#include <CCobjects.hpp>
#include <ccserver/connection_registry.hpp>

//!#define RTEST_CUZZ_WAIT				(1024-1)

#ifndef RTEST_CUZZ_WAIT
#define RTEST_CUZZ_WAIT					0	// don't test
#endif

#define TRACE_DBCONN	(g_params.trace_validation_q_db)
#define TRACE_PROCESS	(g_params.trace_block_validation || g_params.trace_tx_validation || g_params.trace_xreq_processing)

static array<atomic<int>, PROCESS_Q_N>			queued_work;
static array<mutex, PROCESS_Q_N>				work_queue_mutex;
static array<condition_variable, PROCESS_Q_N>	work_queue_condition_variable;

static mutex Process_Q_db_mutex[PROCESS_Q_N];	// to avoid inconsistency problems with shared cache

DbConnProcessQ::DbConnProcessQ()
{
	ClearDbPointers();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::DbConnProcessQ dbconn " << (uintptr_t)this;

	for (unsigned i = 0; i < PROCESS_Q_N; ++i)
	{
		lock_guard<mutex> lock(Process_Q_db_mutex[i]);

		//if (queued_work[i].load())
		//	BOOST_LOG_TRIVIAL(warning) << "DbConnProcessQ::DbConnProcessQ type " << i << " queued work " << queued_work[i].load();

		OpenDb(i);

		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "insert into Process_Q (ObjId, PriorOid, Level, Status, Priority, Seqnum, AuxInt, ConnId, CallbackId, Bufp) values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);", -1, &Process_Q_insert[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Priority = min(Priority, ?2), Seqnum = max(Seqnum, ?3), AuxInt = AuxInt + ?4, ConnId = max(ConnId, ?5), CallbackId = case when ConnId > ?5 then CallbackId else ?6 end where ObjId = ?1;", -1, &Process_Q_update_queue[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "select AuxInt, ConnId, CallbackId, Bufp from Process_Q where ObjId = ?1;", -1, &Process_Q_select[i], NULL)));
		// The follow select is used in ProcessQGetNextValidateObj to select the next object for processing
		// It selects highest Level first to give max chance of the blockchain advancing
		// ok to use offset in this select when witness is selecting valid blocks because between calls (a) no one will change sort keys; (b) no one will delete an entry (b) if an entry is added, it's ok to get same entry twice
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "select Level, ConnId, CallbackId, Bufp from Process_Q where Status = ?1 order by Priority, Level desc, Seqnum limit 1 offset ?2;", -1, &Process_Q_select_next[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Status = ?2, AuxInt = ifnull(?3, AuxInt) where ObjId = ?1;", -1, &Process_Q_update[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Status = " STRINGIFY(PROCESS_Q_STATUS_PENDING) " where Status = " STRINGIFY(PROCESS_Q_STATUS_HOLD) " and PriorOid = ?1;", -1, &Process_Q_update_priorobj[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set AuxInt = 0 where Status = " STRINGIFY(PROCESS_Q_STATUS_VALID) ";", -1, &Process_Q_clear[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "select count(*) from Process_Q where Status = " STRINGIFY(PROCESS_Q_STATUS_VALID) " and AuxInt = ?1;", -1, &Process_Q_count[i], NULL)));	// used for testing
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Priority = random() where Status = " STRINGIFY(PROCESS_Q_STATUS_VALID) ";", -1, &Process_Q_randomize[i], NULL)));		// used for testing
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Status = Status | " STRINGIFY(PROCESS_Q_STATUS_DONE_FLAG) " where Level < ?1;", -1, &Process_Q_done[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "select ObjId, Bufp, Level, Status, ConnId, CallbackId from Process_Q where Level < ?1 limit 1;", -1, &Process_Q_select_level[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "delete from Process_Q where ObjId = ?1;", -1, &Process_Q_delete[i], NULL)));
	}

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::DbConnProcessQ done dbconn " << (uintptr_t)this;
}

DbConnProcessQ::~DbConnProcessQ()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::~DbConnProcessQ dbconn " << (uintptr_t)this;

	static bool explain = TEST_EXPLAIN_DB_QUERIES;

#if TEST_EXPLAIN_DB_QUERIES
	unique_lock<mutex> elock(g_db_explain_lock);

	if (!explain)
		elock.unlock();
#endif

	for (unsigned i = 0; i < PROCESS_Q_N; ++i)
	{
#if TEST_EXPLAIN_DB_QUERIES
		lock_guard<mutex> lock(Process_Q_db_mutex[i]);
#endif

		//if (explain)
		//	CCASSERTZ(dbexec(Process_Q_db[i], "analyze;"));

		DbFinalize(Process_Q_insert[i], explain);
		DbFinalize(Process_Q_update_queue[i], explain);
		DbFinalize(Process_Q_select[i], explain);
		DbFinalize(Process_Q_select_next[i], explain);
		DbFinalize(Process_Q_update[i], explain);
		DbFinalize(Process_Q_update_priorobj[i], explain);
		DbFinalize(Process_Q_clear[i], explain);
		DbFinalize(Process_Q_count[i], explain);
		DbFinalize(Process_Q_randomize[i], explain);
		DbFinalize(Process_Q_done[i], explain);
		DbFinalize(Process_Q_select_level[i], explain);
		DbFinalize(Process_Q_delete[i], explain);

		explain = false;	// if the queue types have different queries, then comment out this line
	}

	explain = false;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::~DbConnProcessQ done dbconn " << (uintptr_t)this;
}

void DbConnProcessQ::DoProcessQFinish(unsigned type)
{
	if (RandTest(RTEST_DELAY_DB_RESET)) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::DoProcessQFinish dbconn " << uintptr_t(this) << " type " << type;

	sqlite3_reset(Process_Q_insert[type]);
	sqlite3_reset(Process_Q_update_queue[type]);
	sqlite3_reset(Process_Q_select[type]);
	sqlite3_reset(Process_Q_select_next[type]);
	sqlite3_reset(Process_Q_update[type]);
	sqlite3_reset(Process_Q_update_priorobj[type]);
	sqlite3_reset(Process_Q_clear[type]);
	sqlite3_reset(Process_Q_count[type]);
	sqlite3_reset(Process_Q_randomize[type]);
	sqlite3_reset(Process_Q_done[type]);
	sqlite3_reset(Process_Q_select_level[type]);
	sqlite3_reset(Process_Q_delete[type]);
}

void DbConnProcessQ::IncrementQueuedWork(unsigned type, unsigned changes)
{
	CCASSERT(type < PROCESS_Q_N);

	lock_guard<mutex> lock(work_queue_mutex[type]);

	auto prior_work = queued_work[type].fetch_add(changes);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::IncrementQueuedWork type " << type << " changes " << changes << " pre-increment work " << prior_work;

	if (prior_work <= 0)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::IncrementQueuedWork calling notify_one/notify_all type " << type;

		for (unsigned i = 0; i < changes; ++i)
			work_queue_condition_variable[type].notify_one();
	}
}

void DbConnProcessQ::StopQueuedWork(unsigned type)
{
	CCASSERT(type < PROCESS_Q_N);

	lock_guard<mutex> lock(work_queue_mutex[type]);

	queued_work[type] = INT_MAX / 2;

	work_queue_condition_variable[type].notify_all();
}

void DbConnProcessQ::WaitForQueuedWork(unsigned type)
{
	CCASSERT(type < PROCESS_Q_N);

	if (queued_work[type].fetch_sub(1) > 0)
		return;

	queued_work[type].fetch_add(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::WaitForQueuedWork type " << type;

	unique_lock<mutex> lock(work_queue_mutex[type]);

	while (!g_shutdown)
	{
		if (queued_work[type].fetch_sub(1) > 0)
			return;

		queued_work[type].fetch_add(1);

		static array<bool, PROCESS_Q_N> timed_wake_scheduled;

		if (timed_wake_scheduled[type])
		{
			if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

			work_queue_condition_variable[type].wait(lock);		// lock is acquired before waking up
		}
		else
		{
			if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::WaitForQueuedWork type " << type << " timed wait";

			timed_wake_scheduled[type] = true;

			work_queue_condition_variable[type].wait_for(lock, chrono::seconds(2)); // lock is acquired before waking up

			timed_wake_scheduled[type] = false;

			if (queued_work[type].fetch_sub(1) > 0)
				return;

			queued_work[type].fetch_add(1);

			return;		// check for work now regardless of queued_work status
		}
	}
}

int DbConnProcessQ::ProcessQEnqueueValidate(unsigned type, SmartBuf smartobj, const ccoid_t *prior_oid, int64_t level, unsigned status, Process_Q_Priority priority, bool is_block_tx, unsigned conn_index, uint32_t callback_id)
{
	static int64_t sequence = 0;

	bool inserted = true;
	unsigned changes = 0;

	{

	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	auto seqnum = sequence++;

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQEnqueueValidate type " << type << " level " << level << " status " << status << " priority " << priority << " seqnum " << seqnum << " is_block_tx " << is_block_tx << " callback_id " << callback_id << " smartobj " << (uintptr_t)&smartobj;

	auto bufp = smartobj.BasePtr();
	auto obj = (CCObject*)smartobj.data();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQEnqueueValidate type " << type << " level " << level << " status " << status << " priority " << priority << " seqnum " << seqnum << " is_block_tx " << is_block_tx << " callback_id " << callback_id << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

	// ObjId, PriorOid, Level, Status, Priority, Seqnum, AuxInt, ConnId, CallbackId, Bufp
	if (dblog(sqlite3_bind_blob(Process_Q_insert[type], 1, obj->OidPtr(), sizeof(ccoid_t), SQLITE_STATIC))) return -1;
	if (type == PROCESS_Q_TYPE_BLOCK)
	{
		if (dblog(sqlite3_bind_blob(Process_Q_insert[type], 2, prior_oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;
		if (dblog(sqlite3_bind_int64(Process_Q_insert[type], 3, level))) return -1;
	}
	else
	{
		if (dblog(sqlite3_bind_null(Process_Q_insert[type], 2))) return -1;
		if (dblog(sqlite3_bind_null(Process_Q_insert[type], 3))) return -1;
	}
	if (dblog(sqlite3_bind_int(Process_Q_insert[type], 4, status))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_insert[type], 5, priority))) return -1;
	if (dblog(sqlite3_bind_int64(Process_Q_insert[type], 6, seqnum))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_insert[type], 7, is_block_tx))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_insert[type], 8, conn_index))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_insert[type], 9, callback_id))) return -1;
	if (dblog(sqlite3_bind_blob(Process_Q_insert[type], 10, &bufp, sizeof(bufp), SQLITE_STATIC))) return -1;

	auto rc = sqlite3_step(Process_Q_insert[type]);

	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		inserted = false;

		if (!is_block_tx)
		{
			BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQEnqueueValidate constraint violation; object downloaded more than once?";
		}
		else
		{
			//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQEnqueueValidate constraint violation";

			// ObjId, Priority, Seqnum, AuxInt, ConnId, CallbackId
			if (dblog(sqlite3_bind_blob(Process_Q_update_queue[type], 1, obj->OidPtr(), sizeof(ccoid_t), SQLITE_STATIC))) return -1;
			if (dblog(sqlite3_bind_int(Process_Q_update_queue[type], 2, priority))) return -1;
			if (dblog(sqlite3_bind_int64(Process_Q_update_queue[type], 3, seqnum))) return -1;
			if (dblog(sqlite3_bind_int(Process_Q_update_queue[type], 4, is_block_tx))) return -1;
			if (dblog(sqlite3_bind_int(Process_Q_update_queue[type], 5, conn_index))) return -1;
			if (dblog(sqlite3_bind_int(Process_Q_update_queue[type], 6, callback_id))) return -1;

			if (dblog(sqlite3_step(Process_Q_update_queue[type]), DB_STMT_STEP)) return -1;
		}
	}
	else if (dblog(rc, DB_STMT_STEP)) return -1;

	changes = sqlite3_changes(Process_Q_db[type]);

	if (changes && inserted)
	{
		if (TRACE_DBCONN || TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQEnqueueValidate inserted into Process_Q type " << type << " level " << level << " status " << status << " priority " << priority << " seqnum " << seqnum << " is_block_tx " << is_block_tx << " callback_id " << callback_id << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

		smartobj.IncRef();
	}

	if (changes != 1)
	{
		if (inserted)
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQEnqueueValidate sqlite3_changes " << changes << " after insert into Process_Q type " << type << " status " << status << " level " << level << " priority " << priority << " seqnum " << seqnum << " is_block_tx " << is_block_tx << " callback_id " << callback_id << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
		else
			BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQEnqueueValidate sqlite3_changes " << changes << " after update Process_Q type " << type << " status " << status << " level " << level << " priority " << priority << " seqnum " << seqnum << " is_block_tx " << is_block_tx << " callback_id " << callback_id << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
	}

	} // unlock db

	if (changes && inserted)
		IncrementQueuedWork(type, changes);

	return !inserted;
}

int DbConnProcessQ::ProcessQGetNextValidateObj(unsigned type, SmartBuf *retobj, unsigned& conn_index, uint32_t& callback_id)
{
	Finally finally2(boost::bind(&DbConnProcessQ::IncrementQueuedWork, type, 1));		// increment work on error

	{

	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidateObj type " << type;

	retobj->ClearRef();
	conn_index = 0;
	callback_id = 0;

	int result = 0;
	int rc;

	// SELECT an entry from Process_Q --> Priority, ObjId, Callback, Bufp

	// Status, offset
	if (dblog(sqlite3_bind_int(Process_Q_select_next[type], 1, PROCESS_Q_STATUS_PENDING))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_select_next[type], 2, 0))) return -1;

	if (dblog(rc = sqlite3_step(Process_Q_select_next[type]), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidateObj simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidateObj select returned SQLITE_DONE";

		finally2.Clear();	// don't increment work

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Process_Q_select_next[type]) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj select returned " << sqlite3_data_count(Process_Q_select_next[type]) << " columns";

		return -1;
	}

	// Level, ConnId, CallbackId, Bufp
	auto level = sqlite3_column_int64(Process_Q_select_next[type], 0);
	conn_index = sqlite3_column_int(Process_Q_select_next[type], 1);
	callback_id = sqlite3_column_int(Process_Q_select_next[type], 2);
	auto bufp_blob = sqlite3_column_blob(Process_Q_select_next[type], 3);

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidateObj simulating database error; setting bufp_blob = NULL";

		bufp_blob = NULL;
	}

	if (!bufp_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj bufp_blob is null";

		return -1;
	}

	if (sqlite3_column_bytes(Process_Q_select_next[type], 3) != sizeof(void*))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj Bufp size " << sqlite3_column_bytes(Process_Q_select_next[type], 3) << " != " << sizeof(void*);

		return -1;
	}

	void *bufp = *(void**)bufp_blob;

	if (!bufp)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj bufp is null";

		return -1;
	}

	SmartBuf smartobj(bufp);
	auto obj = (CCObject*)smartobj.data();
	CCASSERT(obj);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidateObj type " << type << " level " << level << " bufp " << (uintptr_t)bufp << " obj.oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

	if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidateObj simulating database error post-error check";

		return -1;
	}

	// UPDATE to status = PROCESS_Q_STATUS_HOLD

	// ObjId, Status, AuxInt
	if (dblog(sqlite3_bind_blob(Process_Q_update[type], 1, obj->OidPtr(), sizeof(ccoid_t), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_update[type], 2, PROCESS_Q_STATUS_HOLD))) return -1;
	if (dblog(sqlite3_bind_null(Process_Q_update[type], 3))) return -1;

	if (dblog(sqlite3_step(Process_Q_update[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj sqlite3_changes " << changes << " after Process_Q_update type " << type << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidateObj type " << type << " result " << result;

	if (!result)
	{
		*retobj = smartobj;

		finally2.Clear();	// don't increment work
	}

	return result;

	} // unlock db
}

int DbConnProcessQ::ProcessQUpdateSubsequentBlockStatus(unsigned type, const ccoid_t& oid)
{
	unsigned changes = 0;

	{

	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQUpdateSubsequentBlockStatus type " << type << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

	// ObjId
	if (dblog(sqlite3_bind_blob(Process_Q_update_priorobj[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;

	if (dblog(sqlite3_step(Process_Q_update_priorobj[type]), DB_STMT_STEP)) return -1;

	changes = sqlite3_changes(Process_Q_db[type]);

	} // unlock db

	if (changes > 0)
	{
		BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQUpdateSubsequentBlockStatus sqlite3_changes " << changes << " after Process_Q_update_priorobj type " << type << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

		IncrementQueuedWork(type, changes);
	}

	return 0;
}

int DbConnProcessQ::ProcessQUpdateObj(unsigned type, const ccoid_t& oid, int status, int64_t auxint)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQUpdateObj type " << type << " status " << status << " auxint " << auxint << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

	// ObjId, Status, AuxInt
	if (dblog(sqlite3_bind_blob(Process_Q_update[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_update[type], 2, status))) return -1;
	if (dblog(sqlite3_bind_int64(Process_Q_update[type], 3, auxint))) return -1;

	if (dblog(sqlite3_step(Process_Q_update[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnProcessQ::ProcessQUpdateObj sqlite3_changes " << changes << " after Process_Q_update type " << type << " status " << status << " auxint " << auxint << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

		return -1;
	}

	return 0;
}

int DbConnProcessQ::ProcessQCountValidObjs(unsigned type, int64_t auxint)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQCountValidObjs type " << type << " auxint " << auxint;

	int rc;

	// AuxInt
	if (dblog(sqlite3_bind_int64(Process_Q_count[type], 1, auxint))) return -1;

	if (dblog(rc = sqlite3_step(Process_Q_count[type]), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQCountValidObjs simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQCountValidObjs select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Process_Q_count[type]) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQCountValidObjs select returned " << sqlite3_data_count(Process_Q_count[type]) << " columns";

		return -1;
	}

	// count
	auto count = sqlite3_column_int(Process_Q_count[type], 0);

	if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQCountValidObjs simulating database error post-error check";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQCountValidObjs type " << type << " auxint " << auxint << " returning count " << count;

	return count;
}

int DbConnProcessQ::ProcessQClearValidObjs(unsigned type)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQClearValidObjs type " << type;

	if (dblog(sqlite3_step(Process_Q_clear[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes64(Process_Q_db[type]);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQClearValidObjs type " << type << " changes " << changes;

	return 0;
}

int DbConnProcessQ::ProcessQRandomizeValidObjs(unsigned type)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQRandomizeValidObjs type " << type;

	if (dblog(sqlite3_step(Process_Q_randomize[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes64(Process_Q_db[type]);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQRandomizeValidObjs type " << type << " changes " << changes;

	return 0;
}

int DbConnProcessQ::ProcessQGetNextValidObj(unsigned type, unsigned offset, SmartBuf *retobj)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidObj type " << type << " offset " << offset;

	retobj->ClearRef();

	int rc;

	// Status, offset
	if (dblog(sqlite3_bind_int(Process_Q_select_next[type], 1, PROCESS_Q_STATUS_VALID))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_select_next[type], 2, offset))) return -1;

	if (dblog(rc = sqlite3_step(Process_Q_select_next[type]), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidObj simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE) return 1;

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Process_Q_select_next[type]) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj select returned " << sqlite3_data_count(Process_Q_select_next[type]) << " columns";

		return -1;
	}

	// Level, ConnId, CallbackId, Bufp
	auto bufp_blob = sqlite3_column_blob(Process_Q_select_next[type], 3);

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidObj simulating database error; setting bufp_blob = NULL";

		bufp_blob = NULL;
	}

	if (!bufp_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj bufp_blob is null";

		return -1;
	}

	if (sqlite3_column_bytes(Process_Q_select_next[type], 3) != sizeof(void*))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj Bufp size " << sqlite3_column_bytes(Process_Q_select_next[type], 3) << " != " << sizeof(void*);

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidObj simulating database error post-error check";

		return -1;
	}

	void *bufp = *(void**)bufp_blob;

	if (!bufp)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj bufp is null";

		return -1;
	}

	SmartBuf smartobj(bufp);
	auto obj = (CCObject*)smartobj.data();
	CCASSERT(obj);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidObj bufp " << (uintptr_t)bufp << " obj.oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

	*retobj = smartobj;

	return 0;
}

int DbConnProcessQ::ProcessQDone(unsigned type, int64_t level)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQDone type " << type << " level " << level;

	// Level <
	if (dblog(sqlite3_bind_int64(Process_Q_done[type], 1, level))) return -1;

	if (dblog(sqlite3_step(Process_Q_done[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes64(Process_Q_db[type]);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQDone type " << type << " changes " << changes;

	return 0;
}

int DbConnProcessQ::ProcessQPruneLevel(unsigned type, int64_t level)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQPruneLevel type " << type << " level " << level;

	// Level <
	if (dblog(sqlite3_bind_int64(Process_Q_select_level[type], 1, level))) return -1;

	while (!g_shutdown)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Process_Q_select_level[type]), DB_STMT_SELECT)) return -1;

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQPruneLevel simulating database error post-select";

			return -1;
		}

		if (dbresult(rc) == SQLITE_DONE) return 1;

		if (dbresult(rc) != SQLITE_ROW)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel select returned " << rc;

			return -1;
		}

		if (sqlite3_data_count(Process_Q_select_level[type]) != 6)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel select returned " << sqlite3_data_count(Process_Q_select_level[type]) << " columns";

			return -1;
		}

		// ObjId, Bufp, Level, Status, ConnId, CallbackId
		auto objid_blob = sqlite3_column_blob(Process_Q_select_level[type], 0);
		auto bufp_blob = sqlite3_column_blob(Process_Q_select_level[type], 1);
		auto level = sqlite3_column_int64(Process_Q_select_level[type], 2);
		auto status = sqlite3_column_int(Process_Q_select_level[type], 3);
		auto conn_index = sqlite3_column_int(Process_Q_select_level[type], 4);
		auto callback_id = sqlite3_column_int(Process_Q_select_level[type], 5);

		ccoid_t oid;
		void *bufp = NULL;
		SmartBuf smartobj;
		CCObject* obj = NULL;

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQPruneLevel simulating database error; setting bufp_blob = NULL";

			bufp_blob = NULL;
		}

		if (!objid_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel ObjId is null";

			return -1;
		}
		else if (sqlite3_column_bytes(Process_Q_select_level[type], 0) != sizeof(ccoid_t))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel ObjId size " << sqlite3_column_bytes(Process_Q_select_level[type], 0) << " != " << sizeof(ccoid_t);

			return -1;
		}

		if (!bufp_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel bufp_blob is null";
		}
		else if (sqlite3_column_bytes(Process_Q_select_level[type], 1) != sizeof(void*))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel Bufp size " << sqlite3_column_bytes(Process_Q_select_level[type], 1) << " != " << sizeof(void*);

			bufp_blob = NULL;
		}

		memcpy(&oid, objid_blob, sizeof(ccoid_t));

		if (bufp_blob)
			bufp = *(void**)bufp_blob;

		if (!bufp)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel bufp is null";
		}
		else
		{
			if (TRACE_DBCONN | TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQPruneLevel smartobj " << (uintptr_t)&smartobj << " bufp " << (uintptr_t)bufp << " db ObjId " << buf2hex(objid_blob, CC_OID_TRACE_SIZE);

			smartobj.SetBasePtr(bufp);
			obj = (CCObject*)smartobj.data();
			CCASSERT(obj);

			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQPruneLevel bufp " << (uintptr_t)bufp << " obj.oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

			if (memcmp(obj->OidPtr(), objid_blob, sizeof(ccoid_t)))
			{
				BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel ObjId mismatch";

				smartobj.ClearRef();
			}
		}

		if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQPruneLevel simulating database error post-error check";

			return -1;
		}

		sqlite3_reset(Process_Q_select_level[type]);

		// DELETE entry from processing queue

		if (dblog(sqlite3_bind_blob(Process_Q_delete[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;

		if (dblog(sqlite3_step(Process_Q_delete[type]), DB_STMT_STEP)) return -1;

		sqlite3_reset(Process_Q_delete[type]);

		if (smartobj)
		{
			if (TRACE_DBCONN | TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQPruneLevel DecRef bufp " << (uintptr_t)smartobj.BasePtr() << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

			smartobj.DecRef();		// it's now deleted from the db
		}

		if (conn_index && ((status & ~PROCESS_Q_STATUS_DONE_FLAG) == PROCESS_Q_STATUS_PENDING || (status & ~PROCESS_Q_STATUS_DONE_FLAG) == PROCESS_Q_STATUS_HOLD) && !g_shutdown)
		{
			if (TRACE_DBCONN || TRACE_PROCESS) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQPruneLevel calling HandleValidateDone type " << type << " level " << level << " status " << status << " Conn " << conn_index << " callback_id " << callback_id;

			auto conn = g_connregistry.GetConn(conn_index);

			conn->HandleValidateDone(level, callback_id, 1);
		}
	}

	return 0;
}

int DbConnProcessQ::ProcessQSelectAndDelete(unsigned type, const ccoid_t& oid, unsigned& block_tx_count, unsigned& conn_index, uint32_t& callback_id)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type));

	block_tx_count = 0;
	conn_index = 0;
	callback_id = 0;

	int rc;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQSelectAndDelete type " << type << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

	if (dblog(sqlite3_bind_blob(Process_Q_select[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;

	if (dblog(rc = sqlite3_step(Process_Q_select[type]), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQSelectAndDelete simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQSelectAndDelete select returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQSelectAndDelete select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Process_Q_select[type]) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQSelectAndDelete select returned " << sqlite3_data_count(Process_Q_select[type]) << " columns";

		return -1;
	}

	// AuxInt, ConnId, CallbackId, Bufp
	block_tx_count = sqlite3_column_int(Process_Q_select[type], 0);
	conn_index = sqlite3_column_int(Process_Q_select[type], 1);
	callback_id = sqlite3_column_int(Process_Q_select[type], 2);
	auto bufp_blob = sqlite3_column_blob(Process_Q_select[type], 3);

	if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	// if bufp fails, we can still try to clean up by executing the delete statement
	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQSelectAndDelete simulating database error; setting bufp_blob = NULL";

		bufp_blob = NULL;
	}

	if (!bufp_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQSelectAndDelete bufp_blob is null";
	}
	else if (sqlite3_column_bytes(Process_Q_select[type], 3) != sizeof(void*))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQSelectAndDelete Bufp size " << sqlite3_column_bytes(Process_Q_select[type], 3) << " != " << sizeof(void*);

		bufp_blob = NULL;
	}

	auto *bufp = *(void**)bufp_blob;
	SmartBuf smartobj;
	CCObject* obj = NULL;

	if (!bufp)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQSelectAndDelete bufp is null";
	}
	else
	{
		if (TRACE_DBCONN | TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQSelectAndDelete smartobj " << (uintptr_t)&smartobj << " bufp " << (uintptr_t)bufp << " db ObjId " << buf2hex(&oid, CC_OID_TRACE_SIZE);

		smartobj.SetBasePtr(bufp);
		obj = (CCObject*)smartobj.data();
		CCASSERT(obj);

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQSelectAndDelete bufp " << (uintptr_t)bufp << " obj.oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

		if (memcmp(obj->OidPtr(), &oid, sizeof(ccoid_t)))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQSelectAndDelete ObjId mismatch";

			smartobj.ClearRef();
		}
	}

	if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	// DELETE tx from processing queue

	if (dblog(sqlite3_bind_blob(Process_Q_delete[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;

	if (dblog(sqlite3_step(Process_Q_delete[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQSelectAndDelete sqlite3_changes " << changes << " after Process_Q_delete type " << type << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

		return -1;
	}

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQSelectAndDelete DecRef bufp " << (uintptr_t)smartobj.BasePtr() << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

	smartobj.DecRef();		// it's now deleted from the db

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQSelectAndDelete type " << type << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE) << " block_tx_count " << block_tx_count << " conn_index " << conn_index << " callback_id " << callback_id;

	return 0;
}
