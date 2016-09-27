/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dbconn.hpp
*/

#pragma once

#include "relay_request_params.h"

#include <SmartBuf.hpp>
#include <CCobjdefs.h>
#include <sqlite/sqlite3.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/locks.hpp>

//#define TEST_EXPLAIN_DB_QUERIES	1	// for testing

//#define TEST_RANDOM_DB_ERRORS		63
//#define TEST_DELAY_DB_RESET		31

#ifndef TEST_EXPLAIN_DB_QUERIES
#define TEST_EXPLAIN_DB_QUERIES		0	// don't test
#endif

#ifndef TEST_RANDOM_DB_ERRORS
#define TEST_RANDOM_DB_ERRORS 0	// don't test
#endif

#ifndef TEST_DELAY_DB_RESET
#define TEST_DELAY_DB_RESET 0	// don't test
#endif

#define VALID_BLOCK_SEQNUM_START	(INT64_MIN/2+1)

#define RELAY_STATUS_ANNOUNCED		0
#define RELAY_STATUS_DOWNLOADED		1

#define RELAY_PEER_STATUS_READY		0
#define RELAY_PEER_STATUS_STARTED	1

#define PROCESS_Q_TYPE_BLOCK		0
#define PROCESS_Q_TYPE_TX			1
#define PROCESS_Q_N					2

#define PROCESS_Q_STATUS_PENDING	0
#define PROCESS_Q_STATUS_HOLD		1
#define PROCESS_Q_STATUS_VALID		2
#define PROCESS_Q_STATUS_DONE		3

#define PERSISTENT_SERIALS_TYPE		0
#define TEMP_SERIALS_PROCESS_BLOCKP	1
#define TEMP_SERIALS_WITNESS_BLOCKP	2
#define TEMP_SERIALS_SPECIAL_BLOCKP	TEMP_SERIALS_WITNESS_BLOCKP


class DbConnBasePersistData
{
public:
	sqlite3 *Persistent_db;

	void OpenDb();
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

	void OpenDb();
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

	void OpenDb();
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

	void OpenDb(unsigned type);
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

	void OpenDb();
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
	void WalWaitForFullCheckpoint();
};

class DbConnPersistData : protected DbConnBasePersistData
{
	sqlite3_stmt *Persistent_Data_begin_read;
	sqlite3_stmt *Persistent_Data_rollback;
	sqlite3_stmt *Persistent_Data_begin_write;
	sqlite3_stmt *Persistent_Data_commit;
	sqlite3_stmt *Parameters_insert;
	sqlite3_stmt *Parameters_select;
	sqlite3_stmt *Blockchain_insert;
	sqlite3_stmt *Blockchain_select_max;
	sqlite3_stmt *Blockchain_select;
	sqlite3_stmt *Serialnum_insert;
	sqlite3_stmt *Serialnum_check;
	sqlite3_stmt *Commit_Tree_insert;
	sqlite3_stmt *Commit_Tree_select;
	sqlite3_stmt *Commit_Roots_insert;
	sqlite3_stmt *Commit_Roots_select;
	sqlite3_stmt *Tx_Outputs_insert;
	sqlite3_stmt *Tx_Outputs_select;

	static WalDB Persistent_Wal;

public:
	DbConnPersistData();
	~DbConnPersistData();
	void DoPersistentDataFinish();

	int BeginRead();
	int EndRead();

	int BeginWrite();
	int EndWrite(bool keep_mutex = false);
	bool ThisThreadHoldsMutex();
	void ReleaseMutex();

	int ParameterInsert(int key, int subkey, void *value, unsigned valsize);
	int ParameterSelect(int key, int subkey, void *value, unsigned bufsize, unsigned *retsize = NULL);
	int BlockchainInsert(uint64_t level, SmartBuf smartobj);
	int BlockchainSelect(uint64_t level, SmartBuf *retobj);
	int BlockchainSelectMax(uint64_t& level);
	int SerialnumInsert(const void *serial, unsigned size);
	int SerialnumCheck(const void *serial, unsigned size);
	int CommitTreeInsert(unsigned height, uint64_t offset, const void *data, unsigned datasize);
	int CommitTreeSelect(unsigned height, uint64_t offset, void *data, unsigned datasize);
	int CommitRootsInsert(uint64_t level, uint64_t timestamp, const void *hash, unsigned hashsize);
	int CommitRootsSelect(uint64_t level, bool or_greater, uint64_t& timestamp, void *hash, unsigned hashsize);
	int TxOutputsInsert(const void *addr, unsigned addrsize, uint64_t value_enc, uint64_t param_level, const void *commitment, unsigned commitsize, uint64_t offset);
	int TxOutputsSelect(const void *addr, unsigned addrsize, uint64_t commitnum_start, uint64_t commitnum_end, uint64_t *value_enc, char *commitment_iv, unsigned commitment_ivsize, char *commitment, unsigned commitsize, uint64_t *commitnums, unsigned limit = 1, bool *have_more = NULL);

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

	static void PersistentData_WaitForFullCheckpoint()
	{
		Persistent_Wal.WalWaitForFullCheckpoint();
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

public:
	DbConnTempSerials();
	~DbConnTempSerials();
	void DoTempSerialsFinish();

	int TempSerialnumInsert(const void *serial, unsigned size, const void* blockp);
	//int TempSerialnumDelete(const void *serial, unsigned size, const void* blockp);
	int TempSerialnumSelect(const void *serial, unsigned size, const void* last_blockp, void *output[], unsigned bufsize);
	int TempSerialnumUpdate(const void* old_blockp, const void* new_blockp, uint64_t level);
	int TempSerialnumClear(const void* blockp);
	int TempSerialnumPruneLevel(uint64_t level);
};

class DbConnRelayObjs : protected DbConnBaseRelayObjs
{
	sqlite3_stmt *Relay_Objs_begin;
	sqlite3_stmt *Relay_Objs_rollback;
	sqlite3_stmt *Relay_Objs_commit;
	sqlite3_stmt *Relay_Objs_select_seqnum;
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

public:
	DbConnRelayObjs();
	~DbConnRelayObjs();
	void DoRelayObjsFinish(bool rollback);

	void RelayObjsInsert(unsigned peer, unsigned type, const relay_request_wire_params_t& req_params, unsigned obj_status, unsigned peer_status);
	int RelayObjsFindDownloads(unsigned conn_index, uint64_t tx_level_max, uint8_t *output, unsigned bufsize, relay_request_param_buf_t& req_params, int maxobjs, int64_t bytes_pending, unsigned &nobjs, unsigned &nbytes);
	int RelayObjsSetStatus(const ccoid_t& oid, int obj_status, int timeout);
	int RelayObjsDeletePeer(unsigned peer);

	int RelayObjsDeleteSeqnum(int64_t seqnum);
	int RelayObjsGetExpires(int64_t min_seqnum, int64_t max_seqnum, int64_t& next_expires_seqnum, uint32_t& next_expires_t0);
};

class DbConnProcessQ : protected DbConnBaseProcessQ
{
	sqlite3_stmt *Process_Q_insert[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_begin[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_rollback[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_commit[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_select[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_update[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_update_priorobj[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_clear[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_count[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_randomize[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_done[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_select_level[PROCESS_Q_N];
	sqlite3_stmt *Process_Q_delete[PROCESS_Q_N];

public:
	DbConnProcessQ();
	~DbConnProcessQ();
	void DoProcessQFinish(unsigned type, bool rollback, bool increment_work);

	static void IncrementQueuedWork(unsigned type, unsigned changes = 1);
	static void WaitForQueuedWork(unsigned type);
	static void StopQueuedWork(unsigned type);

	int ProcessQEnqueueValidate(unsigned type, SmartBuf smartobj, const ccoid_t *prior_oid, int64_t level, unsigned status, int64_t priority, unsigned conn_index, uint64_t callback_id);
	int ProcessQGetNextValidateObj(unsigned type, SmartBuf *retobj, unsigned& conn_index, unsigned& callback_id);
	int ProcessQUpdateSubsequentBlockStatus(unsigned type, const ccoid_t& oid);

	int ProcessQUpdateValidObj(unsigned type, const ccoid_t& oid, int status, int64_t auxint);
	int ProcessQClearValidObjs(unsigned type);
	int ProcessQCountValidObjs(unsigned type, int64_t auxint);
	int ProcessQRandomizeValidObjs(unsigned type);
	int ProcessQGetNextValidObj(unsigned type, unsigned offset, SmartBuf *retobj);

	int ProcessQDone(unsigned type, int64_t level);
	int ProcessQPruneLevel(unsigned type, int64_t level);
};

class DbConnValidObjs : protected DbConnBaseValidObjs
{
	sqlite3_stmt *Valid_Objs_insert;
	sqlite3_stmt *Valid_Objs_select_obj;
	sqlite3_stmt *Valid_Objs_select_new;
	sqlite3_stmt *Valid_Objs_select_oldest;
	sqlite3_stmt *Valid_Objs_delete_seqnum;
	sqlite3_stmt *Valid_Objs_delete_obj;

public:
	DbConnValidObjs();
	~DbConnValidObjs();
	void DoValidObjsFinish();

	static int64_t GetNextTxSeqnum();

	int ValidObjsInsert(SmartBuf smartobj);
	int ValidObjsGetObj(const ccoid_t& oid, SmartBuf *retobj);
	unsigned ValidObjsFindNew(int64_t& next_block_seqnum, int64_t& next_tx_seqnum, uint8_t *output, unsigned bufsize);

	int ValidObjsDeleteObj(SmartBuf smartobj);
	int ValidObjsDeleteSeqnum(int64_t seqnum);
	int ValidObjsGetExpires(int64_t min_seqnum, int64_t max_seqnum, int64_t& next_expires_seqnum, SmartBuf *retobj, uint32_t& next_expires_t0);
};

// bundle them all together so each DbConn can talk to all databases
class DbConn : public DbConnPersistData, public DbConnTempSerials, public DbConnRelayObjs, public DbConnProcessQ, public DbConnValidObjs
{
};

// DbInit is used only to open/create the databases when the program starts up
class DbInit : DbConnBasePersistData, DbConnBaseTempSerials, DbConnBaseRelayObjs, DbConnBaseProcessQ, DbConnBaseValidObjs
{
public:
	void CreateDBs();
	void OpenDbs();

	void DeInit();
};

void DbExplainQueryPlan(sqlite3_stmt *pStmt);
void DbFinalize(sqlite3_stmt *pStmt, bool explain);

extern mutex g_db_explain_lock;
