/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * dbconn-persistent.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"
#include "block.hpp"
#include "dbparamkeys.h"

#include <dblog.h>
#include <CCobjects.hpp>

#define TRACE_DBCONN	(g_params.trace_persistent_db)

//!#define TEST_FOR_TIMING_ERROR	1

#ifndef TEST_FOR_TIMING_ERROR
#define TEST_FOR_TIMING_ERROR	0	// don't test
#endif

static mutex Persistent_db_write_mutex;		// since db is in WAL mode, this mutex is used only as a write-lock; !!! would it work without this?
static atomic<uint8_t> write_pending(0);
static atomic<thread::id> write_thread_id;

// note Persistent_db_write_mutex is shared by reference with DbConnPersistData::Persistent_Wal,
// so Persistent_db_write_mutex and DbConnPersistData::Persistent_Wal::Wal_db_mutex refer to the same mutex
WalDB DbConnPersistData::Persistent_Wal("PersistData", Persistent_db_write_mutex);

DbConnPersistData::DbConnPersistData()
{
	void ClearDbPointers();

	lock_guard<mutex> lock(Persistent_db_write_mutex);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::DbConnPersistData dbconn " << (uintptr_t)this;

	OpenDb();

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "begin;", -1, &Persistent_Data_begin_read, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "rollback;", -1, &Persistent_Data_rollback, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "begin exclusive;", -1, &Persistent_Data_begin_write, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "commit;", -1, &Persistent_Data_commit, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert or replace into Parameters (Key, Subkey, Value) values (?1, ?2, ?3);", -1, &Parameters_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert or replace into Parameters select ?1 as Key, ?2 as Subkey, 1 as Value union select Key, Subkey, Value+1 from Parameters where Key = ?1 and Subkey = ?2 order by Value desc limit 1;", -1, &Parameters_increment, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Value from Parameters where Key = ?1 and Subkey = ?2;", -1, &Parameters_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Blockchain (Level, Block) values (?1, ?2);", -1, &Blockchain_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select max(Level) from Blockchain;", -1, &Blockchain_select_max, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Block from Blockchain where Level = ?1;", -1, &Blockchain_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Serialnums (Serialnum, HashKey) values (?1, ?2);", -1, &Serialnum_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select HashKey from Serialnums where Serialnum = ?1;", -1, &Serialnum_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert or replace into Commit_Tree (Height, Offset, Data) values (?1, ?2, ?3);", -1, &Commit_Tree_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Data from Commit_Tree where Height = ?1 and Offset = ?2;", -1, &Commit_Tree_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Commit_Roots (Level, Timestamp, NextCommitnum, MerkleRoot) values (?1, ?2, ?3, ?4);", -1, &Commit_Roots_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Level, Timestamp, NextCommitnum, MerkleRoot from Commit_Roots where Level >= ?1 order by Level limit 1;", -1, &Commit_Roots_Level_select, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Level, Timestamp, MerkleRoot from Commit_Roots where NextCommitnum > ?1 order by NextCommitnum desc limit 1;", -1, &Commit_Roots_Commitnum_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Tx_Outputs (Address, Pool, AssetEnc, AmountEnc, ParamLevel, Commitnum) values (?1, ?2, ?3, ?4, ?5, ?6);", -1, &Tx_Outputs_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Pool, AssetEnc, AmountEnc, MerkleRoot, Data as Commitment, Commitnum from Tx_Outputs, Commit_Roots, Commit_Tree where Level = ParamLevel and Height = 0 and Offset = Commitnum and Address = ?1 and Commitnum >= ?2 order by Commitnum limit ?3;", -1, &Tx_Outputs_select, NULL)));

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
	DbFinalize(Parameters_increment, explain);
	DbFinalize(Blockchain_insert, explain);
	DbFinalize(Blockchain_select_max, explain);
	DbFinalize(Blockchain_select, explain);
	DbFinalize(Serialnum_insert, explain);
	DbFinalize(Serialnum_select, explain);
	DbFinalize(Commit_Tree_insert, explain);
	DbFinalize(Commit_Tree_select, explain);
	DbFinalize(Commit_Roots_insert, explain);
	DbFinalize(Commit_Roots_Level_select, explain);
	DbFinalize(Commit_Roots_Commitnum_select, explain);
	DbFinalize(Tx_Outputs_insert, explain);
	DbFinalize(Tx_Outputs_select, explain);

	explain = false;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::~DbConnPersistData done dbconn " << (uintptr_t)this;
}

void DbConnPersistData::DoPersistentDataFinish()
{
	if (RandTest(TEST_DELAY_DB_RESET)) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::DoPersistentDataFinish dbconn " << uintptr_t(this);

	sqlite3_reset(Parameters_insert);
	sqlite3_reset(Parameters_select);
	sqlite3_reset(Parameters_increment);
	sqlite3_reset(Blockchain_insert);
	sqlite3_reset(Blockchain_select_max);
	sqlite3_reset(Blockchain_select);
	sqlite3_reset(Serialnum_insert);
	sqlite3_reset(Serialnum_select);
	sqlite3_reset(Commit_Tree_insert);
	sqlite3_reset(Commit_Tree_select);
	sqlite3_reset(Commit_Roots_insert);
	sqlite3_reset(Commit_Roots_Level_select);
	sqlite3_reset(Commit_Roots_Commitnum_select);
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

	if (dbresult(rc))
		return -1;

	return 0;
}

int DbConnPersistData::EndRead()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::EndRead";

	auto rc = dblog(sqlite3_step(Persistent_Data_rollback), DB_STMT_STEP);

	DoPersistentDataFinish();

	if (dbresult(rc))
		return -1;

	return 0;
}

bool DbConnPersistData::ThisThreadHoldsMutex()
{
	return write_pending && this_thread::get_id() == write_thread_id;
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

	if (dbresult(rc) == SQLITE_BUSY)	// this can happen during testing if sqlite3_busy_timeout is enabled
	{
		usleep(1000);
		goto retry;
	}

	if (dbresult(rc))
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginWrite releasing mutex";

		Persistent_db_write_mutex.unlock();

		return -1;
	}

	if (TEST_FOR_TIMING_ERROR && (rand() & 0x7F))	// try multiple times to make sure there are no timing errors
	{
		EndRead();
		goto retry;
	}

	write_pending = true;
	write_thread_id = this_thread::get_id();

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

	if (dbresult(rc) || !commit)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::EndWrite releasing mutex";

		Persistent_db_write_mutex.unlock();
	}

	if (dbresult(rc))
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

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::ParameterInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Parameters_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterInsert sqlite3_changes " << changes << " after insert key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::ParameterInsert inserted key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

	return 0;
}

int DbConnPersistData::ParameterIncrement(int key, int subkey)
{
	//CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterIncrement key " << key << " subkey " << subkey;

	// Key, Subkey
	if (dblog(sqlite3_bind_int(Parameters_increment, 1, key))) return -1;
	if (dblog(sqlite3_bind_int(Parameters_increment, 2, subkey))) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::ParameterIncrement simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Parameters_increment);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterIncrement sqlite3_changes " << changes << " after increment key " << key << " subkey " << subkey;

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::ParameterIncrement incremented key " << key << " subkey " << subkey;

	return 0;
}

int DbConnPersistData::ParameterSelect(int key, int subkey, void *value, unsigned bufsize, bool add_terminator, unsigned *retsize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " bufsize " << bufsize << " add_terminator " << add_terminator;

	memset(value, 0, bufsize);
	if (retsize)
		*retsize = 0;

	int rc;

	// Key, Subkey
	if (dblog(sqlite3_bind_int(Parameters_select, 1, key))) return -1;
	if (dblog(sqlite3_bind_int(Parameters_select, 2, subkey))) return -1;

	if (dblog(rc = sqlite3_step(Parameters_select), DB_STMT_SELECT)) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::ParameterSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::ParameterSelect select key " << key << " subkey " << subkey << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect select key " << key << " subkey " << subkey << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Parameters_select) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect select returned " << sqlite3_data_count(Parameters_select) << " columns";

		return -1;
	}

	// Value
	auto data_blob = sqlite3_column_blob(Parameters_select, 0);
	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect Data is null";

		return -1;
	}

	unsigned datasize = sqlite3_column_bytes(Parameters_select, 0);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::ParameterSelect simulating database error post-error check";

		return -1;
	}

	if (datasize + add_terminator > bufsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " data size " << datasize << " > " << bufsize;

		return -1;
	}

	if (!add_terminator && !retsize && datasize != bufsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " data size " << datasize << " != " << bufsize;

		return -1;
	}

	memcpy(value, data_blob, datasize);		// terminator added by initial memset

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

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Blockchain_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainInsert sqlite3_changes " << changes << " after insert level " << level << " obj tag " << obj->ObjTag() << " obj size " << objsize << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::BlockchainInsert inserted level " << level << " obj tag " << obj->ObjTag() << " obj size " << objsize << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	return 0;
}

int DbConnPersistData::BlockchainSelect(uint64_t level, SmartBuf *retobj)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelect level " << level;

	retobj->ClearRef();

	int rc;

	// Level
	if (dblog(sqlite3_bind_int64(Blockchain_select, 1, level))) return -1;

	if (dblog(rc = sqlite3_step(Blockchain_select), DB_STMT_SELECT)) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::BlockchainSelect select level " << level << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect select level " << level << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Blockchain_select) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect select returned " << sqlite3_data_count(Blockchain_select) << " columns";

		return -1;
	}

	// Block
	auto data_blob = sqlite3_column_blob(Blockchain_select, 0);
	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect Data is null";

		return -1;
	}

	unsigned datasize = sqlite3_column_bytes(Blockchain_select, 0);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(TEST_RANDOM_DB_ERRORS))
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
	if (wire->level.GetValue() != level)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect data level " << wire->level.GetValue() << " != " << level;

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

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelectMax simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelectMax select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Blockchain_select_max) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelectMax select returned " << sqlite3_data_count(Blockchain_select_max) << " columns";

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

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelectMax simulating database error post-error check";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelectMax level " << level;

	return 0;
}

int DbConnPersistData::SerialnumInsert(const void *serialnum, unsigned serialnum_size, const void *hashkey, unsigned hashkey_size)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::SerialnumInsert serialnum " << buf2hex(serialnum, serialnum_size) << " hashkey " << buf2hex(hashkey, hashkey_size);

	// Serialnum, HashKey
	if (dblog(sqlite3_bind_blob(Serialnum_insert, 1, serialnum, serialnum_size, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Serialnum_insert, 2, hashkey, hashkey_size, SQLITE_STATIC))) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::SerialnumInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Serialnum_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumInsert sqlite3_changes " << changes << " after insert serialnum " << buf2hex(serialnum, serialnum_size) << " hashkey " << buf2hex(hashkey, hashkey_size);

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::SerialnumInsert inserted serialnum " << buf2hex(serialnum, serialnum_size) << " hashkey " << buf2hex(hashkey, hashkey_size);

	return 0;
}

int DbConnPersistData::SerialnumSelect(const void *serialnum, unsigned serialnum_size, void *hashkey, unsigned *hashkey_size)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::SerialnumSelect serialnum " << buf2hex(serialnum, serialnum_size);

	unsigned keysize = 0;

	if (hashkey)
	{
		CCASSERT(hashkey_size);

		memset(hashkey, 0, *hashkey_size);

		keysize = *hashkey_size;
		*hashkey_size = 0;
	}

	int rc;

	// Serialnum
	if (dblog(sqlite3_bind_blob(Serialnum_select, 1, serialnum, serialnum_size, SQLITE_STATIC))) return -1;

	if (dblog(rc = sqlite3_step(Serialnum_select), DB_STMT_SELECT)) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::SerialnumSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::SerialnumSelect select serialnum " << buf2hex(serialnum, serialnum_size) << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumSelect select serialnum " << buf2hex(serialnum, serialnum_size) << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Serialnum_select) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumSelect select returned " << sqlite3_data_count(Serialnum_select) << " columns";

		return -1;
	}

	// Block
	auto data_blob = sqlite3_column_blob(Serialnum_select, 0);
	unsigned datasize = sqlite3_column_bytes(Serialnum_select, 0);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::SerialnumSelect simulating database error post-error check";

		return -1;
	}

	if (hashkey && data_blob && datasize > keysize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumSelect data size " << datasize << " > " << keysize;

		return -1;
	}

	if (hashkey && data_blob && datasize)
	{
		memcpy(hashkey, data_blob, datasize);
		*hashkey_size = datasize;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::SerialnumSelect serialnum " << buf2hex(serialnum, serialnum_size) << " returning " << (hashkey_size && *hashkey_size ? buf2hex(hashkey, *hashkey_size) : "found");

	return 0;
}

int DbConnPersistData::CommitTreeInsert(unsigned height, uint64_t offset, const void *data, unsigned datasize)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitTreeInsert height " << height << " offset " << offset << " data " << buf2hex(data, datasize);

	// Height, Offset, Data
	if (dblog(sqlite3_bind_int(Commit_Tree_insert, 1, height))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Tree_insert, 2, offset))) return -1;
	if (dblog(sqlite3_bind_blob(Commit_Tree_insert, 3, data, datasize, SQLITE_STATIC))) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitTreeInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Commit_Tree_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeInsert sqlite3_changes " << changes << " after insert height " << height << " offset " << offset << " data " << buf2hex(data, datasize);

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitTreeInsert inserted height " << height << " offset " << offset << " data " << buf2hex(data, datasize);

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

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitTreeSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::CommitTreeSelect select height " << height << " offset " << offset << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect select height " << height << " offset " << offset << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Commit_Tree_select) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect select returned " << sqlite3_data_count(Commit_Tree_select) << " columns";

		return -1;
	}

	// Data
	auto data_blob = sqlite3_column_blob(Commit_Tree_select, 0);
	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect Data is null";

		return -1;
	}
	else if ((unsigned)sqlite3_column_bytes(Commit_Tree_select, 0) != datasize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect Data size " << sqlite3_column_bytes(Commit_Tree_select, 0) << " != " << datasize;

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitTreeSelect simulating database error post-error check";

		return -1;
	}

	memcpy(data, data_blob, datasize);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitTreeSelect height " << height << " offset " << offset << " returning " << buf2hex(data, datasize);

	return 0;
}

int DbConnPersistData::CommitRootsInsert(uint64_t level, uint64_t timestamp, uint64_t next_commitnum, const void *hash, unsigned hashsize)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsInsert level " << level << " timestamp " << timestamp << " next_commitnum " << next_commitnum << " hash " << buf2hex(hash, hashsize);

	// Level, Timestamp, NextCommitnum, MerkleRoot
	if (dblog(sqlite3_bind_int64(Commit_Roots_insert, 1, level))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Roots_insert, 2, timestamp))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Roots_insert, 3, next_commitnum))) return -1;
	if (dblog(sqlite3_bind_blob(Commit_Roots_insert, 4, hash, hashsize, SQLITE_STATIC))) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Commit_Roots_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsInsert sqlite3_changes " << changes << " after insert level " << level << " timestamp " << timestamp << " next_commitnum " << next_commitnum << " hash " << buf2hex(hash, hashsize);

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsInsert inserted level " << level << " timestamp " << timestamp << " next_commitnum " << next_commitnum << " hash " << buf2hex(hash, hashsize);

	return 0;
}

int DbConnPersistData::CommitRootsSelectLevel(uint64_t level, bool or_greater, uint64_t& timestamp, uint64_t& next_commitnum, void *hash, unsigned hashsize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelectLevel level " << level << " or_greater = " << or_greater;

	timestamp = 0;
	next_commitnum = 0;
	memset(hash, 0, hashsize);
	int rc;

	// Level
	if (dblog(sqlite3_bind_int64(Commit_Roots_Level_select, 1, level))) return -1;

	if (dblog(rc = sqlite3_step(Commit_Roots_Level_select), DB_STMT_SELECT)) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelectLevel simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsSelectLevel select level " << level << " or_greater = " << or_greater << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectLevel select level " << level << " or_greater = " << or_greater << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Commit_Roots_Level_select) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectLevel select returned " << sqlite3_data_count(Commit_Roots_Level_select) << " columns";

		return -1;
	}

	// Level, Timestamp, NextCommitnum, MerkleRoot
	uint64_t _level = sqlite3_column_int64(Commit_Roots_Level_select, 0);

	if (_level != level && !or_greater)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsSelectLevel select level " << level << " or_greater = " << or_greater << " found " << _level << " returning SQLITE_DONE";

		return 1;
	}

	timestamp = sqlite3_column_int64(Commit_Roots_Level_select, 1);
	next_commitnum = sqlite3_column_int64(Commit_Roots_Level_select, 2);
	auto merkleroot_blob = sqlite3_column_blob(Commit_Roots_Level_select, 3);

	if (!merkleroot_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectLevel MerkleRoot is null";

		return -1;
	}
	else if ((unsigned)sqlite3_column_bytes(Commit_Roots_Level_select, 3) != hashsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectLevel MerkleRoot Data size " << sqlite3_column_bytes(Commit_Roots_Level_select, 3) << " != " << hashsize;

		return -1;
	}

	memcpy(hash, merkleroot_blob, hashsize);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelectLevel simulating database error post-error check";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelectLevel level " << level << " returning timestamp " << timestamp << " next_commitnum " << next_commitnum << " root " << buf2hex(hash, hashsize);

	return 0;
}

int DbConnPersistData::CommitRootsSelectCommitnum(uint64_t commitnum, uint64_t& level, uint64_t& timestamp, void *hash, unsigned hashsize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelectCommitnum commitnum " << commitnum;

	level = 0;
	timestamp = 0;
	memset(hash, 0, hashsize);
	int rc;

	// Commitnum
	if (dblog(sqlite3_bind_int64(Commit_Roots_Commitnum_select, 1, commitnum))) return -1;

	if (dblog(rc = sqlite3_step(Commit_Roots_Commitnum_select), DB_STMT_SELECT)) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelectCommitnum simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsSelectCommitnum select commitnum " << commitnum << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectCommitnum select commitnum " << commitnum << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Commit_Roots_Commitnum_select) != 3)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectCommitnum select returned " << sqlite3_data_count(Commit_Roots_Commitnum_select) << " columns";

		return -1;
	}

	// Level, Timestamp, MerkleRoot
	level = sqlite3_column_int64(Commit_Roots_Commitnum_select, 0);
	timestamp = sqlite3_column_int64(Commit_Roots_Commitnum_select, 1);
	auto merkleroot_blob = sqlite3_column_blob(Commit_Roots_Commitnum_select, 2);

	if (!merkleroot_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectCommitnum MerkleRoot is null";

		return -1;
	}
	else if ((unsigned)sqlite3_column_bytes(Commit_Roots_Commitnum_select, 2) != hashsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectCommitnum MerkleRoot Data size " << sqlite3_column_bytes(Commit_Roots_Commitnum_select, 2) << " != " << hashsize;

		return -1;
	}

	memcpy(hash, merkleroot_blob, hashsize);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelectCommitnum simulating database error post-error check";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelectCommitnum commitnum " << commitnum << " returning level " << level << " timestamp " << timestamp << " root " << buf2hex(hash, hashsize);

	return 0;
}

int DbConnPersistData::TxOutputsInsert(const void *addr, unsigned addrsize, uint32_t pool, uint64_t asset_enc, uint64_t amount_enc, uint64_t param_level, uint64_t commitnum)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsInsert address " << buf2hex(addr, addrsize) << " pool " << pool << " asset_enc " << asset_enc << " amount_enc " << amount_enc << " param_level " << param_level << " commitnum " << commitnum;

	// Address, Pool, AssetEnc, AmountEnc, ParamLevel, Commitnum
	if (dblog(sqlite3_bind_blob(Tx_Outputs_insert, 1, addr, addrsize, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Tx_Outputs_insert, 2, pool))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 3, asset_enc))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 4, amount_enc))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 5, param_level))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 6, commitnum))) return -1;

	if (RandTest(TEST_RANDOM_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputsInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Tx_Outputs_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsInsert sqlite3_changes " << changes << " after insert address " << buf2hex(addr, addrsize) << " pool " << pool << " asset_enc " << asset_enc << " amount_enc " << amount_enc << " param_level " << param_level << " commitnum " << commitnum;

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::TxOutputsInsert inserted address " << buf2hex(addr, addrsize) << " pool " << pool << " asset_enc " << asset_enc << " amount_enc " << amount_enc << " param_level " << param_level << " commitnum " << commitnum;

	return 0;
}

int DbConnPersistData::TxOutputsSelect(const void *addr, unsigned addrsize, uint64_t commitnum_start, uint32_t *pool, uint64_t *asset_enc, uint64_t *amount_enc, char *commitiv, unsigned ivsize, char *commitment, unsigned commitsize, uint64_t *commitnum, unsigned limit, bool *have_more)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect address " << buf2hex(addr, addrsize) << " commitnum_start " << commitnum_start << " limit " << limit;

	CCASSERT(limit);

	// Address, Commitnum start, limit
	if (dblog(sqlite3_bind_blob(Tx_Outputs_select, 1, addr, addrsize, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_select, 2, commitnum_start))) return -1;
	if (dblog(sqlite3_bind_int(Tx_Outputs_select, 3, limit < 2 ? limit : limit + 1))) return -1;

	unsigned nfound = 0;
	if (have_more)
		*have_more = false;

	while (true)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Tx_Outputs_select), DB_STMT_SELECT)) return -1;

		if (RandTest(TEST_RANDOM_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputsSelect simulating database error post-select";

			return -1;
		}

		if (dbresult(rc) == SQLITE_DONE)
		{
			BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect not found";

			break;
		}

		if (dbresult(rc) != SQLITE_ROW)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect select returned " << rc;

			return -1;
		}

		if (nfound >= limit)
		{
			if (have_more)
				*have_more = true;

			break;
		}

		if (sqlite3_data_count(Tx_Outputs_select) != 6)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect select returned " << sqlite3_data_count(Tx_Outputs_select) << " columns";

			return -1;
		}

		// Pool, AssetEnc, AmountEnc, MerkleRoot, Commitment, Commitnum
		pool[nfound] = sqlite3_column_int(Tx_Outputs_select, 0);
		asset_enc[nfound] = sqlite3_column_int64(Tx_Outputs_select, 1);
		amount_enc[nfound] = sqlite3_column_int64(Tx_Outputs_select, 2);
		auto commitiv_blob = sqlite3_column_blob(Tx_Outputs_select, 3);
		auto commit_blob = sqlite3_column_blob(Tx_Outputs_select, 4);
		commitnum[nfound] = sqlite3_column_int64(Tx_Outputs_select, 5);

		if (!commitiv_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect CommitIV is null";

			return -1;
		}
		else if ((unsigned)sqlite3_column_bytes(Tx_Outputs_select, 3) < ivsize)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect CommitIV Data size " << sqlite3_column_bytes(Tx_Outputs_select, 3) << " < " << ivsize;

			return -1;
		}

		if (!commit_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect Commitment is null";

			return -1;
		}
		else if ((unsigned)sqlite3_column_bytes(Tx_Outputs_select, 4) != commitsize)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect Commitment Data size " << sqlite3_column_bytes(Tx_Outputs_select, 4) << " != " << commitsize;

			return -1;
		}

		memcpy(commitiv + nfound * ivsize, commitiv_blob, ivsize);
		memcpy(commitment + nfound * commitsize, commit_blob, commitsize);

		if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

		if (RandTest(TEST_RANDOM_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputsSelect simulating database error post-error check";

			return -1;
		}

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect address " << buf2hex(addr, addrsize) << " pool " << pool[nfound] << " asset_enc " << asset_enc[nfound] << " amount_enc " << amount_enc[nfound] << " commitiv " << buf2hex(commitiv + nfound * ivsize, ivsize) << " commitment " << buf2hex(commitment + nfound * commitsize, commitsize) << " commitnum " << commitnum[nfound];

		nfound++;
	}

	return nfound;
}

void DbConnPersistData::TestConcurrency()
{
	auto dbconnR = new DbConn;
	auto dbconnW = new DbConn;

	const char hashkey = 0;

	int rc;

	BOOST_LOG_TRIVIAL(info) << " ---- ...first with no read tx...";
	int serial = 22;

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERT(rc > 0);

	rc = dbconnW->BeginWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial), &hashkey, sizeof(hashkey));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " insert result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->EndWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- EndWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	BOOST_LOG_TRIVIAL(info) << " ---- ...now with a read tx then write...";
	serial = 33;

	rc = dbconnR->BeginRead();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginRead result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->BeginWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial), &hashkey, sizeof(hashkey));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " insert result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->EndWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- EndWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->EndRead();
	BOOST_LOG_TRIVIAL(info) << " ---- EndRead result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	BOOST_LOG_TRIVIAL(info) << " ---- ...now with a write tx then read...";
	serial = 44;

	rc = dbconnW->BeginWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->BeginRead();
	BOOST_LOG_TRIVIAL(info) << " ---- BeginRead result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial), &hashkey, sizeof(hashkey));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " insert result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnW->EndWrite();
	BOOST_LOG_TRIVIAL(info) << " ---- EndWrite result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->EndRead();
	BOOST_LOG_TRIVIAL(info) << " ---- EndRead result " << rc;
	CCASSERTZ(rc);

	rc = dbconnR->SerialnumSelect(&serial, sizeof(serial));
	BOOST_LOG_TRIVIAL(info) << " ---- serial " << serial << " check result " << rc;
	CCASSERTZ(rc);

	//delete dbconnR;
	//delete dbconnW;
}