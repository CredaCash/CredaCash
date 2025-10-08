/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * dbconn-persistent.cpp
*/

#include "ccnode.h"
#include "dbconn.hpp"
#include "block.hpp"
#include "dbparamkeys.h"

#include <dblog.h>
#include <CCobjects.hpp>
#include <transaction.h>
#include <xmatch.hpp>
#include <amounts.h>

#define TRACE_DB_READS	(g_params.trace_persistent_db_reads)
#define TRACE_DB_WRITES	(g_params.trace_persistent_db_writes)

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

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::DbConnPersistData dbconn " << (uintptr_t)this;

	OpenDb();

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "begin;", -1, &Persistent_Data_begin_read, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "rollback;", -1, &Persistent_Data_rollback, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "begin exclusive;", -1, &Persistent_Data_begin_write, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "commit;", -1, &Persistent_Data_commit, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert or replace into Parameters (Key, Subkey, Value) values (?1, ?2, ?3);", -1, &Parameters_insert, NULL)));
	//CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert or replace into Parameters select Key, Subkey, Value+1 from Parameters where Key = ?1 and Subkey = ?2 union all select ?1 as Key, ?2 as Subkey, 1 as Value order by Value desc limit 1;", -1, &Parameters_increment, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Parameters (Key, Subkey, Value) values (?1, ?2, 1) on conflict (Key, Subkey) do update set Value = Value + 1;", -1, &Parameters_increment, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Value from Parameters where Key = ?1 and Subkey = ?2;", -1, &Parameters_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Blockchain (Level, Block) values (?1, ?2);", -1, &Blockchain_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select max(Level) from Blockchain;", -1, &Blockchain_select_max, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Block from Blockchain where Level = ?1;", -1, &Blockchain_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Serialnums (Serialnum, HashKey, TxCommitnum) values (?1, ?2, ?3);", -1, &Serialnum_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select HashKey, TxCommitnum from Serialnums where Serialnum = ?1;", -1, &Serialnum_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert or replace into Commit_Tree (Height, Offset, Data) values (?1, ?2, ?3);", -1, &Commit_Tree_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Data from Commit_Tree where Height = ?1 and Offset = ?2;", -1, &Commit_Tree_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Commit_Roots (Level, Timestamp, NextCommitnum, MerkleRoot) values (?1, ?2, ?3, ?4);", -1, &Commit_Roots_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Level, Timestamp, NextCommitnum, MerkleRoot from Commit_Roots where Level >= ?1 order by Level limit 1;", -1, &Commit_Roots_select_level, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Level, Timestamp, NextCommitnum, MerkleRoot from Commit_Roots where Level <= ?1 order by Level desc limit 1;", -1, &Commit_Roots_select_level_last, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Level, Timestamp, MerkleRoot from Commit_Roots where NextCommitnum > ?1 order by NextCommitnum limit 1;", -1, &Commit_Roots_select_next_commitnum, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Tx_Outputs (Address, Domain, AssetEnc, AmountEnc, ParamLevel, Commitnum) values (?1, ?2, ?3, ?4, ?5, ?6);", -1, &Tx_Outputs_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Domain, AssetEnc, AmountEnc, MerkleRoot, Data as Commitment, Commitnum from Tx_Outputs, Commit_Roots, Commit_Tree where Level = ParamLevel and Height = 0 and Offset = Commitnum and Address = ?1 and Commitnum >= ?2 order by Commitnum limit ?3;", -1, &Tx_Outputs_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Exchange_Nums (Level, Timestamp, NextXreqnum, NextXmatchnum) values (?1, ?2, ?3, ?4);", -1, &Xcx_Nums_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Level, Timestamp, NextXreqnum, NextXmatchnum from Exchange_Nums where Level <= ?1 order by Level desc limit 1;", -1, &Xcx_Nums_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Exchange_Matches values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17) "
		"on conflict(Xmatchnum) do update set BaseAmount = excluded.BaseAmount, Rate = excluded.Rate, AmountPaid = excluded.AmountPaid, MiningAmount = excluded.MiningAmount, Status = excluded.Status, NextDeadline = excluded.NextDeadline, AcceptTimestamp = excluded.AcceptTimestamp, FinalTimestamp = excluded.FinalTimestamp;", -1, &Xcx_Match_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Exchange_Match_Reqs values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25) "
		"on conflict(Xreqnum) do update set Disposition = excluded.Disposition;", -1, &Xcx_Match_Reqs_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "update Exchange_Match_Reqs set Disposition = ?2 where Xreqnum = ?1 and Xreqnum != 0;", -1, &Xcx_Match_Reqs_update, NULL)));
	// note: need to be very careful about changing DeleteTime, because ForeignAddress conflicts affect crosschain sell req validity, and therefore block validity
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "insert into Exchange_Matching_Reqs values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
		"on conflict(Xreqnum) do update set OpenAmount = excluded.OpenAmount, DeleteTime = max(DeleteTime, excluded.DeleteTime);", -1, &Xcx_Matching_Reqs_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "delete from Exchange_Matching_Reqs where DeleteTime < ?1;", -1, &Xcx_Matching_Reqs_prune, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select * from Exchange_Match_Reqs inner join Exchange_Matching_Reqs on Exchange_Match_Reqs.Xreqnum = Exchange_Matching_Reqs.Xreqnum "
		"where Exchange_Matching_Reqs.Xreqnum >= ?1 order by Exchange_Matching_Reqs.Xreqnum limit 1;", -1, &Xcx_Match_Reqs_select_matching, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select Xreqnum from Exchange_Match_Reqs where ObjId = ?1 and Xreqnum <= ?2 order by Xreqnum desc limit 1;", -1, &Xcx_Match_Reqs_select_objid_descending_reqnum, NULL)));
	const string select_sql = "select * from Exchange_Matches "
		"left join Exchange_Match_Reqs    as XBuy     on Exchange_Matches.BuyXreqnum  =     XBuy.Xreqnum "
		"left join Exchange_Matching_Reqs as XBuying  on Exchange_Matches.BuyXreqnum  =  XBuying.Xreqnum "
		"left join Exchange_Match_Reqs    as XSell    on Exchange_Matches.SellXreqnum =    XSell.Xreqnum "
		"left join Exchange_Matching_Reqs as XSelling on Exchange_Matches.SellXreqnum = XSelling.Xreqnum ";
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, (select_sql + "where Exchange_Matches.Xmatchnum >= ?1 order by Xmatchnum limit 1;").c_str(), -1, &Xcx_Match_select, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, (select_sql + "where (BuyXreqnum = ?1 or SellXreqnum = ?1) and Xmatchnum >= ?2 order by Xmatchnum limit 1;").c_str(), -1, &Xcx_Match_select_reqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, (select_sql + "where (NextDeadline < ?1) and NextDeadline > 0 order by NextDeadline, Xmatchnum limit 1;").c_str(), -1, &Xcx_Match_select_deadline, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select true from Exchange_Matching_Reqs where DeleteTime >= ?1 and QuoteAsset = ?2 and ForeignAddress = ?3 and ForeignAddressUnique and ForeignAddress is not null limit 1;", -1, &Xcx_Matching_Reqs_Foreign_Address_select, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Persistent_db, "select true from Exchange_Blocked_Foreign_Addresses where Blockchain = ?1 and ForeignAddress = ?2 limit 1;", -1, &Xcx_Blocked_Foreign_Address_select, NULL)));

	//if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::DbConnPersistData dbconn done " << (uintptr_t)this;
}

DbConnPersistData::~DbConnPersistData()
{
	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::~DbConnPersistData dbconn " << (uintptr_t)this;

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
	DbFinalize(Commit_Roots_select_level, explain);
	DbFinalize(Commit_Roots_select_level_last, explain);
	DbFinalize(Commit_Roots_select_next_commitnum, explain);
	DbFinalize(Tx_Outputs_insert, explain);
	DbFinalize(Tx_Outputs_select, explain);
	DbFinalize(Xcx_Nums_insert, explain);
	DbFinalize(Xcx_Nums_select, explain);
	DbFinalize(Xcx_Match_insert, explain);
	DbFinalize(Xcx_Match_Reqs_insert, explain);
	DbFinalize(Xcx_Match_Reqs_update, explain);
	DbFinalize(Xcx_Matching_Reqs_insert, explain);
	DbFinalize(Xcx_Matching_Reqs_prune, explain);
	DbFinalize(Xcx_Match_Reqs_select_matching, explain);
	DbFinalize(Xcx_Match_Reqs_select_objid_descending_reqnum, explain);
	DbFinalize(Xcx_Match_select, explain);
	DbFinalize(Xcx_Match_select_reqnum, explain);
	DbFinalize(Xcx_Match_select_deadline, explain);
	DbFinalize(Xcx_Matching_Reqs_Foreign_Address_select, explain);
	DbFinalize(Xcx_Blocked_Foreign_Address_select, explain);

	explain = false;

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::~DbConnPersistData done dbconn " << (uintptr_t)this;
}

void DbConnPersistData::DoPersistentDataFinish()
{
	if (RandTest(RTEST_DELAY_DB_RESET)) sleep(1);

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::DoPersistentDataFinish dbconn " << uintptr_t(this);

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
	sqlite3_reset(Commit_Roots_select_level);
	sqlite3_reset(Commit_Roots_select_level_last);
	sqlite3_reset(Commit_Roots_select_next_commitnum);
	sqlite3_reset(Tx_Outputs_insert);
	sqlite3_reset(Tx_Outputs_select);
	sqlite3_reset(Xcx_Nums_insert);
	sqlite3_reset(Xcx_Nums_select);
	sqlite3_reset(Xcx_Match_insert);
	sqlite3_reset(Xcx_Match_Reqs_insert);
	sqlite3_reset(Xcx_Match_Reqs_update);
	sqlite3_reset(Xcx_Matching_Reqs_insert);
	sqlite3_reset(Xcx_Matching_Reqs_prune);
	sqlite3_reset(Xcx_Match_Reqs_select_matching);
	sqlite3_reset(Xcx_Match_Reqs_select_objid_descending_reqnum);
	sqlite3_reset(Xcx_Match_select);
	sqlite3_reset(Xcx_Match_select_reqnum);
	sqlite3_reset(Xcx_Match_select_deadline);
	sqlite3_reset(Xcx_Matching_Reqs_Foreign_Address_select);
	sqlite3_reset(Xcx_Blocked_Foreign_Address_select);

	sqlite3_reset(Persistent_Data_begin_read);
	sqlite3_reset(Persistent_Data_rollback);
	sqlite3_reset(Persistent_Data_begin_write);
	sqlite3_reset(Persistent_Data_commit);
}

int DbConnPersistData::BeginRead()
{
	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginRead";

	auto rc = dblog(sqlite3_step(Persistent_Data_begin_read), DB_STMT_STEP);

	DoPersistentDataFinish();

	if (dbresult(rc))
		return -1;

	return 0;
}

int DbConnPersistData::EndRead()
{
	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::EndRead";

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

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginWrite acquiring mutex...";

	Persistent_db_write_mutex.lock();

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginWrite mutex acquired";

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
		if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BeginWrite releasing mutex";

		Persistent_db_write_mutex.unlock();

		return -1;
	}

	if (TEST_FOR_TIMING_ERROR && RandTest(128))	// try multiple times to make sure there are no timing errors
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

	if (g_shutdown)
		commit = false;	// abort in case required writes were skipped during shutdown

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::EndWrite commit " << commit;

	int rc;

	if (commit)
		rc = dblog(sqlite3_step(Persistent_Data_commit), DB_STMT_STEP);
	else
		rc = dblog(sqlite3_step(Persistent_Data_rollback), DB_STMT_STEP);

	DoPersistentDataFinish();

	if (dbresult(rc) || !commit)
		ReleaseMutex();

	if (dbresult(rc))
		return -1;

	return 0;
}

void DbConnPersistData::ReleaseMutex()
{
	if (!ThisThreadHoldsMutex())
		return;

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ReleaseMutex";

	write_pending = false;

	Persistent_db_write_mutex.unlock();
}

int DbConnPersistData::ParameterInsert(int key, int subkey, void *value, unsigned valsize)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterInsert key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

	// Key, Subkey, Value
	if (dblog(sqlite3_bind_int(Parameters_insert, 1, key))) return -1;
	if (dblog(sqlite3_bind_int(Parameters_insert, 2, subkey))) return -1;
	if (dblog(sqlite3_bind_blob(Parameters_insert, 3, value, valsize, SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
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

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::ParameterInsert inserted key " << key << " subkey " << subkey << " valsize " << valsize << " value " << buf2hex(value, (valsize < 16 ? valsize : 16));

	return 0;
}

int DbConnPersistData::ParameterIncrement(int key, int subkey)
{
	//CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterIncrement key " << key << " subkey " << subkey;

	// Key, Subkey
	if (dblog(sqlite3_bind_int(Parameters_increment, 1, key))) return -1;
	if (dblog(sqlite3_bind_int(Parameters_increment, 2, subkey))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
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

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::ParameterIncrement incremented key " << key << " subkey " << subkey;

	return 0;
}

int DbConnPersistData::ParameterSelect(int key, int subkey, void *value, unsigned bufsize, bool add_terminator, unsigned *retsize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " bufsize " << bufsize << " add_terminator " << add_terminator;

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
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::ParameterSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Parameters_select) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect returned " << sqlite3_data_count(Parameters_select) << " columns";

		return -1;
	}

	// Value
	auto data_blob = sqlite3_column_blob(Parameters_select, 0);
	unsigned datasize = sqlite3_column_bytes(Parameters_select, 0);

	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::ParameterSelect Data is null";

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
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

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::ParameterSelect key " << key << " subkey " << subkey << " returning obj size " << datasize << " value " << buf2hex(value, (datasize < 16 ? datasize : 16));

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

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainInsert level " << level << " obj tag " << hex << obj->ObjTag() << dec << " obj size " << objsize << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

	// Level, Block
	if (dblog(sqlite3_bind_int64(Blockchain_insert, 1, level))) return -1;
	if (dblog(sqlite3_bind_blob(Blockchain_insert, 2, obj->ObjPtr(), objsize, SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Blockchain_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainInsert sqlite3_changes " << changes << " after insert level " << level << " obj tag " << hex << obj->ObjTag() << dec << " obj size " << objsize << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

		return -1;
	}

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::BlockchainInsert inserted level " << level << " obj tag " << hex << obj->ObjTag() << dec << " obj size " << objsize << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

	return 0;
}

int DbConnPersistData::BlockchainSelect(uint64_t level, SmartBuf *retobj)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelect level " << level;

	retobj->ClearRef();

	int rc;

	// Level
	if (dblog(sqlite3_bind_int64(Blockchain_select, 1, level))) return -1;

	if (dblog(rc = sqlite3_step(Blockchain_select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::BlockchainSelect level " << level << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect level " << level << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Blockchain_select) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect returned " << sqlite3_data_count(Blockchain_select) << " columns";

		return -1;
	}

	// Block
	auto data_blob = sqlite3_column_blob(Blockchain_select, 0);
	unsigned datasize = sqlite3_column_bytes(Blockchain_select, 0);

	if (!data_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect Data is null";

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
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
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::BlockchainSelect tag " << hex << tag << dec << " != CC_TAG_BLOCK " << CC_TAG_BLOCK;

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

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelect level " << level << " returning obj size " << datasize;

	*retobj = smartobj;

	return 0;
}

int DbConnPersistData::BlockchainSelectMax(uint64_t& level)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelectMax";

	level = 0;

	int rc;

	if (dblog(rc = sqlite3_step(Blockchain_select_max), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
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

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::BlockchainSelectMax simulating database error post-error check";

		return -1;
	}

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::BlockchainSelectMax level " << level;

	return 0;
}

int DbConnPersistData::SerialnumInsert(const void *serialnum, unsigned serialnum_size, const void *hashkey, unsigned hashkey_size, uint64_t tx_commitnum)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::SerialnumInsert serialnum " << buf2hex(serialnum, serialnum_size) << " hashkey " << buf2hex(hashkey, hashkey_size) << " tx_commitnum " << tx_commitnum;

	// Serialnum, HashKey, TxCommitnum
	if (dblog(sqlite3_bind_blob(Serialnum_insert, 1, serialnum, serialnum_size, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Serialnum_insert, 2, hashkey, hashkey_size, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Serialnum_insert, 3, tx_commitnum))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::SerialnumInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Serialnum_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumInsert sqlite3_changes " << changes << " after insert serialnum " << buf2hex(serialnum, serialnum_size) << " hashkey " << buf2hex(hashkey, hashkey_size) << " tx_commitnum " << tx_commitnum;

		return -1;
	}

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::SerialnumInsert inserted serialnum " << buf2hex(serialnum, serialnum_size) << " hashkey " << buf2hex(hashkey, hashkey_size) << " tx_commitnum " << tx_commitnum;

	return 0;
}

int DbConnPersistData::SerialnumSelect(const void *serialnum, unsigned serialnum_size, void *hashkey, unsigned *hashkey_size, uint64_t *tx_commitnum)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::SerialnumSelect serialnum " << buf2hex(serialnum, serialnum_size);

	unsigned keysize = 0;

	if (hashkey)
	{
		CCASSERT(hashkey_size);

		memset(hashkey, 0, *hashkey_size);

		keysize = *hashkey_size;
		*hashkey_size = 0;
	}

	if (tx_commitnum)
		*tx_commitnum = 0;

	int rc;

	// Serialnum
	if (dblog(sqlite3_bind_blob(Serialnum_select, 1, serialnum, serialnum_size, SQLITE_STATIC))) return -1;

	if (dblog(rc = sqlite3_step(Serialnum_select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::SerialnumSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		//BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::SerialnumSelect serialnum " << buf2hex(serialnum, serialnum_size) << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumSelect serialnum " << buf2hex(serialnum, serialnum_size) << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Serialnum_select) != 2)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::SerialnumSelect returned " << sqlite3_data_count(Serialnum_select) << " columns";

		return -1;
	}

	// HashKey, TxCommitnum
	auto data_blob = sqlite3_column_blob(Serialnum_select, 0);
	unsigned datasize = sqlite3_column_bytes(Serialnum_select, 0);
	uint64_t commitnum = sqlite3_column_int64(Serialnum_select, 1);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
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

	if (tx_commitnum)
		*tx_commitnum = commitnum;

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::SerialnumSelect serialnum " << buf2hex(serialnum, serialnum_size) << " returning " << (hashkey_size && *hashkey_size ? buf2hex(hashkey, *hashkey_size) : "found") << " tx_commitnum " << commitnum;

	return 0;
}

int DbConnPersistData::CommitTreeInsert(unsigned height, uint64_t offset, const void *data, unsigned datasize)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitTreeInsert height " << height << " offset " << offset << " data " << buf2hex(data, datasize);

	// Height, Offset, Data
	if (dblog(sqlite3_bind_int(Commit_Tree_insert, 1, height))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Tree_insert, 2, offset))) return -1;
	if (dblog(sqlite3_bind_blob(Commit_Tree_insert, 3, data, datasize, SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
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

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitTreeInsert inserted height " << height << " offset " << offset << " data " << buf2hex(data, datasize);

	return 0;
}

int DbConnPersistData::CommitTreeSelect(unsigned height, uint64_t offset, void *data, unsigned datasize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitTreeSelect height " << height << " offset " << offset;

	memset(data, 0, datasize);

	int rc;

	// Height, Offset
	if (dblog(sqlite3_bind_int(Commit_Tree_select, 1, height))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Tree_select, 2, offset))) return -1;

	if (dblog(rc = sqlite3_step(Commit_Tree_select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitTreeSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::CommitTreeSelect height " << height << " offset " << offset << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect height " << height << " offset " << offset << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Commit_Tree_select) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitTreeSelect returned " << sqlite3_data_count(Commit_Tree_select) << " columns";

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

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitTreeSelect simulating database error post-error check";

		return -1;
	}

	memcpy(data, data_blob, datasize);

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitTreeSelect height " << height << " offset " << offset << " returning " << buf2hex(data, datasize);

	return 0;
}

int DbConnPersistData::CommitRootsInsert(uint64_t level, uint64_t timestamp, uint64_t next_commitnum, const void *hash, unsigned hashsize)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsInsert level " << level << " timestamp " << timestamp << " next_commitnum " << next_commitnum << " hash " << buf2hex(hash, hashsize);

	// Level, Timestamp, NextCommitnum, MerkleRoot
	if (dblog(sqlite3_bind_int64(Commit_Roots_insert, 1, level))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Roots_insert, 2, timestamp))) return -1;
	if (dblog(sqlite3_bind_int64(Commit_Roots_insert, 3, next_commitnum))) return -1;
	if (dblog(sqlite3_bind_blob(Commit_Roots_insert, 4, hash, hashsize, SQLITE_STATIC))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
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

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsInsert inserted level " << level << " timestamp " << timestamp << " next_commitnum " << next_commitnum << " hash " << buf2hex(hash, hashsize);

	return 0;
}

int DbConnPersistData::CommitRootsSelectLevel(uint64_t& level, int or_greater, uint64_t& timestamp, uint64_t& next_commitnum, void *hash, unsigned hashsize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelectLevel level " << level << " or_greater " << or_greater;

	timestamp = 0;
	next_commitnum = 0;
	memset(hash, 0, hashsize);

	int rc;

	auto select_stmt = Commit_Roots_select_level;
	if (or_greater < 0)
		select_stmt = Commit_Roots_select_level_last;

	// Level >=
	if (dblog(sqlite3_bind_int64(select_stmt, 1, level))) return -1;

	if (dblog(rc = sqlite3_step(select_stmt), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelectLevel simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsSelectLevel select level " << level << " or_greater " << or_greater << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectLevel select level " << level << " or_greater " << or_greater << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select_stmt) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectLevel select returned " << sqlite3_data_count(select_stmt) << " columns";

		return -1;
	}

	// Level, Timestamp, NextCommitnum, MerkleRoot
	uint64_t _level = sqlite3_column_int64(select_stmt, 0);

	if (_level != level && !or_greater)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::CommitRootsSelectLevel select level " << level << " or_greater " << or_greater << " found " << _level << " returning SQLITE_DONE";

		return 1;
	}

	auto merkleroot_blob = sqlite3_column_blob(select_stmt, 3);
	unsigned merkleroot_bytes = sqlite3_column_bytes(select_stmt, 3);

	if (!merkleroot_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectLevel MerkleRoot is null";

		return -1;
	}
	else if (merkleroot_bytes != hashsize && merkleroot_bytes != TX_COMMIT_IV_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectLevel MerkleRoot Data size " << sqlite3_column_bytes(select_stmt, 3) << " != " << hashsize;

		return -1;
	}

	memcpy(hash, merkleroot_blob, merkleroot_bytes);

	level = _level;
	timestamp = sqlite3_column_int64(select_stmt, 1);
	next_commitnum = sqlite3_column_int64(select_stmt, 2);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelectLevel simulating database error post-error check";

		return -1;
	}

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelectLevel returning level " << level << " timestamp " << timestamp << " next_commitnum " << next_commitnum << " root " << buf2hex(hash, hashsize);

	return 0;
}

int DbConnPersistData::CommitRootsSelectCommitnum(uint64_t commitnum, uint64_t& level, uint64_t& timestamp, void *hash, unsigned hashsize)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelectCommitnum commitnum " << commitnum;

	level = 0;
	timestamp = 0;
	memset(hash, 0, hashsize);

	int rc;

	// NextCommitnum >
	if (dblog(sqlite3_bind_int64(Commit_Roots_select_next_commitnum, 1, commitnum))) return -1;

	if (dblog(rc = sqlite3_step(Commit_Roots_select_next_commitnum), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
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

	if (sqlite3_data_count(Commit_Roots_select_next_commitnum) != 3)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectCommitnum select returned " << sqlite3_data_count(Commit_Roots_select_next_commitnum) << " columns";

		return -1;
	}

	// Level, Timestamp, MerkleRoot
	level = sqlite3_column_int64(Commit_Roots_select_next_commitnum, 0);
	timestamp = sqlite3_column_int64(Commit_Roots_select_next_commitnum, 1);
	auto merkleroot_blob = sqlite3_column_blob(Commit_Roots_select_next_commitnum, 2);
	unsigned merkleroot_bytes = sqlite3_column_bytes(Commit_Roots_select_next_commitnum, 2);

	if (!merkleroot_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectCommitnum MerkleRoot is null";

		return -1;
	}
	else if (merkleroot_bytes != hashsize && merkleroot_bytes != TX_COMMIT_IV_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::CommitRootsSelectCommitnum MerkleRoot Data size " << sqlite3_column_bytes(Commit_Roots_select_next_commitnum, 2) << " != " << hashsize;

		return -1;
	}

	memcpy(hash, merkleroot_blob, merkleroot_bytes);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::CommitRootsSelectCommitnum simulating database error post-error check";

		return -1;
	}

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::CommitRootsSelectCommitnum commitnum " << commitnum << " returning level " << level << " timestamp " << timestamp << " root " << buf2hex(hash, hashsize);

	return 0;
}

int DbConnPersistData::TxOutputInsert(const void *addr, unsigned addrsize, uint32_t domain, uint64_t asset_enc, uint64_t amount_enc, uint64_t param_level, uint64_t commitnum)
{
	if (!g_params.index_txouts)
		return 0;

	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputInsert address " << buf2hex(addr, addrsize) << " domain " << domain << " asset_enc " << asset_enc << " amount_enc " << amount_enc << " param_level " << param_level << " commitnum " << commitnum;

	// Address, Domain, AssetEnc, AmountEnc, ParamLevel, Commitnum
	if (dblog(sqlite3_bind_blob(Tx_Outputs_insert, 1, addr, addrsize, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Tx_Outputs_insert, 2, domain))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 3, asset_enc))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 4, amount_enc))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 5, param_level))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_insert, 6, commitnum))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Tx_Outputs_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputInsert sqlite3_changes " << changes << " after insert address " << buf2hex(addr, addrsize) << " domain " << domain << " asset_enc " << asset_enc << " amount_enc " << amount_enc << " param_level " << param_level << " commitnum " << commitnum;

		return -1;
	}

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::TxOutputInsert inserted address " << buf2hex(addr, addrsize) << " domain " << domain << " asset_enc " << asset_enc << " amount_enc " << amount_enc << " param_level " << param_level << " commitnum " << commitnum;

	return 0;
}

int DbConnPersistData::TxOutputsSelect(const void *addr, unsigned addrsize, uint64_t commitnum_start, uint32_t *domain, uint64_t *asset_enc, uint64_t *amount_enc, char *commitiv, unsigned ivsize, char *commitment, unsigned commitsize, uint64_t *commitnum, unsigned limit, bool *have_more)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect address " << buf2hex(addr, addrsize) << " commitnum_start " << commitnum_start << " limit " << limit;

	CCASSERT(limit);

	// Address, Commitnum >=, limit
	if (dblog(sqlite3_bind_blob(Tx_Outputs_select, 1, addr, addrsize, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Tx_Outputs_select, 2, commitnum_start))) return -1;
	if (dblog(sqlite3_bind_int(Tx_Outputs_select, 3, limit < 2 ? limit : limit + 1))) return -1;

	unsigned nfound = 0;
	if (have_more)
		*have_more = false;

	while (!g_shutdown)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Tx_Outputs_select), DB_STMT_SELECT)) return -1;

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputsSelect simulating database error post-select";

			return -1;
		}

		if (dbresult(rc) == SQLITE_DONE)
		{
			if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect not found";

			break;
		}

		if (dbresult(rc) != SQLITE_ROW)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect returned " << rc;

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
			BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::TxOutputsSelect returned " << sqlite3_data_count(Tx_Outputs_select) << " columns";

			return -1;
		}

		// Domain, AssetEnc, AmountEnc, MerkleRoot, Commitment, Commitnum
		domain[nfound] = sqlite3_column_int(Tx_Outputs_select, 0);
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

		if (RandTest(RTEST_DB_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::TxOutputsSelect simulating database error post-error check";

			return -1;
		}

		if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::TxOutputsSelect address " << buf2hex(addr, addrsize) << " domain " << domain[nfound] << " asset_enc " << asset_enc[nfound] << " amount_enc " << amount_enc[nfound] << " commitiv " << buf2hex(commitiv + nfound * ivsize, ivsize) << " commitment " << buf2hex(commitment + nfound * commitsize, commitsize) << " commitnum " << commitnum[nfound];

		nfound++;
	}

	return nfound;
}

int DbConnPersistData::XcxNumsInsert(uint64_t level, uint64_t timestamp, uint64_t next_xreqnum, uint64_t next_xmatchnum)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XcxNumsInsert level " << level << " timestamp " << timestamp << " next_xreqnum " << next_xreqnum << " next_xmatchnum " << next_xmatchnum;

	// Level, Timestamp, NextXreqnum, NextXmatchnum
	if (dblog(sqlite3_bind_int64(Xcx_Nums_insert, 1, level))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Nums_insert, 2, timestamp))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Nums_insert, 3, next_xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Nums_insert, 4, next_xmatchnum))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XcxNumsInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Xcx_Nums_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XcxNumsInsert sqlite3_changes " << changes << " after insert level " << level << " next_xreqnum " << next_xreqnum << " next_xmatchnum " << next_xmatchnum;

		return -1;
	}

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XcxNumsInsert inserted level " << level << " timestamp " << timestamp << " next_xreqnum " << next_xreqnum << " next_xmatchnum " << next_xmatchnum;

	return 0;
}

int DbConnPersistData::XcxNumsSelect(const uint64_t max_level, uint64_t& level, uint64_t& timestamp, uint64_t& next_xreqnum, uint64_t& next_xmatchnum)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XcxNumsSelect max_level " << max_level;

	level = 0;
	timestamp = 0;
	next_xreqnum = 0;
	next_xmatchnum = 0;

	int rc;

	// Level <=
	if (dblog(sqlite3_bind_int64(Xcx_Nums_select, 1, max_level))) return -1;

	if (dblog(rc = sqlite3_step(Xcx_Nums_select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XcxNumsSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XcxNumsSelect returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XcxNumsSelect returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Xcx_Nums_select) != 4)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XcxNumsSelect returned " << sqlite3_data_count(Xcx_Nums_select) << " columns";

		return -1;
	}

	level = sqlite3_column_int64(Xcx_Nums_select, 0);
	timestamp = sqlite3_column_int64(Xcx_Nums_select, 1);
	next_xreqnum = sqlite3_column_int64(Xcx_Nums_select, 2);
	next_xmatchnum = sqlite3_column_int64(Xcx_Nums_select, 3);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XcxNumsSelect simulating database error post-error check";

		return -1;
	}

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XcxNumsSelect max_level " << max_level << " returning level " << level << " timestamp " << timestamp << " next_xreqnum " << next_xreqnum << " next_xmatchnum " << next_xmatchnum;

	return 0;
}

int DbConnPersistData::XmatchreqInsert(const Xmatchreq& req)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqInsert " << req.DebugString();

	if (XmatchreqInsertInternal(req))
		return -1;

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XmatchreqInsert inserted " << req.DebugString();

	return 0;
}

int DbConnPersistData::XmatchreqUpdate(uint64_t xreqnum, unsigned disposition)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqUpdate xreqnum " << xreqnum << " disposition " << disposition;

	// Xreqnum, Disposition
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_update, 1, xreqnum))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_update, 2, disposition))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchreqUpdate simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Xcx_Match_Reqs_update);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
		BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::XmatchreqUpdate sqlite3_changes " << changes << " after update xreqnum " << xreqnum << " disposition " << disposition;

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XmatchreqUpdate updated xreqnum " << xreqnum << " disposition " << disposition;

	return 0;
}

int DbConnPersistData::XmatchreqInsertInternal(const Xmatchreq& req)
{
	//if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqInsert " << req.DebugString();

	auto min_fp = tx_amount_encode(req.min_amount, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);
	auto max_fp = tx_amount_encode(req.max_amount, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);

	unsigned packed_flags = 0;
	packed_flags = (packed_flags << 1) | !!req.flags.add_immediately_to_blockchain;
	packed_flags = (packed_flags << 1) | !!req.flags.auto_accept_matches;
	packed_flags = (packed_flags << 1) | !!req.flags.no_minimum_after_first_match;
	packed_flags = (packed_flags << 1) | !!req.flags.must_liquidate_crossing_minimum;
	packed_flags = (packed_flags << 1) | !!req.flags.must_liquidate_below_minimum;

	//Exchange_Match_Reqs:
	// 1:Xreqnum integer
	// 2:Disposition integer, ExpireTime integer
	// 4:ObjId blob, Type integer
	// 6:BaseAsset integer, QuoteAsset integer, ForeignAsset string
	// 9:MinAmount integer, MaxAmount integer
	// 11:NetRateRequired float, WaitDiscount float, BaseCosts float, QuoteCosts float
	// 15:PackedFlags integer
	// 16:ConsiderationRequired integer, ConsiderationOffered integer, Pledge integer
	// 19:HoldTime integer, HoldTimeRequired integer, MinWaitTime integer
	// 22:AcceptTimeRequired integer, AcceptTimeOffered integer
	// 24:PaymentTime integer, Confirmations integer
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_insert, 1, req.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 2, req.disposition))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_insert, 3, req.expire_time))) return -1;
	if (dblog(sqlite3_bind_blob(Xcx_Match_Reqs_insert, 4, &req.objid, sizeof(req.objid), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 5, req.type))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_insert, 6, req.base_asset))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_insert, 7, req.quote_asset))) return -1;
	if (dblog(sqlite3_bind_text(Xcx_Match_Reqs_insert, 8, req.foreign_asset.c_str(), -1, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_insert, 9, min_fp))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_insert, 10, max_fp))) return -1;
	if (dblog(sqlite3_bind_double(Xcx_Match_Reqs_insert, 11, req.net_rate_required.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xcx_Match_Reqs_insert, 12, req.wait_discount.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xcx_Match_Reqs_insert, 13, req.base_costs.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xcx_Match_Reqs_insert, 14, req.quote_costs.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 15, packed_flags))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 16, req.consideration_required))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 17, req.consideration_offered))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 18, req.pledge))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 19, req.hold_time))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 20, req.hold_time_required))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 21, req.min_wait_time))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 22, req.accept_time_required))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 23, req.accept_time_offered))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 24, req.payment_time))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_Reqs_insert, 25, req.confirmations))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchreqInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Xcx_Match_Reqs_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqInsert sqlite3_changes " << changes << " after insert " << req.DebugString();

		return -1;
	}

	if (req.flags.have_matching)
	{
		if (XmatchingreqInsertInternal(req))
			return -1;

		sqlite3_reset(Xcx_Matching_Reqs_insert);
	}

	//if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XmatchreqInsert inserted " << req.DebugString();

	return 0;
}

#define DB_MATCHREQ_COLS		25
#define DB_MATCHINGREQ_COLS		8
#define DB_REQ_COLS				(DB_MATCHREQ_COLS + DB_MATCHINGREQ_COLS)

int DbConnPersistData::XmatchingreqInsertInternal(const Xmatchreq& req)
{
	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchingreqInsert " << req.DebugString();
	//BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchingreqInsert " << req.DebugString();

	//Exchange_Matching_Reqs:
	// 1:Xreqnum
	// 2:DeleteTime integer
	// 3:QuoteAsset integer, ForeignAddressUnique boolean, ForeignAddress blob, Destination blob, PubSigningKey blob, OpenAmount blob
	if (dblog(sqlite3_bind_int64(Xcx_Matching_Reqs_insert, 1, req.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Matching_Reqs_insert, 2, req.delete_time))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Matching_Reqs_insert, 3, req.quote_asset))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Matching_Reqs_insert, 4, req.BlockchainRequiresUniqueForeignAddress()))) return -1;
	if (dblog(sqlite3_bind_blob(Xcx_Matching_Reqs_insert, 5, req.foreign_address.c_str(), req.foreign_address.length(), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Xcx_Matching_Reqs_insert, 6, &req.destination, sizeof(req.destination), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Xcx_Matching_Reqs_insert, 7, &req.signing_public_key, sizeof(req.signing_public_key), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Xcx_Matching_Reqs_insert, 8, &req.open_amount, sizeof(req.open_amount), SQLITE_STATIC))) return -1;

	CCASSERT(DB_MATCHINGREQ_COLS == 8);

	if (!req.foreign_address.length())
	{
		if (dblog(sqlite3_bind_null(Xcx_Matching_Reqs_insert, 5))) return -1;
	}

	if (!req.flags.has_signing_key)
	{
		if (dblog(sqlite3_bind_null(Xcx_Matching_Reqs_insert, 7))) return -1;
	}

	if (!req.open_amount)
	{
		if (dblog(sqlite3_bind_null(Xcx_Matching_Reqs_insert, 8))) return -1;
	}

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchingreqInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Xcx_Matching_Reqs_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchingreqInsert sqlite3_changes " << changes << " after insert " << req.DebugString();

		return -1;
	}

	//if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XmatchingreqInsert inserted " << req.DebugString();

	return 0;
}

int DbConnPersistData::XmatchreqSelectInternal(Xmatchreq& req, bool matching_required, sqlite3_stmt *select, int cs)
{
	req.xreqnum = sqlite3_column_int64(select, cs + 0);
	if (!req.xreqnum)
		return 1;

	uint64_t matching_xreqnum = sqlite3_column_int64(select, cs + DB_MATCHREQ_COLS);

	if (matching_xreqnum)
	{
		if (matching_xreqnum != req.xreqnum)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XmatchreqSelect matching_xreqnum " << matching_xreqnum << " != xreqnum " << req.xreqnum;

			return -1;
		}
	}
	else if (matching_required)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XmatchreqSelect matching_xreqnum is null";

		return -1;
	}

	auto objid_blob = sqlite3_column_blob(select, cs + 3);
	auto destination_blob = sqlite3_column_blob(select, cs + 30);
	auto signing_key_blob = sqlite3_column_blob(select, cs + 31);
	auto open_amount_blob = sqlite3_column_blob(select, cs + 32);

	auto objid_size = sqlite3_column_bytes(select, cs + 3);
	auto destination_size = sqlite3_column_bytes(select, cs + 30);
	auto signing_key_size = sqlite3_column_bytes(select, cs + 31);
	auto open_amount_size = sqlite3_column_bytes(select, cs + 32);

	if (!objid_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelect objid_blob is null";

		return -1;
	}

	if (objid_blob && objid_size != sizeof(req.objid))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelect objid_size " << objid_size << " != " << sizeof(req.objid);

		return -1;
	}

	if (matching_xreqnum && !destination_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelect destination_blob is null";

		return -1;
	}

	if (destination_blob && destination_size != sizeof(req.destination))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelect destination_size " << destination_size << " != " << sizeof(req.destination);

		return -1;
	}

	if (signing_key_blob && (signing_key_size != sizeof(req.signing_private_key) || signing_key_size != sizeof(req.signing_public_key)))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelect signing_key_size " << signing_key_size << " != " << sizeof(req.signing_private_key) << " or " << sizeof(req.signing_public_key);

		return -1;
	}

	if (open_amount_blob && open_amount_size != sizeof(req.open_amount))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelect open_amount_size " << open_amount_size << " != " << sizeof(req.open_amount);

		return -1;
	}

	//Exchange_Match_Reqs:
	// 0:Xreqnum integer
	// 1:Disposition integer, ExpireTime integer
	// 3:ObjId blob, Type integer
	// 5:BaseAsset integer, QuoteAsset integer, ForeignAsset string
	// 8:MinAmount integer, MaxAmount integer
	// 10:NetRateRequired float, WaitDiscount float, BaseCosts float, QuoteCosts float
	// 14:PackedFlags integer
	// 15:ConsiderationRequired integer, ConsiderationOffered integer, Pledge integer
	// 18:HoldTime integer, HoldTimeRequired integer, MinWaitTime integer
	// 21:AcceptTimeRequired integer, AcceptTimeOffered integer
	// 23:PaymentTime integer, Confirmations integer
	//Exchange_Matching_Reqs:
	// 25:Xreqnum
	// 26:DeleteTime integer
	// 27:QuoteAsset integer, ForeignAddressUnique boolean, ForeignAddress blob, Destination blob, PubSigningKey blob, OpenAmount blob

	//req.xreqnum = sqlite3_column_int64(select, cs + 0);
	req.disposition = sqlite3_column_int(select, cs + 1);
	req.expire_time = sqlite3_column_int64(select, cs + 2);
	//&req.objid = sqlite3_column_blob(select, cs + 3);
	req.type = sqlite3_column_int(select, cs + 4);
	req.base_asset = sqlite3_column_int64(select, cs + 5);
	req.quote_asset = sqlite3_column_int64(select, cs + 6);
	auto foreign_asset_text = sqlite3_column_text(select, cs + 7);
	auto min_fp = sqlite3_column_int64(select, cs + 8);
	auto max_fp = sqlite3_column_int64(select, cs + 9);
	req.net_rate_required = sqlite3_column_double(select, cs + 10);
	req.wait_discount = sqlite3_column_double(select, cs + 11);
	req.base_costs = sqlite3_column_double(select, cs + 12);
	req.quote_costs = sqlite3_column_double(select, cs + 13);
	auto packed_flags = sqlite3_column_int(select, cs + 14);
	req.consideration_required = sqlite3_column_int(select, cs + 15);
	req.consideration_offered = sqlite3_column_int(select, cs + 16);
	req.pledge = sqlite3_column_int(select, cs + 17);
	req.hold_time = sqlite3_column_int(select, cs + 18);
	req.hold_time_required = sqlite3_column_int(select, cs + 19);
	req.min_wait_time = sqlite3_column_int(select, cs + 20);
	req.accept_time_required = sqlite3_column_int(select, cs + 21);
	req.accept_time_offered = sqlite3_column_int(select, cs + 22);
	req.payment_time = sqlite3_column_int(select, cs + 23);
	req.confirmations = sqlite3_column_int(select, cs + 24);

	CCASSERT(DB_MATCHREQ_COLS == 25);

	//req.xreqnum = sqlite3_column_int64(select, cs + 25);
	req.delete_time = sqlite3_column_int64(select, cs + 26);
	//req.quote_asset = sqlite3_column_int64(select, cs + 27);
	//req.foreign_address_unique = sqlite3_column_int64(select, cs + 28);
	auto foreign_address_text = sqlite3_column_text(select, cs + 29);
	//&req.destination = sqlite3_column_blob(select, cs + 30);
	//&req.signing_public_key = sqlite3_column_blob(select, cs + 31);
	//&req.open_amount = sqlite3_column_blob(select, cs + 32);

	CCASSERT(DB_REQ_COLS == 33);

	if (matching_xreqnum)
		req.flags.have_matching = true;

	req.flags.must_liquidate_below_minimum = packed_flags & 1; packed_flags >>= 1;
	req.flags.must_liquidate_crossing_minimum = packed_flags & 1; packed_flags >>= 1;
	req.flags.no_minimum_after_first_match = packed_flags & 1; packed_flags >>= 1;
	req.flags.auto_accept_matches = packed_flags & 1; packed_flags >>= 1;
	req.flags.add_immediately_to_blockchain = packed_flags & 1; packed_flags >>= 1;

	tx_amount_decode(min_fp, req.min_amount, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);
	tx_amount_decode(max_fp, req.max_amount, false, TX_AMOUNT_BITS, TX_AMOUNT_EXPONENT_BITS);

	if (foreign_asset_text)		req.foreign_asset = (char*)foreign_asset_text;
	if (foreign_address_text)	req.foreign_address = (char*)foreign_address_text;

	if (objid_blob)				memcpy(&req.objid, objid_blob, sizeof(req.objid));
	if (destination_blob)		memcpy(&req.destination, destination_blob, sizeof(req.destination));
	if (open_amount_blob)		memcpy(&req.open_amount, open_amount_blob, sizeof(req.open_amount));
	if (signing_key_blob)		memcpy(&req.signing_public_key, signing_key_blob, sizeof(req.signing_public_key));
	if (signing_key_blob)		req.flags.has_signing_key = true;

	return 0;
}

int DbConnPersistData::XmatchreqSelectMatching(uint64_t reqnum, Xmatchreq& req, bool or_greater)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqSelectMatching reqnum " << reqnum << " or_greater " << or_greater;

	req.Clear();

	int rc;

	// Xreqnum >=
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_select_matching, 1, reqnum))) return -1;

	if (dblog(rc = sqlite3_step(Xcx_Match_Reqs_select_matching), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchreqSelectMatching simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqSelectMatching reqnum " << reqnum << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelectMatching reqnum " << reqnum << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Xcx_Match_Reqs_select_matching) != DB_REQ_COLS)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelectMatching returned " << sqlite3_data_count(Xcx_Match_Reqs_select_matching) << " columns";

		return -1;
	}

	rc = XmatchreqSelectInternal(req, true, Xcx_Match_Reqs_select_matching);
	if (rc) goto err;

	if (req.xreqnum != reqnum && !or_greater)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XmatchreqSelectMatching reqnum " << reqnum << " or_greater " << or_greater << " found " << req.xreqnum << " returning SQLITE_DONE";

		return 1;
	}

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqSelectMatching reqnum " << reqnum << " or_greater " << or_greater << " returning " << req.DebugString();

	return 0;

err:
	req.Clear();

	return -1;
}

int DbConnPersistData::XmatchreqSelectObjIdDescendingId(const ccoid_t& objid, uint64_t reqnum_start, uint64_t &reqnum)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqSelectObjIdDescendingId reqnum_start " << reqnum_start << " objid " << buf2hex(&objid, CC_OID_TRACE_SIZE);

	reqnum = 0;

	int rc;

	// ObjId, Xreqnum <=
	if (dblog(sqlite3_bind_blob(Xcx_Match_Reqs_select_objid_descending_reqnum, 1, &objid, sizeof(objid), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_Reqs_select_objid_descending_reqnum, 2, reqnum_start))) return -1;

	if (dblog(rc = sqlite3_step(Xcx_Match_Reqs_select_objid_descending_reqnum), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchreqSelectObjIdDescendingId simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqSelectObjIdDescendingId reqnum " << reqnum << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelectObjId reqnum " << reqnum << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Xcx_Match_Reqs_select_objid_descending_reqnum) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchreqSelectObjId select returned " << sqlite3_data_count(Xcx_Match_Reqs_select_objid_descending_reqnum) << " columns";

		return -1;
	}

	// Xreqnum
	auto xreqnum = sqlite3_column_int64(Xcx_Match_Reqs_select_objid_descending_reqnum, 0);

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchreqSelectObjIdDescendingId simulating database error post-error check";

		return -1;
	}

	reqnum = xreqnum;

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchreqSelectObjIdDescendingId objid " << buf2hex(&objid, CC_OID_TRACE_SIZE) << " reqnum " << reqnum;

	return 0;
}

#define DB_MATCH_COLS 17

int DbConnPersistData::XmatchInsert(const Xmatch& match)
{
	CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchInsert " << match.DebugString();
	//BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchInsert " << match.DebugString();

	packed_unsigned_amount_t packed_amount;
	CCASSERTZ(pack_unsigned_amount(match.base_amount, packed_amount));

	//Exchange_Matches:
	// 1:Xmatchnum integer
	// 2:BuyXreqnum integer, SellXreqnum integer
	// 4:Type integer, Status integer, NextDeadline integer
	// 7:MatchTimestamp integer, AcceptTimestamp integer, FinalTimestamp integer
	// 10:BaseAmount blob, Rate float, AmountPaid float, MiningAmount float
	// 14:AcceptTime integer, BuyerConsideration integer, SellerConsideration integer
	// 17:BuyerPledge integer
	if (dblog(sqlite3_bind_int64(Xcx_Match_insert, 1, match.xmatchnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_insert, 2, match.xbuy.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_insert, 3, match.xsell.xreqnum))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_insert, 4, match.type))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_insert, 5, match.status))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_insert, 6, match.next_deadline))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_insert, 7, match.match_timestamp))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_insert, 8, match.accept_timestamp))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_insert, 9, match.final_timestamp))) return -1;
	if (dblog(sqlite3_bind_blob(Xcx_Match_insert, 10, &packed_amount, AMOUNT_UNSIGNED_PACKED_BYTES, SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_double(Xcx_Match_insert, 11, match.rate.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xcx_Match_insert, 12, match.amount_paid.asFloat()))) return -1;
	if (dblog(sqlite3_bind_double(Xcx_Match_insert, 13, match.mining_amount.asFloat()))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_insert, 14, match.accept_time))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_insert, 15, match.xbuy.match_consideration))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_insert, 16, match.xsell.match_consideration))) return -1;
	if (dblog(sqlite3_bind_int(Xcx_Match_insert, 17, match.match_pledge))) return -1;

	CCASSERT(DB_MATCH_COLS == 17);

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Xcx_Match_insert);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Persistent_db);

	if (changes != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchInsert sqlite3_changes " << changes << " after insert " << match.DebugString();

		return -1;
	}

	if (match.have_xreqs)
	{
		if (XmatchreqInsertInternal(match.xbuy))
			return -1;

		sqlite3_reset(Xcx_Match_Reqs_insert);

		if (XmatchreqInsertInternal(match.xsell))
			return -1;
	}

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XmatchInsert inserted " << match.DebugString();

	if (0) // for testing
	{
		cerr << "Checking XmatchInsert --> XmatchSelectReqnum xmatchnum " << match.xmatchnum << " buyer xreqnum " << match.xbuy.xreqnum << " seller xreqnum " << match.xsell.xreqnum << endl;
		auto m1 = match.DebugString();
		Xmatch match2;
		if (RandTest(2))
			XmatchSelect(match.xmatchnum, match2, false, true, true);
		else if (RandTest(2))
			XmatchSelectReqnum(match.xbuy.xreqnum, match.xmatchnum, match2, true, true);
		else
			XmatchSelectReqnum(match.xsell.xreqnum, match.xmatchnum, match2, true, true);
		//match2.Clear();
		//match2.xbuy.delete_time = match.xbuy.delete_time;
		//match2.xsell.delete_time = match.xsell.delete_time;
		auto m2 = match2.DebugString();
		if (m1 != m2)
		{
			cerr << "Xmatch mismatch:" << endl;
			cerr << "inserted: " << m1 << endl;
			cerr << "    read: " << m2 << endl;
		}
	}

	return 0;
}

int DbConnPersistData::XmatchSelectInternal(uint64_t matchnum, Xmatch& match, sqlite3_stmt *select, bool or_greater, bool xreqs_required, bool matching_required)
{
	int rc;

	if (dblog(rc = sqlite3_step(select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchSelect matchnum " << matchnum << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchSelect matchnum " << matchnum << " returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(select) != DB_MATCH_COLS + 2*DB_REQ_COLS)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchSelect returned " << sqlite3_data_count(select) << " columns";

		return -1;
	}

	//for (int i = 0; i < sqlite3_data_count(select); ++i)
	//	cout << "select column " << i << " = " << sqlite3_column_text(select, i) << endl;

	uint64_t xmatchnum = sqlite3_column_int64(select, 0);
	if (xmatchnum != matchnum && !or_greater)
	{
		BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XmatchSelect matchnum " << matchnum << " or_greater " << or_greater << " found " << xmatchnum << " returning SQLITE_DONE";

		return 1;
	}

	//Exchange_Matches:
	// 0:Xmatchnum integer
	// 1:BuyXreqnum integer, SellXreqnum integer
	// 3:Type integer, Status integer, NextDeadline integer
	// 6:MatchTimestamp integer, AcceptTimestamp integer, FinalTimestamp integer
	// 9:BaseAmount blob, Rate float, AmountPaid float, MiningAmount float
	// 13:AcceptTime integer, BuyerConsideration integer, SellerConsideration integer
	// 16:BuyerPledge integer
	match.xmatchnum = xmatchnum;
	match.xbuy.xreqnum = sqlite3_column_int64(select, 1);
	match.xsell.xreqnum = sqlite3_column_int64(select, 2);
	match.type = sqlite3_column_int(select, 3);
	match.status = sqlite3_column_int(select, 4);
	match.next_deadline = sqlite3_column_int64(select, 5);
	match.match_timestamp = sqlite3_column_int64(select, 6);
	match.accept_timestamp = sqlite3_column_int64(select, 7);
	match.final_timestamp = sqlite3_column_int64(select, 8);
	auto amount_blob = sqlite3_column_blob(select, 9);
	auto amount_size = sqlite3_column_bytes(select, 9);
	match.rate = sqlite3_column_double(select, 10);
	match.amount_paid = sqlite3_column_double(select, 11);
	match.mining_amount = sqlite3_column_double(select, 12);
	match.accept_time = sqlite3_column_int(select, 13);
	match.xbuy.match_consideration = sqlite3_column_int(select, 14);
	match.xsell.match_consideration = sqlite3_column_int(select, 15);
	match.match_pledge = sqlite3_column_int(select, 16);

	CCASSERT(DB_MATCH_COLS == 17);

	if (!amount_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::XmatchSelect amount is null";

		return -1;
	}

	if (amount_size != AMOUNT_UNSIGNED_PACKED_BYTES)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConn::XmatchSelect returned amount size " << amount_size << " != " << AMOUNT_UNSIGNED_PACKED_BYTES;

		return -1;
	}

	unpack_unsigned_amount(amount_blob, match.base_amount);

	rc = XmatchreqSelectInternal(match.xbuy, matching_required, select, DB_MATCH_COLS);
	if (rc) goto err;

	if (match.xbuy.xreqnum)
	{
		if (match.xbuy.xreqnum != (uint64_t)sqlite3_column_int64(select, 1))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XmatchSelect xbuy.xreqnum " << match.xbuy.xreqnum << " != BuyXreqnum " << sqlite3_column_int64(select, 1);

			goto err;
		}
	}
	else if (xreqs_required)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XmatchSelect xbuy.xreqnum is null";

		goto err;
	}

	rc = XmatchreqSelectInternal(match.xsell, matching_required, select, DB_MATCH_COLS + DB_REQ_COLS);
	if (rc) goto err;

	if (match.xsell.xreqnum)
	{
		if (match.xsell.xreqnum != (uint64_t)sqlite3_column_int64(select, 2))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XmatchSelect xsell.xreqnum " << match.xsell.xreqnum << " != SellXreqnum " << sqlite3_column_int64(select, 2);

			goto err;
		}
	}
	else if (xreqs_required)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnXreqs::XmatchSelect xsell.xreqnum is null";

		goto err;
	}

	if (dblog(sqlite3_extended_errcode(Persistent_db), DB_STMT_SELECT)) goto err;	// check if error retrieving results

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchSelect simulating database error post-error check";

		goto err;
	}

	if (match.xbuy.xreqnum && match.xsell.xreqnum)
		match.have_xreqs = true;
	else
		CCASSERTZ(xreqs_required);

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchSelect matchnum " << matchnum << " returning " << match.DebugString();

	return 0;

err:
	match.Clear();

	return -1;
}

int DbConnPersistData::XmatchSelect(uint64_t matchnum, Xmatch& match, bool or_greater, bool xreqs_required, bool matching_required)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchSelect matchnum " << matchnum << " or_greater " << or_greater << " xreqs_required " << xreqs_required << " matching_required " << matching_required;

	match.Clear();

	// Xmatchnum >=
	if (dblog(sqlite3_bind_int64(Xcx_Match_select, 1, matchnum))) return -1;

	return XmatchSelectInternal(matchnum, match, Xcx_Match_select, or_greater, xreqs_required, matching_required);
}

int DbConnPersistData::XmatchSelectReqnum(uint64_t reqnum, uint64_t matchnum, Xmatch& match, bool xreqs_required, bool matching_required)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchSelectReqnum reqnum " << reqnum << " matchnum " << matchnum << " xreqs_required " << xreqs_required << " matching_required " << matching_required;

	match.Clear();

	// Xreqnum, Xmatchnum >=
	if (dblog(sqlite3_bind_int64(Xcx_Match_select_reqnum, 1, reqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Match_select_reqnum, 2, matchnum))) return -1;

	return XmatchSelectInternal(matchnum, match, Xcx_Match_select_reqnum, true, xreqs_required, matching_required);
}

int DbConnPersistData::XmatchSelectNextDeadline(uint64_t deadline, Xmatch& match, bool xreqs_required, bool matching_required)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchSelectNextDeadline deadline " << deadline << " xreqs_required " << xreqs_required << " matching_required " << matching_required;

	match.Clear();

	// NextDeadline <=
	if (dblog(sqlite3_bind_int64(Xcx_Match_select_deadline, 1, deadline))) return -1;

	return XmatchSelectInternal(0, match, Xcx_Match_select_deadline, true, xreqs_required, matching_required);
}

int DbConnPersistData::XmatchingreqPrune(uint64_t delete_time)
{
	//CCASSERT(ThisThreadHoldsMutex());
	// call BeginWrite first to acquire lock
	//lock_guard<mutex> lock(Persistent_db_write_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchingreqPrune delete_time " << delete_time;

	// DeleteTime
	if (dblog(sqlite3_bind_int64(Xcx_Matching_Reqs_prune, 1, delete_time))) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchingreqPrune simulating database error pre-delete";

		return -1;
	}

	auto rc = sqlite3_step(Xcx_Matching_Reqs_prune);

	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes64(Persistent_db);

	if (TRACE_DB_WRITES) BOOST_LOG_TRIVIAL(debug) << "DbConnPersistData::XmatchingreqPrune sqlite3_changes " << changes << " after delete delete_time " << delete_time;

	return 0;
}

int DbConnPersistData::XmatchingreqUniqueForeignAddressSelect(uint64_t prior_blocktime, uint64_t blockchain, const string& foreign_address)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchingreqUniqueForeignAddressSelect prior_blocktime " << prior_blocktime << " blockchain " << blockchain << " foreign_address " << foreign_address;

	CCASSERT(foreign_address.length());

	int rc;

	//  Blocktime, Blockchain, ForeignAddress
	if (dblog(sqlite3_bind_int64(Xcx_Matching_Reqs_Foreign_Address_select, 1, prior_blocktime))) return -1;
	if (dblog(sqlite3_bind_int64(Xcx_Matching_Reqs_Foreign_Address_select, 2, blockchain))) return -1;
	if (dblog(sqlite3_bind_blob(Xcx_Matching_Reqs_Foreign_Address_select, 3, foreign_address.c_str(), foreign_address.length(), SQLITE_STATIC))) return -1;

	if (dblog(rc = sqlite3_step(Xcx_Matching_Reqs_Foreign_Address_select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XmatchingreqUniqueForeignAddressSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		//BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::XmatchingreqUniqueForeignAddressSelect prior_blocktime " << prior_blocktime << " foreign_address " << foreign_address << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XmatchingreqUniqueForeignAddressSelect prior_blocktime " << prior_blocktime << " foreign_address " << foreign_address << " returned " << rc;

		return -1;
	}

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XmatchingreqUniqueForeignAddressSelect prior_blocktime " << prior_blocktime << " foreign_address " << foreign_address << " found";

	return 0;
}

int DbConnPersistData::XcxBlockedForeignAddressSelect(uint64_t blockchain, const string& foreign_address)
{
	Finally finally(boost::bind(&DbConnPersistData::DoPersistentDataFinish, this));

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XcxBlockedForeignAddressSelect blockchain " << blockchain << " foreign_address " << foreign_address;

	CCASSERT(foreign_address.length());

	int rc;

	//  Blockchain, ForeignAddress
	if (dblog(sqlite3_bind_int64(Xcx_Blocked_Foreign_Address_select, 1, blockchain))) return -1;
	if (dblog(sqlite3_bind_blob(Xcx_Blocked_Foreign_Address_select, 2, foreign_address.c_str(), foreign_address.length(), SQLITE_STATIC))) return -1;

	if (dblog(rc = sqlite3_step(Xcx_Blocked_Foreign_Address_select), DB_STMT_SELECT)) return -1;

	if (RandTest(RTEST_DB_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnPersistData::XcxBlockedForeignAddressSelect simulating database error post-select";

		return -1;
	}

	if (dbresult(rc) == SQLITE_DONE)
	{
		//BOOST_LOG_TRIVIAL(warning) << "DbConnPersistData::XcxBlockedForeignAddressSelect blockchain " << blockchain << " foreign_address " << foreign_address << " returned SQLITE_DONE";

		return 1;
	}

	if (dbresult(rc) != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnPersistData::XcxBlockedForeignAddressSelect blockchain " << blockchain << " foreign_address " << foreign_address << " returned " << rc;

		return -1;
	}

	if (TRACE_DB_READS) BOOST_LOG_TRIVIAL(trace) << "DbConnPersistData::XcxBlockedForeignAddressSelect blockchain " << blockchain << " foreign_address " << foreign_address << " found";

	return 0;
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

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial), &hashkey, sizeof(hashkey), 0);
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

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial), &hashkey, sizeof(hashkey), 0);
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

	rc = dbconnW->SerialnumInsert(&serial, sizeof(serial), &hashkey, sizeof(hashkey), 0);
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