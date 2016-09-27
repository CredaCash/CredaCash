/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dbconn-persistent.cpp
*/

#include "CCdef.h"
#include "dbconn.hpp"
#include "block.hpp"
#include "util.h"
#include "dbparamkeys.h"

#include <dblog.h>
#include <CCobjects.hpp>
#include <Finally.hpp>
#include <CCutil.h>

#define TRACE_DBCONN	(g_params.trace_persistent_db)

//#define TEST_FOR_TIMING_ERROR	1	// for testing

#ifndef TEST_FOR_TIMING_ERROR
#define TEST_FOR_TIMING_ERROR	0	// don't test
#endif

static mutex Persistent_db_write_mutex;		// since db is in WAL mode, this mutex is used only as a write-lock
static atomic<uint8_t> write_pending;
static atomic<ccthreadid_t> write_thread_id;

// note Persistent_db_write_mutex is shared by reference with DbConnPersistData::Persistent_Wal,
// so Persistent_db_write_mutex and DbConnPersistData::Persistent_Wal::Wal_db_mutex refer to the same mutex
WalDB DbConnPersistData::Persistent_Wal("PersistData", Persistent_db_write_mutex);

DbConnPersistData::DbConnPersistData()
{
	lock_guard<mutex> lock(Persistent_db_write_mutex);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::DbConnPersistData dbconn " << (uintptr_t)this;

	OpenDb();

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "begin;", -1, &Persistent_Data_begin_read, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "rollback;", -1, &Persistent_Data_rollback, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "begin exclusive;", -1, &Persistent_Data_begin_write, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "commit;", -1, &Persistent_Data_commit, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert or replace into Parameters (Key, Subkey, Value) values (?1, ?2, ?3);", -1, &Parameters_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Value from Parameters where Key = ?1 and Subkey = ?2;", -1, &Parameters_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Blockchain (Level, Block) values (?1, ?2);", -1, &Blockchain_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select max(Level) from Blockchain;", -1, &Blockchain_select_max, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Block from Blockchain where Level = ?1;", -1, &Blockchain_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Serialnums (Serialnum) values (?1);", -1, &Serialnum_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select count(*) from Serialnums where Serialnum = ?1;", -1, &Serialnum_check, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert or replace into Commit_Tree (Height, Offset, Data) values (?1, ?2, ?3);", -1, &Commit_Tree_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Data from Commit_Tree where Height = ?1 and Offset = ?2;", -1, &Commit_Tree_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Commit_Roots (Level, Timestamp, MerkleRoot) values (?1, ?2, ?3);", -1, &Commit_Roots_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Timestamp, MerkleRoot from Commit_Roots where Level = ?1 or (?2 and Level >= ?1) order by Level limit 1;", -1, &Commit_Roots_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Tx_Outputs (Address, ValueEnc, ParamLevel, Commitment, Commitnum) values (?1, ?2, ?3, ?4, ?5);", -1, &Tx_Outputs_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select ValueEnc, MerkleRoot, Commitment, Commitnum from Tx_Outputs, Commit_Roots on Level = ParamLevel where Address = ?1 and Commitnum >= ?2 and Commitnum <= ?3 order by Commitnum limit ?4;", -1, &Tx_Outputs_select, NULL)));

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::DbConnPersistData dbconn done " << (uintptr_t)this;
}

DbConnPersistData::~DbConnPersistData()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::~DbConnPersistData dbconn " << (uintptr_t)this;

	static bool explain = TEST_EXPLAIN_DB_QUERIES;

#if TEST_EXPLAIN_DB_QUERIES
	unique_lock<mutex> elock(g_db_explain_lock);

	if (!explain)
		elock.unlock();

	lock_guard<mutex> lock(Persistent_db_write_mutex);
#endif

	//if (explain)
	//	CCASSERTZ(dbexec(Persistent_db, "analyze;"));

	DbFinalize(Persistent_Data_begin_read, explain);
	DbFinalize(Persistent_Data_rollback, explain);
	DbFinalize(Persistent_Data_begin_write, explain);
	DbFinalize(Persistent_Data_commit, explain);
	DbFinalize(Parameters_insert, explain);
	DbFinalize(Parameters_select, explain);
	DbFinalize(Blockchain_insert, explain);
	DbFinalize(Blockchain_select_max, explain);
	DbFinalize(Blockchain_select, explain);
	DbFinalize(Serialnum_insert, explain);
	DbFinalize(Serialnum_check, explain);
	DbFinalize(Commit_Tree_insert, explain);
	DbFinalize(Commit_Tree_select, explain);
	DbFinalize(Commit_Roots_insert, explain);
	DbFinalize(Commit_Roots_select, explain);
	DbFinalize(Tx_Outputs_insert, explain);
	DbFinalize(Tx_Outputs_select, explain);

	explain = false;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::~DbConnPersistData done dbconn " << (uintptr_t)this;
}

void DbConnPersistData::DoPersistentDataFinish()
{
	if ((TEST_DELAY_DB_RESET & rand()) == 1) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::DoPersistentDataFinish dbconn " << uintptr_t(this);

	sqlite3_reset(Parameters_insert);
	sqlite3_reset(Parameters_select);
	sqlite3_reset(Blockchain_insert);
	sqlite3_reset(Blockchain_select_max);
	sqlite3_reset(Blockchain_select);
	sqlite3_reset(Serialnum_insert);
	sqlite3_reset(Serialnum_check);
	sqlite3_reset(Commit_Tree_insert);
	sqlite3_reset(Commit_Tree_select);
	sqlite3_reset(Commit_Roots_insert);
	sqlite3_reset(Commit_Roots_select);
	sqlite3_reset(Tx_Outputs_insert);
	sqlite3_reset(Tx_Outputs_select);
	sqlite3_reset(Persistent_Data_begin_read);
	sqlite3_reset(Persistent_Data_rollback);
	sqlite3_reset(Persistent_Data_begin_write);
	sqlite3_reset(Persistent_Data_commit);
}

int DbConnPersistData::BeginRead()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginRead";

	auto rc = dblog(sqlite3_step(Persistent_Data_begin_read), DB_STMT_STEP);

	DoPersistentDataFinish();

	if (rc)
		return -1;

	return 0;
}

int DbConnPersistData::EndRead()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::EndRead";

	auto rc = dblog(sqlite3_step(Persistent_Data_rollback), DB_STMT_STEP);

	DoPersistentDataFinish();

	if (rc)
		return -1;

	return 0;
}

bool DbConnPersistData::ThisThreadHoldsMutex()
{
	return write_pending && ccgetthreadid() == write_thread_id;
}

int DbConnPersistData::BeginWrite()
{
	if (ThisThreadHoldsMutex())
		return 1;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginWrite acquiring mutex...";

	Persistent_db_write_mutex.lock();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginWrite mutex acquired";

retry:

	auto rc = dblog(sqlite3_step(Persistent_Data_begin_write), DB_STMT_STEP);	// note: this can return SQLITE_BUSY if sqlite3_busy_timeout is enabled

	DoPersistentDataFinish();

	if (rc == SQLITE_BUSY)	// this can happen during testing if sqlite3_busy_timeout is enabled
	{
		usleep(1000);
		goto retry;
	}

	if (rc)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginWrite releasing mutex";

		Persistent_db_write_mutex.unlock();

		return -1;
	}

	if (TEST_FOR_TIMING_ERROR && (rand() & 0xFFF))	// try multiple times to make sure there are no timing errors
	{
		EndRead();
		goto retry;
	}

	write_pending = true;
	write_thread_id = ccgetthreadid();

	return 0;
}

int DbConnPersistData::EndWrite(bool commit)
{
	if (!ThisThreadHoldsMutex())
		return 1;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::EndWrite commit " << commit;

	int rc;

	if (commit)
		rc = dblog(sqlite3_step(Persistent_Data_commit), DB_STMT_STEP);
	else
		rc = dblog(sqlite3_step(Persistent_Data_rollback), DB_STMT_STEP);

	DoPersistentDataFinish();

	write_pending = false;

	if (rc || !commit)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::EndWrite releasing mutex";

		Persistent_db_write_mutex.unlock();
	}

	if (rc)
		return -1;

	return 0;
}

void DbConnPersistData::ReleaseMutex()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ReleaseMutex releasing mutex";

	Persistent_db_write_mutex.unlock();
}

int DbConnPersistData::ParameterInsert(int key, int subkey, void *value, unsigned valsize)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterInsert key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

	// Key, Subkey, Value
	if (dblog(sqlite3_bind_int(Parameters_insert, 1, key))) return -1;
	if (dblog(sqlite3_bind_int(Parameters_insert, 2, subkey))) return -1;
	if (dblog(sqlite3_bind_blob(Parameters_insert, 3, value, valsize, SQLITE_STATIC))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::ParameterInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Parameters_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterInsert sqlite3_changes " << changes << " after insert into Parameters key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::ParameterInsert inserted into Parameters key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

	return 0;
}

int DbConnPersistData::ParameterSelect(int key, int subkey, void *value, unsigned bufsize, unsigned *retsize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	memset(value, 0, bufsize);
	if (retsize)
		*retsize = 0;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " bufsize " << bufsize;

	int rc;

	// Key, Subkey
	if (dblog(sqlite3_bind_int(Parameters_select, 1, key))) return -1;
	if (dblog(sqlite3_bind_int(Parameters_select, 2, subkey))) return -1;

	if (dblog(rc = sqlite3_step(Parameters_select), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::ParameterSelect simulating database error post-select";

		return -1;
	}

	if (rc == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::ParameterSelect select returned SQLITE_DONE";

		return 1;
	}

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect select returned " << rc;

		return -1;
	}

	// Value
	auto data_blob = sqlite3_column_blob(Parameters_select, 0);
	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect Data is null";

		return -1;
	}

	auto datasize = sqlite3_column_bytes(Parameters_select, 0);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::ParameterSelect simulating database error post-error check";

		return -1;
	}

	if (datasize > (int)(bufsize))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " data size " << datasize << " > " << bufsize;

		return -1;
	}

	if (!retsize && datasize > (int)(bufsize))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " data size " << datasize << " != " << bufsize;

		return -1;
	}

	memcpy(value, data_blob, datasize);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " returning obj size " << datasize << " value " << buf2hex(value, (datasize < 16 ? datasize : 16));

	if (retsize)
		*retsize = datasize;

	return 0;
}

int DbConnPersistData::BlockchainInsert(uint64_t level, SmartBuf smartobj)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	auto bufp = smartobj.BasePtr();
	auto obj = (CCObject*)smartobj.data();
	auto objsize = obj->ObjSize();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainInsert level " << level << " obj tag " << obj->ObjTag() << " obj size " << objsize << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	// Level, Block
	if (dblog(sqlite3_bind_int64(Blockchain_insert, 1, level))) return -1;
	if (dblog(sqlite3_bind_blob(Blockchain_insert, 2, obj->ObjPtr(), objsize, SQLITE_STATIC))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Blockchain_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainInsert sqlite3_changes " << changes << " after insert into Blockchain level " << level << " obj tag " << obj->ObjTag() << " obj size " << objsize << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::BlockchainInsert inserted into Blockchain level " << level << " obj tag " << obj->ObjTag() << " obj size " << objsize << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	return 0;
}

int DbConnPersistData::BlockchainSelect(uint64_t level, SmartBuf *retobj)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));
	retobj->ClearRef();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelect level " << level;

	int rc;

	// Level
	if (dblog(sqlite3_bind_int64(Blockchain_select, 1, level))) return -1;

	if (dblog(rc = sqlite3_step(Blockchain_select), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelect simulating database error post-select";

		return -1;
	}

	if (rc == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::BlockchainSelect select returned SQLITE_DONE";

		return 1;
	}

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect select returned " << rc;

		return -1;
	}

	// Block
	auto data_blob = sqlite3_column_blob(Blockchain_select, 0);
	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect Data is null";

		return -1;
	}

	auto datasize = sqlite3_column_bytes(Blockchain_select, 0);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelect simulating database error post-error check";

		return -1;
	}

	if (datasize < (int)(sizeof(CCObject::Header) + sizeof(BlockWireHeader)) || datasize > CC_BLOCK_MAX_SIZE)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect data size " << datasize << " < " << sizeof(BlockWireHeader) << " or > CC_BLOCK_MAX_SIZE " << CC_BLOCK_MAX_SIZE;

		return -1;
	}

	unsigned tag = *(uint32_t*)((char*)data_blob + 4);
	if (tag != CC_TAG_BLOCK)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect tag " << tag << " != CC_TAG_BLOCK " << CC_TAG_BLOCK;

		return -1;
	}

	auto wire = (BlockWireHeader*)((char*)data_blob + sizeof(CCObject::Header));
	if (wire->level != level)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect data level " << wire->level << " != " << level;

		return -1;
	}

	SmartBuf smartobj(datasize + sizeof(CCObject::Preamble));

	memcpy(smartobj.data() + sizeof(CCObject::Preamble), data_blob, datasize);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelect level " << level << " returning obj size " << datasize;

	*retobj = smartobj;

	return 0;
}

int DbConnPersistData::BlockchainSelectMax(uint64_t& level)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelectMax";

	level = 0;
	int rc;

	if (dblog(rc = sqlite3_step(Blockchain_select_max), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelectMax simulating database error post-select";

		return -1;
	}

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelectMax select returned " << rc;

		return -1;
	}

	// Level

	if (sqlite3_column_type(Blockchain_select_max, 0) == SQLITE_NULL)
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelectMax select returned SQLITE_NULL";

		return 1;
	}

	level = sqlite3_column_int64(Blockchain_select_max, 0);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelectMax simulating database error post-error check";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelectMax level " << level;

	return 0;
}

int DbConnPersistData::SerialnumInsert(const void *serial, unsigned size)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::SerialnumInsert serialnum " << buf2hex(serial, size);

	// Serialnum
	if (dblog(sqlite3_bind_blob(Serialnum_insert, 1, serial, size, SQLITE_STATIC))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::SerialnumInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Serialnum_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumInsert sqlite3_changes " << changes << " after insert into Serialnums " << buf2hex(serial, size);

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::SerialnumInsert inserted serialnum " << buf2hex(serial, size);

	return 0;
}

int DbConnPersistData::SerialnumCheck(const void *serial, unsigned size)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::SerialnumCheck serialnum " << buf2hex(serial, size);

	int rc;

	// Serialnum
	if (dblog(sqlite3_bind_blob(Serialnum_check, 1, serial, size, SQLITE_STATIC))) return -1;

	if (dblog(rc = sqlite3_step(Serialnum_check), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::SerialnumCheck simulating database error post-select";

		return -1;
	}

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumCheck select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Serialnum_check) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumCheck select returned " << sqlite3_data_count(Serialnum_check) << " columns";

		return -1;
	}

	// count
	auto count = sqlite3_column_int(Serialnum_check, 0);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::SerialnumCheck simulating database error post-error check";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::SerialnumCheck returning count " << count << " for serialnum " << buf2hex(serial, size);

	return count;
}

int DbConnPersistData::CommitTreeInsert(unsigned height, uint64_t offset, const void *data, unsigned datasize)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitTreeInsert height " << height << " offset " << offset << " data " << buf2hex(data, datasize);

	// Height, Offset, Hash
	if (dblog(sqlite3_bind_int(Commit_Tree_insert, 1, height))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Tree_insert, 2, offset))) return -1;
	if (dblog(sqlite3_bind_blob(Commit_Tree_insert, 3, data, datasize, SQLITE_STATIC))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitTreeInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Commit_Tree_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeInsert sqlite3_changes " << changes << " after insert into CommitTree height " << height << " offset " << offset << " data " << buf2hex(data, datasize);

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitTreeInsert inserted into CommitTree height " << height << " offset " << offset << " data " << buf2hex(data, datasize);

	return 0;
}

int DbConnPersistData::CommitTreeSelect(unsigned height, uint64_t offset, void *data, unsigned datasize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitTreeSelect height " << height << " offset " << offset;

	memset(data, 0, datasize);
	int rc;

	// Height, Offset
	if (dblog(sqlite3_bind_int(Commit_Tree_select, 1, height))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Tree_select, 2, offset))) return -1;

	if (dblog(rc = sqlite3_step(Commit_Tree_select), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitTreeSelect simulating database error post-select";

		return -1;
	}

	if (rc == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::CommitTreeSelect select returned SQLITE_DONE";

		return 1;
	}

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect select returned " << rc;

		return -1;
	}

	// Data
	auto data_blob = sqlite3_column_blob(Commit_Tree_select, 0);
	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect Data is null";

		return -1;
	}
	else if (sqlite3_column_bytes(Commit_Tree_select, 0) != (int)datasize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect Data size " << sqlite3_column_bytes(Commit_Tree_select, 0) << " != " << datasize;

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitTreeSelect simulating database error post-error check";

		return -1;
	}

	memcpy(data, data_blob, datasize);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitTreeSelect height " << height << " offset " << offset << " returning " << buf2hex(data, datasize);

	return 0;
}

int DbConnPersistData::CommitRootsInsert(uint64_t level, uint64_t timestamp, const void *hash, unsigned hashsize)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsInsert level " << level << " timestamp " << timestamp << " hash " << buf2hex(hash, hashsize);

	// Level, Timestamp, MerkleRoot
	if (dblog(sqlite3_bind_int64(Commit_Roots_insert, 1, level))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Roots_insert, 2, timestamp))) return -1;
	if (dblog(sqlite3_bind_blob(Commit_Roots_insert, 3, hash, hashsize, SQLITE_STATIC))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Commit_Roots_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsInsert sqlite3_changes " << changes << " after insert into CommitRoots level " << level << " timestamp " << timestamp << " hash " << buf2hex(hash, hashsize);

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsInsert inserted into CommitRoots level " << level << " timestamp " << timestamp << " hash " << buf2hex(hash, hashsize);

	return 0;
}

int DbConnPersistData::CommitRootsSelect(uint64_t level, bool or_greater, uint64_t& timestamp, void *hash, unsigned hashsize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelect level " << level << " or_greater " << or_greater;

	timestamp = 0;
	memset(hash, 0, hashsize);
	int rc;

	// Level, or_greater
	if (dblog(sqlite3_bind_int64(Commit_Roots_select, 1, level))) return -1;
	if (dblog(sqlite3_bind_int(Commit_Roots_select, 2, or_greater))) return -1;

	if (dblog(rc = sqlite3_step(Commit_Roots_select), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelect simulating database error post-select";

		return -1;
	}

	if (rc == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsSelect select returned SQLITE_DONE";

		return 1;
	}

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelect select returned " << rc;

		return -1;
	}

	// Timestamp, MerkleRoot
	timestamp = sqlite3_column_int64(Commit_Roots_select, 0);
	auto merkleroot_blob = sqlite3_column_blob(Commit_Roots_select, 1);

	if (!merkleroot_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelect MerkleRoot is null";

		return -1;
	}
	else if (sqlite3_column_bytes(Commit_Roots_select, 1) != (int)hashsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelect MerkleRoot Data size " << sqlite3_column_bytes(Commit_Roots_select, 1) << " != " << hashsize;

		return -1;
	}

	memcpy(hash, merkleroot_blob, hashsize);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelect simulating database error post-error check";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelect level " << level << " returning timestamp " << timestamp << " root " << buf2hex(hash, hashsize);

	return 0;
}

int DbConnPersistData::TxOutputsInsert(const void *addr, unsigned addrsize, uint64_t value_enc, uint64_t param_level, const void *commitment, unsigned commitsize, uint64_t commitnum)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsInsert address " << buf2hex(addr, addrsize) << " value_enc " << value_enc << " param_level " << param_level << " commitment " << buf2hex(commitment, commitsize) << " commitnum " << commitnum;

	// Address, ValueEnc, ParamLevel, Commitment, Commitnum
	if (dblog(sqlite3_bind_blob(Tx_Outputs_insert, 1, addr, addrsize, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 2, value_enc))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 3, param_level))) return -1;
	if (dblog(sqlite3_bind_blob(Tx_Outputs_insert, 4, commitment, commitsize, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 5, commitnum))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputsInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Tx_Outputs_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsInsert sqlite3_changes " << changes << " after insert into TxOutputs address " << buf2hex(addr, addrsize) << " value_enc " << value_enc << " param_level " << param_level << " commitment " << buf2hex(commitment, commitsize) << " commitnum " << commitnum;

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::TxOutputsInsert inserted into TxOutputs address " << buf2hex(addr, addrsize) << " value_enc " << value_enc << " param_level " << param_level << " commitment " << buf2hex(commitment, commitsize) << " commitnum " << commitnum;

	return 0;
}

int DbConnPersistData::TxOutputsSelect(const void *addr, unsigned addrsize, uint64_t commitnum_start, uint64_t commitnum_end, uint64_t *value_enc, char *commitment_iv, unsigned commitment_ivsize, char *commitment, unsigned commitsize, uint64_t *commitnums, unsigned limit, bool *have_more)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect address " << buf2hex(addr, addrsize) << " commitnum_start " << commitnum_start << " commitnum_end " << commitnum_end << " limit " << limit;

	CCASSERT(limit);

	unsigned nfound = 0;
	if (have_more) *have_more = false;
	int rc;

	// Address, Commitnum start, Commitnum end, limit
	if (dblog(sqlite3_bind_blob(Tx_Outputs_select, 1, addr, addrsize, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_select, 2, commitnum_start))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_select, 3, commitnum_end))) return -1;
	if (dblog(sqlite3_bind_int(Tx_Outputs_select, 4, limit < 2 ? limit : limit + 1))) return -1;

	if (dblog(rc = sqlite3_step(Tx_Outputs_select), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputsSelect simulating database error post-select";

		return -1;
	}

	if (rc == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect not found";

		return nfound;
	}

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect select returned " << rc;

		return -1;
	}

	// ValueEnc, MerkleRoot, Commitment, Commitnum
	value_enc[nfound] = sqlite3_column_int64(Tx_Outputs_select, 0);
	auto merkleroot_blob = sqlite3_column_blob(Tx_Outputs_select, 1);
	auto commit_blob = sqlite3_column_blob(Tx_Outputs_select, 2);
	commitnums[nfound] = sqlite3_column_int64(Tx_Outputs_select, 3);

	if (!merkleroot_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect MerkleRoot is null";

		return -1;
	}
	else if (sqlite3_column_bytes(Tx_Outputs_select, 1) != (int)commitment_ivsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect MerkleRoot Data size " << sqlite3_column_bytes(Tx_Outputs_select, 1) << " != " << commitment_ivsize;

		return -1;
	}

	if (!commit_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect Commitment is null";

		return -1;
	}
	else if (sqlite3_column_bytes(Tx_Outputs_select, 2) != (int)commitsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect Commitment Data size " << sqlite3_column_bytes(Tx_Outputs_select, 2) << " != " << commitsize;

		return -1;
	}

	memcpy(commitment_iv + nfound * commitment_ivsize, merkleroot_blob, commitment_ivsize);
	memcpy(commitment + nfound * commitsize, commit_blob, commitsize);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputsSelect simulating database error post-error check";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect address " << buf2hex(addr, addrsize) << " value_enc " << value_enc[nfound] << " commitment_iv " << buf2hex(commitment_iv + nfound * commitment_ivsize, commitment_ivsize) << " commitment " << buf2hex(commitment + nfound * commitsize, commitsize) << " commitnum " << commitnums[nfound];

	nfound++;

	return nfound;
}

void DbConnPersistData::TestConcurrency()
{
	auto dbconnR = new DbConn;
	auto dbconnW = new DbConn;

	int rc;

	BOOST_LOG_TRIVIAL(info) << " ---- ...first with no read tx...";
	int serial = 22;

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->BeginWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " insert result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->EndWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- EndWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERT(rc);

	BOOST_LOG_TRIVIAL(info) << " ---- ...now with a read tx then write...";
	serial = 33;

	rc = dbconnR->BeginRead();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginRead result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->BeginWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " insert result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->EndWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- EndWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->EndRead();
	BOOST_LOG_TRIVIAL(info) << " ---- EndRead result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERT(rc);

	BOOST_LOG_TRIVIAL(info) << " ---- ...now with a write tx then read...";
	serial = 44;

	rc = dbconnW->BeginWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->BeginRead();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginRead result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " insert result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->EndWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- EndWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->EndRead();
	BOOST_LOG_TRIVIAL(info) << " ---- EndRead result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumCheck(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERT(rc);

	//delete dbconnR;
	//delete dbconnW;
}