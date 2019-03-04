/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * dbconn-processq.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"

#include <dblog.h>
#include <CCobjects.hpp>

#define TRACE_DBCONN	(g_params.trace_validation_q_db)

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

		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "insert into Process_Q (ObjId, PriorOid, Level, Status, Priority, AuxInt, CallbackId, Bufp) values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);", -1, &Process_Q_insert[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "begin exclusive;", -1, &Process_Q_begin[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "rollback;", -1, &Process_Q_rollback[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "commit;", -1, &Process_Q_commit[i], NULL)));
		// ok to use offset in following select because between calls (a) no one will change sort keys; (b) no one will delete an entry (b) if an entry is added, it's ok to get same entry twice
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "select ObjId, AuxInt, CallbackId, Bufp from Process_Q where Status = ?1 order by Priority, Level desc limit 1 offset ?2;", -1, &Process_Q_select[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Status = ?2, AuxInt = ?3 where ObjId = ?1;", -1, &Process_Q_update[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Status = " STRINGIFY(PROCESS_Q_STATUS_PENDING) " where Status = " STRINGIFY(PROCESS_Q_STATUS_HOLD) " and PriorOid = ?1;", -1, &Process_Q_update_priorobj[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set AuxInt = 0 where Status = " STRINGIFY(PROCESS_Q_STATUS_VALID) ";", -1, &Process_Q_clear[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "select count(*) from Process_Q where Status = " STRINGIFY(PROCESS_Q_STATUS_VALID) " and AuxInt = ?1;", -1, &Process_Q_count[i], NULL)));	// used for testing
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Priority = random() where Status = " STRINGIFY(PROCESS_Q_STATUS_VALID) ";", -1, &Process_Q_randomize[i], NULL)));		// used for testing
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "update Process_Q set Status = " STRINGIFY(PROCESS_Q_STATUS_DONE) " where Level < ?1;", -1, &Process_Q_done[i], NULL)));
		CCASSERTZ(dblog(sqlite3_prepare_v2(Process_Q_db[i], "select ObjId, Bufp from Process_Q where Level < ?1 limit 1;", -1, &Process_Q_select_level[i], NULL)));
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
		DbFinalize(Process_Q_begin[i], explain);
		DbFinalize(Process_Q_rollback[i], explain);
		DbFinalize(Process_Q_commit[i], explain);
		DbFinalize(Process_Q_select[i], explain);
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

void DbConnProcessQ::DoProcessQFinish(unsigned type, bool rollback, bool increment_work)
{
	if ((TEST_DELAY_DB_RESET & rand()) == 1) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::DoProcessQFinish dbconn " << uintptr_t(this) << " type " << type << " rollback " << rollback << " increment work " << increment_work;

	sqlite3_reset(Process_Q_insert[type]);
	sqlite3_reset(Process_Q_select[type]);
	sqlite3_reset(Process_Q_update[type]);
	sqlite3_reset(Process_Q_update_priorobj[type]);
	sqlite3_reset(Process_Q_clear[type]);
	sqlite3_reset(Process_Q_count[type]);
	sqlite3_reset(Process_Q_randomize[type]);
	sqlite3_reset(Process_Q_done[type]);
	sqlite3_reset(Process_Q_select_level[type]);
	sqlite3_reset(Process_Q_delete[type]);
	sqlite3_reset(Process_Q_commit[type]);
	sqlite3_reset(Process_Q_begin[type]);
	sqlite3_reset(Process_Q_rollback[type]);

	if (rollback)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::DoProcessQFinish dbconn " << uintptr_t(this) << " type " << type << " rollback";

		dblog(sqlite3_step(Process_Q_rollback[type]), DB_STMT_STEP);
		sqlite3_reset(Process_Q_rollback[type]);
	}


	if (increment_work)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::DoProcessQFinish dbconn " << uintptr_t(this) << " type " << type << " IncrementQueuedWork";

		IncrementQueuedWork(type);
	}
}

void DbConnProcessQ::IncrementQueuedWork(unsigned type, unsigned changes)
{
	CCASSERT(type < PROCESS_Q_N);

	auto prior_work = queued_work[type].fetch_add(changes);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::IncrementQueuedWork type " << type << " changes " << changes << " pre-increment work " << prior_work;

	if (prior_work <= 0)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::IncrementQueuedWork calling notify_one/notify_all type " << type;

		lock_guard<mutex> lock(work_queue_mutex[type]);

		for (unsigned i = 0; i < changes; ++i)
			work_queue_condition_variable[type].notify_one();	// need to hold lock so notify isn't missed by a thread that is just about to enter wait
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
			work_queue_condition_variable[type].wait(lock);		// lock is acquired before waking up
		}
		else
		{
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

int DbConnProcessQ::ProcessQEnqueueValidate(unsigned type, SmartBuf smartobj, const ccoid_t *prior_oid, int64_t level, unsigned status, int64_t priority, unsigned conn_index, uint32_t callback_id)
{
	unique_lock<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQEnqueueValidate type " << type << " level " << level << " status " << status << " priority " << priority << " callback_id " << callback_id << " smartobj " << (uintptr_t)&smartobj;

	auto bufp = smartobj.BasePtr();
	auto obj = (CCObject*)smartobj.data();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQEnqueueValidate type " << type << " level " << level << " status " << status << " priority " << priority << " callback_id " << callback_id << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t)) << " conn_index Conn " << conn_index << " callback_id " << callback_id;

	// ObjId, PriorOid, Level, Status, Priority, AuxInt, CallbackId, Bufp
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
	if (dblog(sqlite3_bind_int64(Process_Q_insert[type], 5, priority))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_insert[type], 6, conn_index))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_insert[type], 7, callback_id))) return -1;
	if (dblog(sqlite3_bind_blob(Process_Q_insert[type], 8, &bufp, sizeof(bufp), SQLITE_STATIC))) return -1;

	auto rc = sqlite3_step(Process_Q_insert[type]);
	if (dbresult(rc) == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQEnqueueValidate constraint violation; object downloaded more than once?";

		return 1;
	}
	else if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (changes)
	{
		if (TRACE_DBCONN || TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQEnqueueValidate inserted into Process_Q type " << type << " level " << level << " status " << status << " priority " << priority << " callback_id " << callback_id << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		smartobj.IncRef();
	}

	DoProcessQFinish(type, 0, 0);	// don't rollback or increment work

	finally.Clear();

	lock.unlock();

	if (changes != 1)
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQEnqueueValidate sqlite3_changes " << changes << " after insert into Process_Q type " << type << " status " << status << " level " << level << " priority " << priority << " callback_id " << callback_id << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	if (changes)
		IncrementQueuedWork(type);

	return 0;
}

int DbConnProcessQ::ProcessQGetNextValidateObj(unsigned type, SmartBuf *retobj, unsigned& conn_index, uint32_t& callback_id)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 1, 1));		// rollback and increment work

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidateObj type " << type;

	retobj->ClearRef();
	conn_index = 0;
	callback_id = 0;

	int result = 0;
	int rc;

	// BEGIN

	if (dblog(sqlite3_step(Process_Q_begin[type]), DB_STMT_STEP)) return -1;

	// SELECT an entry from Process_Q --> Priority, ObjId, Callback, Bufp

	// Status, offset
	if (dblog(sqlite3_bind_int(Process_Q_select[type], 1, PROCESS_Q_STATUS_PENDING))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_select[type], 2, 0))) return -1;

	if (dblog(rc = sqlite3_step(Process_Q_select[type]), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidateObj simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidateObj select returned SQLITE_DONE";

		DoProcessQFinish(type, 1, 0);	// rollback but don't increment work

		finally.Clear();

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Process_Q_select[type]) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj select returned " << sqlite3_data_count(Process_Q_select[type]) << " columns";

		return -1;
	}

	// ObjId, AuxInt, CallbackId, Bufp
	auto objid_blob = sqlite3_column_blob(Process_Q_select[type], 0);
	conn_index = sqlite3_column_int(Process_Q_select[type], 1);
	callback_id = sqlite3_column_int(Process_Q_select[type], 2);
	auto bufp_blob = sqlite3_column_blob(Process_Q_select[type], 3);

	ccoid_t oid;
	void *bufp = NULL;
	SmartBuf smartobj;
	CCObject* obj = NULL;

	// must have objid_blob for the delete statement
	// if other columns fail, it is an error but we can still try to clean up by executing the delete statement

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidateObj simulating database error; setting bufp_blob = NULL";

		bufp_blob = NULL;
	}

	if (!objid_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj ObjId is null";

		return -1;
	}
	else if (sqlite3_column_bytes(Process_Q_select[type], 0) != sizeof(ccoid_t))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj ObjId size " << sqlite3_column_bytes(Process_Q_select[type], 0) << " != " << sizeof(ccoid_t);

		return -1;
	}

	if (!bufp_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj bufp_blob is null";
	}
	else if (sqlite3_column_bytes(Process_Q_select[type], 3) != sizeof(void*))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj Bufp size " << sqlite3_column_bytes(Process_Q_select[type], 3) << " != " << sizeof(void*);

		bufp_blob = NULL;
	}

	memcpy(&oid, objid_blob, sizeof(ccoid_t));

	if (!bufp_blob)
		result = -1;
	else
		bufp = *(void**)bufp_blob;

	if (!bufp)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj bufp is null";

		result = -1;
	}
	else
	{
		if (TRACE_DBCONN | TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQGetNextValidateObj smartobj " << (uintptr_t)&smartobj << " bufp " << (uintptr_t)bufp << " db ObjId " << buf2hex(objid_blob, sizeof(ccoid_t));

		smartobj.SetBasePtr(bufp);
		obj = (CCObject*)smartobj.data();
		CCASSERT(obj);

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidateObj bufp " << (uintptr_t)bufp << " obj.oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		if (memcmp(obj->OidPtr(), objid_blob, sizeof(ccoid_t)))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj ObjId mismatch";

			result = -1;	// server error
		}
	}

	if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidateObj simulating database error post-error check";

		return -1;
	}

	if (type == PROCESS_Q_TYPE_TX)
	{
		// DELETE tx from processing queue

		if (dblog(sqlite3_bind_blob(Process_Q_delete[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;

		if (dblog(sqlite3_step(Process_Q_delete[type]), DB_STMT_STEP)) return -1;
	}
	else
	{
		// UPDATE block to status = PROCESS_Q_STATUS_HOLD

		// ObjId, Status, AuxInt
		if (dblog(sqlite3_bind_blob(Process_Q_update[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;
		if (dblog(sqlite3_bind_int(Process_Q_update[type], 2, PROCESS_Q_STATUS_HOLD))) return -1;
		if (dblog(sqlite3_bind_int64(Process_Q_update[type], 3, 0))) return -1;

		if (dblog(sqlite3_step(Process_Q_update[type]), DB_STMT_STEP)) return -1;
	}

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidateObj sqlite3_changes " << changes << " after Process_Q_update/Process_Q_delete type " << type << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(&oid, sizeof(ccoid_t));

		return -1;
	}

	// COMMIT

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidateObj simulating database error pre-commit";

		return -1;
	}

	if (dblog(sqlite3_step(Process_Q_commit[type]), DB_STMT_STEP)) return -1;

	if (type == PROCESS_Q_TYPE_TX)
	{
		if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQGetNextValidateObj DecRef bufp " << (uintptr_t)smartobj.BasePtr() << " oid " << buf2hex(&oid, sizeof(ccoid_t));

		smartobj.DecRef();		// it's now deleted from the db
	}

	DoProcessQFinish(type, 0, 0);	// don't rollback or increment work

	finally.Clear();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidateObj done result " << result;

	if (!result)
		*retobj = smartobj;

	return result;
}

int DbConnProcessQ::ProcessQUpdateSubsequentBlockStatus(unsigned type, const ccoid_t& oid)
{
	unique_lock<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQUpdateSubsequentBlockStatus type " << type << " oid " << buf2hex(&oid, sizeof(ccoid_t));

	// ObjId
	if (dblog(sqlite3_bind_blob(Process_Q_update_priorobj[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;

	if (dblog(sqlite3_step(Process_Q_update_priorobj[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	DoProcessQFinish(type, 0, 0);	// don't rollback or increment work

	finally.Clear();

	lock.unlock();

	if (changes > 0)
	{
		BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQUpdateSubsequentBlockStatus sqlite3_changes " << changes << " after Process_Q_update_priorobj type " << type << " oid " << buf2hex(&oid, sizeof(ccoid_t));

		IncrementQueuedWork(type, changes);
	}

	return 0;
}

int DbConnProcessQ::ProcessQUpdateValidObj(unsigned type, const ccoid_t& oid, int status, int64_t auxint)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQUpdateValidObj type " << type << " status " << status << " auxint " << auxint << " oid " << buf2hex(&oid, sizeof(ccoid_t));

	// ObjId, Status, AuxInt
	if (dblog(sqlite3_bind_blob(Process_Q_update[type], 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_update[type], 2, status))) return -1;
	if (dblog(sqlite3_bind_int64(Process_Q_update[type], 3, auxint))) return -1;

	if (dblog(sqlite3_step(Process_Q_update[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnProcessQ::ProcessQUpdateValidObj sqlite3_changes " << changes << " after Process_Q_update type " << type << " status " << status << " auxint " << auxint << " oid " << buf2hex(&oid, sizeof(ccoid_t));

		return -1;
	}

	return 0;
}

int DbConnProcessQ::ProcessQCountValidObjs(unsigned type, int64_t auxint)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQCountValidObjs type " << type << " auxint " << auxint;

	int rc;

	// AuxInt
	if (dblog(sqlite3_bind_int64(Process_Q_count[type], 1, auxint))) return -1;

	if (dblog(rc = sqlite3_step(Process_Q_count[type]), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
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

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
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
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQClearValidObjs type " << type;

	if (dblog(sqlite3_step(Process_Q_clear[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQClearValidObjs type " << type << " changes " << changes;

	return 0;
}

int DbConnProcessQ::ProcessQRandomizeValidObjs(unsigned type)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQRandomizeValidObjs type " << type;

	if (dblog(sqlite3_step(Process_Q_randomize[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQRandomizeValidObjs type " << type << " changes " << changes;

	return 0;
}

int DbConnProcessQ::ProcessQGetNextValidObj(unsigned type, unsigned offset, SmartBuf *retobj)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidObj type " << type << " offset " << offset;

	retobj->ClearRef();

	int rc;

	// Status, offset
	if (dblog(sqlite3_bind_int(Process_Q_select[type], 1, PROCESS_Q_STATUS_VALID))) return -1;
	if (dblog(sqlite3_bind_int(Process_Q_select[type], 2, offset))) return -1;

	if (dblog(rc = sqlite3_step(Process_Q_select[type]), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
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

	if (sqlite3_data_count(Process_Q_select[type]) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj select returned " << sqlite3_data_count(Process_Q_select[type]) << " columns";

		return -1;
	}

	// ObjId, AuxInt, CallbackId, Bufp
	auto bufp_blob = sqlite3_column_blob(Process_Q_select[type], 3);

	void *bufp = NULL;
	SmartBuf smartobj;
	CCObject* obj = NULL;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidObj simulating database error; setting bufp_blob = NULL";

		bufp_blob = NULL;
	}

	if (!bufp_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj bufp_blob is null";

		return -1;
	}

	if (sqlite3_column_bytes(Process_Q_select[type], 3) != sizeof(void*))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj Bufp size " << sqlite3_column_bytes(Process_Q_select[type], 3) << " != " << sizeof(void*);

		return -1;
	}

	bufp = *(void**)bufp_blob;

	if (!bufp)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQGetNextValidObj bufp is null";

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnProcessQ::ProcessQGetNextValidObj simulating database error post-error check";

		return -1;
	}

	smartobj.SetBasePtr(bufp);
	obj = (CCObject*)smartobj.data();
	CCASSERT(obj);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQGetNextValidObj bufp " << (uintptr_t)bufp << " obj.oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	*retobj = smartobj;

	return 0;
}

int DbConnProcessQ::ProcessQDone(unsigned type, int64_t level)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQDone type " << type << " level " << level;

	if (dblog(sqlite3_bind_int64(Process_Q_done[type], 1, level))) return -1;

	if (dblog(sqlite3_step(Process_Q_done[type]), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Process_Q_db[type]);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQDone type " << type << " changes " << changes;

	return 0;
}

int DbConnProcessQ::ProcessQPruneLevel(unsigned type, int64_t level)
{
	lock_guard<mutex> lock(Process_Q_db_mutex[type]);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnProcessQ::DoProcessQFinish, this, type, 0, 0));		// reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQPruneLevel type " << type << " level " << level;

	if (dblog(sqlite3_bind_int64(Process_Q_select_level[type], 1, level))) return -1;

	while (true)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Process_Q_select_level[type]), DB_STMT_SELECT)) return -1;

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
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

		if (sqlite3_data_count(Process_Q_select_level[type]) != 2)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel select returned " << sqlite3_data_count(Process_Q_select_level[type]) << " columns";

			return -1;
		}

		// ObjId, Bufp
		auto objid_blob = sqlite3_column_blob(Process_Q_select_level[type], 0);
		auto bufp_blob = sqlite3_column_blob(Process_Q_select_level[type], 1);

		ccoid_t oid;
		void *bufp = NULL;
		SmartBuf smartobj;
		CCObject* obj = NULL;

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
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
			if (TRACE_DBCONN | TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQPruneLevel smartobj " << (uintptr_t)&smartobj << " bufp " << (uintptr_t)bufp << " db ObjId " << buf2hex(objid_blob, sizeof(ccoid_t));

			smartobj.SetBasePtr(bufp);
			obj = (CCObject*)smartobj.data();
			CCASSERT(obj);

			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnProcessQ::ProcessQPruneLevel bufp " << (uintptr_t)bufp << " obj.oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

			if (memcmp(obj->OidPtr(), objid_blob, sizeof(ccoid_t)))
			{
				BOOST_LOG_TRIVIAL(error) << "DbConnProcessQ::ProcessQPruneLevel ObjId mismatch";

				smartobj.ClearRef();
			}
		}

		if (dblog(sqlite3_extended_errcode(Process_Q_db[type]), DB_STMT_SELECT)) return -1;	// check if error retrieving results

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1)
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
			if (TRACE_DBCONN | TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnProcessQ::ProcessQPruneLevel DecRef bufp " << (uintptr_t)smartobj.BasePtr() << " oid " << buf2hex(&oid, sizeof(ccoid_t));

			smartobj.DecRef();		// it's now deleted from the db
		}
	}

	return 0;
}
