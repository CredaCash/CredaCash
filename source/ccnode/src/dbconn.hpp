/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * dbconn.hpp
*/

#pragma once

#include "relay_request_params.h"
#include "process_queue.h"

#include <SmartBuf.hpp>
#include <CCobjdefs.h>
#include <CCbigint.hpp>

#include <sqlite/sqlite3.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>
//#include <boost/thread/shared_mutex.hpp>	// seems to have bugs (throws random exceptions)
#include <boost/thread/locks.hpp>

//#define TEST_EXPLAIN_DB_QUERIES	1

//#define RTEST_DB_ERRORS			64
//!#define RTEST_DELAY_DB_RESET		32

#ifndef TEST_EXPLAIN_DB_QUERIES
#define TEST_EXPLAIN_DB_QUERIES		0	// don't test
#endif

#ifndef RTEST_DB_ERRORS
#define RTEST_DB_ERRORS				0	// don't test
#endif

#ifndef RTEST_DELAY_DB_RESET
#define RTEST_DELAY_DB_RESET		0	// don't test
#endif

#define RELAY_STATUS_ANNOUNCED		0
#define RELAY_STATUS_DOWNLOADED		1

#define RELAY_PEER_STATUS_READY		0
#define RELAY_PEER_STATUS_STARTED	1

// special values used to mark items in Temp_Serials_db
#define TEMP_SERIALS_PROCESS_BLOCKP		1
#define TEMP_SERIALS_WITNESS_BLOCKP		2

#define CLEAR_DB_POINTERS(lo, hi)	memset(&(lo), 0, sizeof(hi) + (uintptr_t)&(hi) - (uintptr_t)&(lo))

class Xreq;
class Xmatch;
class Xmatchreq;
class UniFloat;

class Db_Exception : public std::exception
{ };

class DbConnBasePersistData
{
public:
	sqlite3 *Persistent_db;

	void OpenDb(bool create = false);
	void DeInit();

	DbConnBasePersistData()
	{
		Persistent_db = NULL;
	}

	~DbConnBasePersistData()
	{
		DeInit();
	}
};

class DbConnBaseTempSerials
{
public:
	sqlite3 *Temp_Serials_db;

	void OpenDb(bool create = false);
	void DeInit();

	DbConnBaseTempSerials()
	{
		Temp_Serials_db = NULL;
	}

	~DbConnBaseTempSerials()
	{
		DeInit();
	}
};

class DbConnBaseRelayObjs
{
public:
	sqlite3 *Relay_Objs_db;

	void OpenDb(bool create = false);
	void DeInit();

	DbConnBaseRelayObjs()
	{
		Relay_Objs_db = NULL;
	}

	~DbConnBaseRelayObjs()
	{
		DeInit();
	}
};

class DbConnBaseProcessQ
{
public:
	array<sqlite3*, PROCESS_Q_N> Process_Q_db;

	void OpenDb(unsigned type, bool create = false);
	void DeInit();

	DbConnBaseProcessQ()
	{
		for (unsigned i = 0; i < PROCESS_Q_N; ++i)
			Process_Q_db[i] = NULL;
	}

	~DbConnBaseProcessQ()
	{
		DeInit();
	}
};

class DbConnBaseValidObjs
{
public:
	sqlite3 *Valid_Objs_db;

	void OpenDb(bool create = false);
	void DeInit();

	DbConnBaseValidObjs()
	{
		Valid_Objs_db = NULL;
	}

	~DbConnBaseValidObjs()
	{
		DeInit();
	}
};

class DbConnBaseXreqs
{
public:
	sqlite3 *Xreqs_db;

	void OpenDb(bool create = false);
	void DeInit();

	DbConnBaseXreqs()
	{
		Xreqs_db = NULL;
	}

	~DbConnBaseXreqs()
	{
		DeInit();
	}
};

class WalDB
{
protected:
	const char *dbname;

	mutex& Wal_db_mutex;

	atomic<bool> checkpoint_needed;
	uint32_t last_full_checkpoint_time;
	bool full_checkpoint_pending;
	atomic<bool> do_full_checkpoint;
	atomic<bool> stop_checkpointing;
	mutex checkpoint_mutex;
	condition_variable checkpoint_condition_variable;

	thread *m_thread;

	void WalCheckpointThreadProc(sqlite3 *db);
	void WalWaitForStartCheckpoint();
	void WalCheckpoint(sqlite3 *db);

public:
	WalDB(const char *name, mutex& mutex)
	 :	dbname(name),
		Wal_db_mutex(mutex),
		last_full_checkpoint_time(0),
		full_checkpoint_pending(0)
	{ }

	void WalStartCheckpointing(sqlite3 *db);
	void WalStopCheckpointing();

	void WalStartCheckpoint(bool full);
};

class DbConnPersistData : protected DbConnBasePersistData
{
	sqlite3_stmt *Persistent_Data_begin_read;
	sqlite3_stmt *Persistent_Data_rollback;
	sqlite3_stmt *Persistent_Data_begin_write;
	sqlite3_stmt *Persistent_Data_commit;
	sqlite3_stmt *Parameters_insert;
	sqlite3_stmt *Parameters_select;
	sqlite3_stmt *Parameters_increment;
	sqlite3_stmt *Blockchain_insert;
	sqlite3_stmt *Blockchain_select_max;
	sqlite3_stmt *Blockchain_select;
	sqlite3_stmt *Serialnum_insert;
	sqlite3_stmt *Serialnum_select;
	sqlite3_stmt *Commit_Tree_insert;
	sqlite3_stmt *Commit_Tree_select;
	sqlite3_stmt *Commit_Roots_insert;
	sqlite3_stmt *Commit_Roots_select_level;
	sqlite3_stmt *Commit_Roots_select_level_last;
	sqlite3_stmt *Commit_Roots_select_next_commitnum;
	sqlite3_stmt *Tx_Outputs_insert;
	sqlite3_stmt *Tx_Outputs_select;
	sqlite3_stmt *Xcx_Nums_insert;
	sqlite3_stmt *Xcx_Nums_select;
	sqlite3_stmt *Xcx_Match_insert;
	sqlite3_stmt *Xcx_Match_Reqs_insert;
	sqlite3_stmt *Xcx_Match_Reqs_update;
	sqlite3_stmt *Xcx_Matching_Reqs_insert;
	sqlite3_stmt *Xcx_Matching_Reqs_prune;
	sqlite3_stmt *Xcx_Match_Reqs_select_matching;
	sqlite3_stmt *Xcx_Match_Reqs_select_objid_descending_reqnum;
	sqlite3_stmt *Xcx_Match_select;
	sqlite3_stmt *Xcx_Match_select_reqnum;
	sqlite3_stmt *Xcx_Match_select_deadline;
	sqlite3_stmt *Xcx_Matching_Reqs_Foreign_Address_select;
	sqlite3_stmt *Xcx_Blocked_Foreign_Address_select;

	void ClearDbPointers()
	{
		CLEAR_DB_POINTERS(Persistent_Data_begin_read, Xcx_Blocked_Foreign_Address_select);
	};

	static WalDB Persistent_Wal;

	int XmatchreqInsertInternal(const Xmatchreq& req);
	int XmatchingreqInsertInternal(const Xmatchreq& req);
	int XmatchreqSelectInternal(Xmatchreq& req, bool matching_required, sqlite3_stmt *select, int cs = 0);
	int XmatchSelectInternal(uint64_t matchnum, Xmatch& match, sqlite3_stmt *select, bool or_greater = false, bool xreqs_required = false, bool matching_required = false);

public:
	DbConnPersistData();
	~DbConnPersistData();
	void DoPersistentDataFinish();

	int BeginRead();
	int EndRead();

	int BeginWrite();
	int EndWrite(bool commit = false);
	bool ThisThreadHoldsMutex();
	void ReleaseMutex();

	int ParameterInsert(int key, int subkey, void *value, unsigned valsize);
	int ParameterIncrement(int key, int subkey);
	int ParameterSelect(int key, int subkey, void *value, unsigned bufsize, bool add_terminator = false, unsigned *retsize = NULL);
	int BlockchainInsert(uint64_t level, SmartBuf smartobj);
	int BlockchainSelect(uint64_t level, SmartBuf *retobj);
	int BlockchainSelectMax(uint64_t& level);
	int SerialnumInsert(const void *serialnum, unsigned serialnum_size, const void *hashkey, unsigned hashkey_size, uint64_t tx_commitnum);
	int SerialnumSelect(const void *serialnum, unsigned serialnum_size, void *hashkey = NULL, unsigned *hashkey_size = NULL, uint64_t *tx_commitnum = NULL);
	int CommitTreeInsert(unsigned height, uint64_t offset, const void *data, unsigned datasize);
	int CommitTreeSelect(unsigned height, uint64_t offset, void *data, unsigned datasize);
	int CommitRootsInsert(uint64_t level, uint64_t timestamp, uint64_t next_commitnum, const void *hash, unsigned hashsize);
	int CommitRootsSelectLevel(uint64_t& level, int or_greater, uint64_t& timestamp, uint64_t& next_commitnum, void *hash, unsigned hashsize);
	int CommitRootsSelectCommitnum(uint64_t commitnum, uint64_t& level, uint64_t& timestamp, void *hash, unsigned hashsize);
	int TxOutputInsert(const void *addr, unsigned addrsize, uint32_t domain, uint64_t asset_enc, uint64_t amount_enc, uint64_t param_level, uint64_t commitnum);
	int TxOutputsSelect(const void *addr, unsigned addrsize, uint64_t commitnum_start, uint32_t *domain, uint64_t *asset_enc, uint64_t *amount_enc, char *commitiv, unsigned ivsize, char *commitment, unsigned commitsize, uint64_t *commitnum, unsigned limit, bool *have_more);
	int XcxNumsInsert(uint64_t level, uint64_t timestamp, uint64_t next_xreqnum, uint64_t next_xmatchnum);
	int XcxNumsSelect(const uint64_t max_level, uint64_t& level, uint64_t& timestamp, uint64_t& next_xreqnum, uint64_t& next_xmatchnum);
	int XmatchreqInsert(const Xmatchreq& req);
	int XmatchreqUpdate(uint64_t xreqnum, unsigned disposition);
	int XmatchreqSelectMatching(uint64_t reqnum, Xmatchreq& req, bool or_greater = false);
	int XmatchreqSelectObjIdDescendingId(const ccoid_t& objid, uint64_t reqnum_start, uint64_t &reqnum);
	int XmatchInsert(const Xmatch& match);
	int XmatchSelect(uint64_t matchnum, Xmatch& match, bool or_greater = false, bool xreqs_required = false, bool matching_required = false);
	int XmatchSelectReqnum(uint64_t reqnum, uint64_t matchnum, Xmatch& match, bool xreqs_required = false, bool matching_required = false);
	int XmatchSelectNextDeadline(uint64_t deadline, Xmatch& match, bool xreqs_required = false, bool matching_required = false);
	int XmatchingreqPrune(uint64_t delete_time);
	int XmatchingreqUniqueForeignAddressSelect(uint64_t prior_blocktime, uint64_t blockchain, const string& foreign_address);
	int XcxBlockedForeignAddressSelect(uint64_t blockchain, const string& foreign_address);

	static void TestConcurrency();

	// PersistentData_StartCheckpointing needs to be called on a DbConn object that is not being used for anything else
	// in order to avoid conflicts on the Persistent_db handle
	void PersistentData_StartCheckpointing()
	{
		Persistent_Wal.WalStartCheckpointing(Persistent_db);
	}

	static void PersistentData_StopCheckpointing()
	{
		Persistent_Wal.WalStopCheckpointing();
	}

	static void PersistentData_StartCheckpoint(bool full)
	{
		Persistent_Wal.WalStartCheckpoint(full);
	}
};

class DbConnTempSerials : protected DbConnBaseTempSerials
{
	sqlite3_stmt *Temp_Serials_insert;
	sqlite3_stmt *Temp_Serials_select;
	sqlite3_stmt *Temp_Serials_update;
	//sqlite3_stmt *Temp_Serials_delete;
	sqlite3_stmt *Temp_Serials_clear;
	sqlite3_stmt *Temp_Serials_prune;

	void ClearDbPointers()
	{
		CLEAR_DB_POINTERS(Temp_Serials_insert, Temp_Serials_prune);
	};

public:
	DbConnTempSerials();
	~DbConnTempSerials();
	void DoTempSerialsFinish();

	int TempSerialnumInsert(const void *serialnum, unsigned serialnum_size, const void* blockp);
	//int TempSerialnumDelete(const void *serialnum, unsigned serialnum_size, const void* blockp);
	int TempSerialnumSelect(const void *serialnum, unsigned serialnum_size, const void* last_blockp, void *output[], unsigned bufsize);
	int TempSerialnumUpdate(const void* old_blockp, const void* new_blockp, uint64_t level);
	int TempSerialnumClear(const void* blockp);
	int TempSerialnumPruneLevel(uint64_t level);
};

class DbConnRelayObjs : protected DbConnBaseRelayObjs
{
	sqlite3_stmt *Relay_Objs_begin;
	sqlite3_stmt *Relay_Objs_rollback;
	sqlite3_stmt *Relay_Objs_commit;
	sqlite3_stmt *Relay_Objs_select_objid;
	sqlite3_stmt *Relay_Objs_insert;
	sqlite3_stmt *Relay_Objs_select_download;
	sqlite3_stmt *Relay_Objs_update;

	sqlite3_stmt *Relay_Objs_select_oldest;
	sqlite3_stmt *Relay_Objs_delete_seqnum;
	sqlite3_stmt *Relay_Objs_delete_oid;

	sqlite3_stmt *Relay_Peers_insert;
	sqlite3_stmt *Relay_Peers_update_seqnum;
	sqlite3_stmt *Relay_Peers_delete_seqnum;
	sqlite3_stmt *Relay_Peers_delete_peer;

	void ClearDbPointers()
	{
		CLEAR_DB_POINTERS(Relay_Objs_begin, Relay_Peers_delete_peer);
	};

public:
	DbConnRelayObjs();
	~DbConnRelayObjs();
	void DoRelayObjsFinish(bool rollback);

	void RelayObjsInsert(unsigned peer, unsigned type, const relay_request_wire_params_t& req_params, unsigned obj_status, unsigned peer_status);
	int RelayObjsFindDownloads(unsigned conn_index, uint64_t last_indelible_level, uint8_t *output, unsigned bufsize, relay_request_param_buf_t& req_params, int maxobjs, int64_t bytes_pending, bool &have_blocks, unsigned &nobjs, unsigned &nbytes);
	int RelayObjsSetStatus(const ccoid_t& oid, unsigned obj_status, int timeout);
	int RelayObjsDeletePeer(unsigned peer);

	int RelayObjsDeleteSeqnum(int64_t seqnum);
	int RelayObjsGetExpires(int64_t min_seqnum, int64_t max_seqnum, int64_t& next_expires_seqnum, ccoid_t& oid, uint32_t& next_expires_t0);
};

class DbConnProcessQ : protected DbConnBaseProcessQ
{
	sqlite3_stmt *Process_Q_insert[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_update_queue[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_select[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_select_next[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_update[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_update_priorobj[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_clear[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_count[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_randomize[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_done[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_select_level[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_delete[PROCESS_Q_N];

	void ClearDbPointers()
	{
		CLEAR_DB_POINTERS(Process_Q_insert, Process_Q_delete);
	};

public:
	DbConnProcessQ();
	~DbConnProcessQ();
	void DoProcessQFinish(unsigned type);

	static void IncrementQueuedWork(unsigned type, unsigned changes);
	static void WaitForQueuedWork(unsigned type);
	static void StopQueuedWork(unsigned type);

	int ProcessQEnqueueValidate(unsigned type, SmartBuf smartobj, const ccoid_t *prior_oid, int64_t level, unsigned status, Process_Q_Priority priority, bool is_block_tx, unsigned conn_index, uint32_t callback_id);
	int ProcessQGetNextValidateObj(unsigned type, SmartBuf *retobj, unsigned& conn_index, uint32_t& callback_id);
	int ProcessQUpdateSubsequentBlockStatus(unsigned type, const ccoid_t& oid);

	int ProcessQUpdateObj(unsigned type, const ccoid_t& oid, int status, int64_t auxint);
	int ProcessQClearValidObjs(unsigned type);
	int ProcessQCountValidObjs(unsigned type, int64_t auxint);
	int ProcessQRandomizeValidObjs(unsigned type);
	int ProcessQGetNextValidObj(unsigned type, unsigned offset, SmartBuf *retobj);

	int ProcessQDone(unsigned type, int64_t level);
	int ProcessQPruneLevel(unsigned type, int64_t level);
	int ProcessQSelectAndDelete(unsigned type, const ccoid_t& oid, unsigned& block_tx_count, unsigned& conn_index, uint32_t& callback_id);
};

class DbConnValidObjs : protected DbConnBaseValidObjs
{
	sqlite3_stmt *Valid_Objs_insert;
	sqlite3_stmt *Valid_Objs_select_obj;
	sqlite3_stmt *Valid_Objs_select_seqnums;
	sqlite3_stmt *Valid_Objs_delete_seqnum;
	sqlite3_stmt *Valid_Objs_delete_obj;

	void ClearDbPointers()
	{
		CLEAR_DB_POINTERS(Valid_Objs_insert, Valid_Objs_delete_obj);
	};

public:
	DbConnValidObjs();
	~DbConnValidObjs();
	void DoValidObjsFinish();

	struct ValidObjSeqnumObjPair
	{
		int64_t seqnum;
		SmartBuf smartobj;
	};

	int ValidObjsInsert(SmartBuf smartobj, int64_t* pseqnum = NULL);
	int ValidObjsGetObj(const ccoid_t& oid, SmartBuf *retobj, bool or_greater = false);
	unsigned ValidObjsFindNew(int64_t& next_seqnum, int64_t max_seqnum, unsigned limit, bool want_msgs, uint8_t *output, unsigned bufsize);

	int ValidObjsDeleteObj(SmartBuf smartobj);
	int ValidObjsDeleteSeqnum(int64_t seqnum);
	int ValidObjsGetExpires(int64_t min_seqnum, int64_t max_seqnum, int64_t& next_expires_seqnum, SmartBuf *retobj, uint32_t& next_expires_t0);
};

class DbConnXreqs : protected DbConnBaseXreqs
{
	sqlite3_stmt *Xreqs_insert;
	sqlite3_stmt *Xreqs_delete;
	sqlite3_stmt *Xreqs_select_seqnum;
	sqlite3_stmt *Xreqs_select_xreqnum;
	sqlite3_stmt *Xreqs_select_objid;
	sqlite3_stmt *Xreqs_select_expire;
	sqlite3_stmt *Xreqs_select_unique_foreign_address;
	sqlite3_stmt *Xreqs_select_open_rate_required;
	sqlite3_stmt *Xreqs_select_pending_match_rate_ascending;
	sqlite3_stmt *Xreqs_select_pending_match_rate_descending;
	sqlite3_stmt *Xreqs_select_pending_match;
	sqlite3_stmt *Xreqs_clear_pending_matches;
	sqlite3_stmt *Xreqs_set_recalc;
	sqlite3_stmt *Xreqs_init_matching;
	sqlite3_stmt *Xreqs_select_pair_base;
	sqlite3_stmt *Xreqs_select_pair_quote;
	sqlite3_stmt *Xreqs_select_major;
	sqlite3_stmt *Xreqs_select_minor;
	sqlite3_stmt *Xreqs_select_nonwitness_match;
	sqlite3_stmt *Xreqs_select_witness_match_seqnum;
	sqlite3_stmt *Xreqs_select_witness_match_xreqnum;
	sqlite3_stmt *Xreqs_update;

	static atomic<unsigned> xreq_count_pending;
	static atomic<unsigned> xreq_count_persistent;

	void ClearDbPointers()
	{
		CLEAR_DB_POINTERS(Xreqs_insert, Xreqs_update);
	};

	int XreqsInsertWithLock(const Xreq& xreq);
	int XreqsDeleteWithLock(const Xreq& xreq);
	int XreqsSelectObjIdWithLock(const ccoid_t& objid, bool for_witness, Xreq& xreq);

	int XreqsSelect(sqlite3_stmt *select, const bool for_witness, Xreq& xreq);
	int XreqsSelectInternal(sqlite3_stmt *select, const bool for_witness, Xreq& xreq, unsigned cs = 0);

	int XreqsSelectRateInternal(sqlite3_stmt* select, const Xreq& xreq, unsigned matching_type, unsigned maxret, unsigned offset, Xreq *xreqs, bool *have_more);

public:
	DbConnXreqs();
	~DbConnXreqs();
	void DoXreqsFinish();

	static unsigned XreqCountPending()
	{
		return xreq_count_pending.load();
	}

	static unsigned XreqCountPersistent()
	{
		return xreq_count_persistent.load();
	}

	int XreqsInsert(Xreq& xreq);
	int XreqsUpdate(const Xreq& xreq);
	int XreqsDelete(const Xreq& xreq);

	int XreqsSelectUniqueForeignAddress(uint64_t blockchain, const string& foreign_address);
	int XreqsSelectSeqnum(const int64_t seqnum, bool for_witness, Xreq& xreq, bool or_greater = false);
	int XreqsSelectObjId(const ccoid_t& objid, bool for_witness, Xreq& xreq);
	int XreqsSelectExpire(const uint64_t expire_time, Xreq& xreq);
	int XreqsSelectXreqnum(uint64_t xreqnum, Xreq& xreq, unsigned type = 0);
	int XreqsSelectOpenRateRequired(const Xreq& xreq, unsigned matching_type, unsigned maxret, unsigned offset, bool include_pending_matched, Xreq *xreqs, bool *have_more);
	int XreqsSelectPendingMatchRate(const Xreq& xreq, unsigned matching_type, unsigned maxret, unsigned offset, Xreq *xreqs, bool *have_more);
	int XreqsClearOldPendingMatches(const uint64_t match_epoch, const uint64_t max_xreqnum);

	int XreqsSelectPairBase(const Xreq& xreq, Xreq& xreq_out);
	int XreqsSelectPairQuote(const Xreq& xreq, Xreq& xreq_out);
	int MatchingSelectMajor(const Xreq& xreq, Xreq& xreq_out);
	int MatchingSelectMinor(const Xreq& xreq, Xreq& xreq_out);
	int MatchingSelectMatch(const Xreq& xreq, Xreq& major, Xreq& minor);

	int MatchingInit(const uint64_t block_time, const bool first_pass, const uint64_t last_matched_num, const uint64_t max_xreqnum, const bool for_witness);

	static void SearchInitPairBase(const bool for_witness, uint64_t max_xreqnum, Xreq& xreq);
	static void SearchInitPairQuote(const Xreq& base, Xreq& xreq);

	static void SearchAdvancePairBase(const Xreq& match, Xreq& xreq);
	static void SearchAdvancePairQuote(const Xreq& match, Xreq& xreq);

	static void MatchingInitTypeRange(unsigned type, bool for_query, Xreq& xreq);

	static void MatchingInitMajor(const Xreq& pair, Xreq& xreq);
	static void MatchingInitMinor(const Xreq& major, Xreq& xreq);

	static void MatchingAdvanceMajor(const Xreq& match, Xreq& xreq);
	static void MatchingAdvanceMinor(const Xreq& match, Xreq& xreq);

	void MatchingInitScan(const bool for_witness, Xreq& xreq);
	void MatchingAdvanceScan(const Xreq& match, Xreq& xreq);
	bool MatchingCheckRestartScan(bool not_found, const Xreq& match, Xreq& xreq);

	int MatchingSelectNextPendingMatch(Xreq& major, Xreq& minor);
};

// bundle them all together so each DbConn can talk to all databases
class DbConn : public DbConnPersistData, public DbConnTempSerials, public DbConnRelayObjs, public DbConnProcessQ, public DbConnValidObjs, public DbConnXreqs
{
};

// DbInit is used only to open/create the databases when the program starts up
class DbInit : DbConnBasePersistData, DbConnBaseTempSerials, DbConnBaseRelayObjs, DbConnBaseProcessQ, DbConnBaseValidObjs, DbConnBaseXreqs
{
public:
	void CreateDBs();
	void OpenDbs();

	void DeInit();

	void InitDb();
	void CheckDb();
};

void DbExplainQueryPlan(const string& explainer, sqlite3_stmt *pStmt);
void DbFinalize(sqlite3_stmt *pStmt, bool explain);

#if TEST_EXPLAIN_DB_QUERIES
extern mutex g_db_explain_lock;
#endif
