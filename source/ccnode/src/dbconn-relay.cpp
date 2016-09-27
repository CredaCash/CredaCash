/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dbconn-relay.cpp
*/

#include "CCdef.h"
#include "dbconn.hpp"
#include "witness.hpp"

#include <dblog.h>
#include <CCobjects.hpp>
#include <CCobjdefs.h>
#include <Finally.hpp>
#include <CCutil.h>

#define TRACE_DBCONN	(g_params.trace_relay_db)

//#define TEST_SEND_TO_SELF		1	// if set to 1, allows relay to download objects it has already downloaded

#ifndef TEST_SEND_TO_SELF
#define TEST_SEND_TO_SELF		0	// don't test
#endif

#define RELAY_DOWLOAD_RETRY_SECS			5
#define RELAY_DOWLOAD_RETRY_BYTES_PER_SEC	2000
#define RELAY_DOWNLOAD_TIME_MAX				15

static atomic<int64_t> g_relay_block_seqnum(VALID_BLOCK_SEQNUM_START);
static atomic<int64_t> g_relay_tx_seqnum(1);

static mutex Relay_Objs_db_mutex; 	// to avoid SQLITE_LOCKED errors when writing to database

DbConnRelayObjs::DbConnRelayObjs()
{
	lock_guard<mutex> lock(Relay_Objs_db_mutex);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::DbConnRelayObjs dbconn " << (uintptr_t)this;

	OpenDb();

	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "begin exclusive;", -1, &Relay_Objs_begin, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "rollback;", -1, &Relay_Objs_rollback, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "commit;", -1, &Relay_Objs_commit, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "select Seqnum, Status from Relay_Objs where ObjId = ?1;", -1, &Relay_Objs_select_seqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "insert into Relay_Objs (Seqnum, Time, ObjId, Status) values (?1, ?2, ?3, ?4);", -1, &Relay_Objs_insert, NULL)));
#if TEST_SEND_TO_SELF
	// for testing the relay: download objects even if they're already been downloaded:
	#error needs to be implemented
#else
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "select Seqnum, Time, ObjId, Size, Level, PriorOid, Witness from Relay_Peers, Relay_Objs using (Seqnum) where Peer = ?1 and PeerStatus = " STRINGIFY(RELAY_PEER_STATUS_READY) " and Status = " STRINGIFY(RELAY_STATUS_ANNOUNCED) " and (Seqnum < 0 or (Seqnum > 0 and Level <= ?2)) and strftime('%s','now') >= Timeout order by Seqnum limit ?3;", -1, &Relay_Objs_select_download, NULL)));
#endif
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "update Relay_Objs set Status = ?2, Timeout = strftime('%s','now') + ?3 where ObjId = ?1;", -1, &Relay_Objs_update, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "select Seqnum, Time from Relay_Objs where Seqnum >= ?1 and Seqnum <= ?2 order by Seqnum limit 1;", -1, &Relay_Objs_select_oldest, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "delete from Relay_Objs where Seqnum = ?1;", -1, &Relay_Objs_delete_seqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "delete from Relay_Objs where ObjId = ?1;", -1, &Relay_Objs_delete_oid, NULL)));

	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "insert into Relay_Peers (Seqnum, Peer, Size, Level, PeerStatus, PriorOid, Witness) values (?1, ?2, ?3, ?4, ?5, ?6, ?7);", -1, &Relay_Peers_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "update Relay_Peers set PeerStatus = ?2 where Seqnum = ?1;", -1, &Relay_Peers_update_seqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "delete from Relay_Peers where Seqnum = ?1;", -1, &Relay_Peers_delete_seqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Relay_Objs_db, "delete from Relay_Peers where Peer = ?1;", -1, &Relay_Peers_delete_peer, NULL)));

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::DbConnRelayObjs dbconn done " << (uintptr_t)this;
}

DbConnRelayObjs::~DbConnRelayObjs()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::~DbConnRelayObjs dbconn " << (uintptr_t)this;

	static bool explain = TEST_EXPLAIN_DB_QUERIES;

#if TEST_EXPLAIN_DB_QUERIES
	unique_lock<mutex> elock(g_db_explain_lock);

	if (!explain)
		elock.unlock();

	lock_guard<mutex> lock(Relay_Objs_db_mutex);
#endif

	//if (explain)
	//	CCASSERTZ(dbexec(Relay_Objs_db, "analyze;"));

	DbFinalize(Relay_Objs_begin, explain);
	DbFinalize(Relay_Objs_rollback, explain);
	DbFinalize(Relay_Objs_commit, explain);
	DbFinalize(Relay_Objs_select_seqnum, explain);
	DbFinalize(Relay_Objs_insert, explain);
	DbFinalize(Relay_Objs_select_download, explain);
	DbFinalize(Relay_Objs_update, explain);

	DbFinalize(Relay_Objs_select_oldest, explain);
	DbFinalize(Relay_Objs_delete_seqnum, explain);
	DbFinalize(Relay_Objs_delete_oid, explain);

	DbFinalize(Relay_Peers_insert, explain);
	DbFinalize(Relay_Peers_update_seqnum, explain);
	DbFinalize(Relay_Peers_delete_seqnum, explain);
	DbFinalize(Relay_Peers_delete_peer, explain);

	explain = false;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::~DbConnRelayObjs done dbconn " << (uintptr_t)this;
}

void DbConnRelayObjs::DoRelayObjsFinish(bool rollback)
{
	if ((TEST_DELAY_DB_RESET & rand()) == 1) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::DoRelayObjsFinish dbconn " << uintptr_t(this) << " rollback " << rollback;

	sqlite3_reset(Relay_Objs_select_seqnum);
	sqlite3_reset(Relay_Objs_insert);
	sqlite3_reset(Relay_Objs_select_download);
	sqlite3_reset(Relay_Objs_update);

	sqlite3_reset(Relay_Objs_select_oldest);
	sqlite3_reset(Relay_Objs_delete_seqnum);
	sqlite3_reset(Relay_Objs_delete_oid);

	sqlite3_reset(Relay_Peers_insert);
	sqlite3_reset(Relay_Peers_update_seqnum);
	sqlite3_reset(Relay_Peers_delete_seqnum);
	sqlite3_reset(Relay_Peers_delete_peer);

	sqlite3_reset(Relay_Objs_commit);
	sqlite3_reset(Relay_Objs_begin);
	sqlite3_reset(Relay_Objs_rollback);

	if (rollback)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::DoRelayObjsFinish dbconn " << uintptr_t(this) << " rollback";

		dblog(sqlite3_step(Relay_Objs_rollback), DB_STMT_STEP);
		sqlite3_reset(Relay_Objs_rollback);
	}
}

void DbConnRelayObjs::RelayObjsInsert(unsigned peer, unsigned type, const relay_request_wire_params_t& req_params, unsigned obj_status, unsigned peer_status)
{
	lock_guard<mutex> lock(Relay_Objs_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnRelayObjs::DoRelayObjsFinish, this, 1));		// 1 = rollback

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsInsert peer Conn-" << peer << " type " << type << " oid " << buf2hex(&req_params.oid, sizeof(ccoid_t)) << " size " << req_params.size << " level " << req_params.level << " obj status " << obj_status << " peer status " << peer_status;

	// BEGIN

	if (dblog(sqlite3_step(Relay_Objs_begin), DB_STMT_STEP)) return;

	// check if ObjId already in db

	if (dblog(sqlite3_bind_blob(Relay_Objs_select_seqnum, 1, &req_params.oid, sizeof(ccoid_t), SQLITE_STATIC))) return;

	int rc;
	int64_t seqnum;

	if (dblog(rc = sqlite3_step(Relay_Objs_select_seqnum), DB_STMT_SELECT)) return;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsInsert simulating database error post-select";

		return;
	}

	if (rc == SQLITE_ROW)
	{
		if (sqlite3_data_count(Relay_Objs_select_seqnum) != 2)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsInsert Relay_Objs_select_seqnum returned " << sqlite3_data_count(Relay_Objs_select_seqnum) << " columns";

			return;
		}

		// fetch existing Seqnum

		seqnum = sqlite3_column_int64(Relay_Objs_select_seqnum, 0);
		obj_status = sqlite3_column_int(Relay_Objs_select_seqnum, 1);

		if (dblog(sqlite3_extended_errcode(Relay_Objs_db), DB_STMT_SELECT)) return;	// check if error retrieving results

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsInsert found existing seqnum " << seqnum << " obj status " << obj_status;

		if (obj_status == RELAY_STATUS_DOWNLOADED && !TEST_SEND_TO_SELF)
		{
			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsInsert seqnum " << seqnum << " already downloaded";

			return;
		}
	}
	else if (rc == SQLITE_DONE)
	{
		// add ObjId to db with new Seqnum

		if (type == CC_TAG_BLOCK)
			seqnum = g_relay_block_seqnum.fetch_add(1);
		else
			seqnum = g_relay_tx_seqnum.fetch_add(1);

		auto ticks = ccticks();

		// Seqnum, Time, ObjId, Status
		if (dblog(sqlite3_bind_int64(Relay_Objs_insert, 1, seqnum))) return;
		if (dblog(sqlite3_bind_int(Relay_Objs_insert, 2, ticks))) return;
		if (dblog(sqlite3_bind_blob(Relay_Objs_insert, 3, &req_params.oid, sizeof(ccoid_t), SQLITE_STATIC))) return;
		if (dblog(sqlite3_bind_int(Relay_Objs_insert, 4, obj_status))) return;

		if (dblog(sqlite3_step(Relay_Objs_insert), DB_STMT_STEP)) return;

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsInsert adding new seqnum " << seqnum << " for obj tag " << type << " announced " << ticks;
	}
	else
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsInsert Relay_Objs_select_seqnum returned " << rc;

		return;
	}

	if (peer)
	{
		// Seqnum, Peer, Size, Level, PeerStatus, PriorOid, Witness
		if (dblog(sqlite3_bind_int64(Relay_Peers_insert, 1, seqnum))) return;
		if (dblog(sqlite3_bind_int(Relay_Peers_insert, 2, peer))) return;
		if (dblog(sqlite3_bind_int64(Relay_Peers_insert, 3, req_params.size))) return;
		if (dblog(sqlite3_bind_int64(Relay_Peers_insert, 4, req_params.level))) return;
		if (dblog(sqlite3_bind_int(Relay_Peers_insert, 5, peer_status))) return;
		if (type == CC_TAG_BLOCK)
		{
			if (dblog(sqlite3_bind_blob(Relay_Peers_insert, 6, &req_params.prior_oid, sizeof(ccoid_t), SQLITE_STATIC))) return;
			if (dblog(sqlite3_bind_int(Relay_Peers_insert, 7, req_params.witness))) return;
		}
		else
		{
			if (dblog(sqlite3_bind_null(Relay_Peers_insert, 6))) return;
			if (dblog(sqlite3_bind_null(Relay_Peers_insert, 7))) return;
		}

		auto rc = sqlite3_step(Relay_Peers_insert);
		if (rc == SQLITE_CONSTRAINT)
		{
			// peer sent CC_MSG_HAVE_BLOCK or CC_MSG_HAVE_TX more than once?

			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(debug) << "DbConnRelayObjs::RelayObjsInsert constraint violation; peer announced object more than once?";

			return;
		}
		else if (dblog(rc, DB_STMT_STEP)) return;
	}

	// COMMIT

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsInsert simulating database error pre-commit";

		return;
	}

	if (dblog(sqlite3_step(Relay_Objs_commit), DB_STMT_STEP)) return;

	DoRelayObjsFinish(0);	// don't rollback

	finally.Clear();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsInsert success";
}

int DbConnRelayObjs::RelayObjsFindDownloads(unsigned conn_index, uint64_t tx_level_max, uint8_t *output, unsigned bufsize, relay_request_param_buf_t& req_params, int maxobjs, int64_t bytes_pending, unsigned &nobjs, unsigned &nbytes)
{
	nobjs = 0;
	nbytes = 0;

	if (maxobjs <= 0)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnRelayObjs::RelayObjsFindDownloads maxobjs = " << maxobjs << " bytes_pending " << bytes_pending;

		return -1;
	}

	uint64_t total_size = bytes_pending;
	int timeout = RELAY_DOWLOAD_RETRY_SECS + total_size/RELAY_DOWLOAD_RETRY_BYTES_PER_SEC;

	if (timeout >= RELAY_DOWNLOAD_TIME_MAX)
	{
		BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsFindDownloads bytes_pending " << bytes_pending << " timeout " << timeout;

		return 1;
	}

	lock_guard<mutex> lock(Relay_Objs_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnRelayObjs::DoRelayObjsFinish, this, 1));		// 1 = rollback

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsFindDownloads peer Conn-" << conn_index << " max objs " << maxobjs;

	// BEGIN

	if (dblog(sqlite3_step(Relay_Objs_begin), DB_STMT_STEP)) return -1;

	// SELECT entries from Relay_Objs --> Peer, maxobj

	if (dblog(sqlite3_bind_int64(Relay_Objs_select_download, 1, conn_index))) return -1;
	if (dblog(sqlite3_bind_int64(Relay_Objs_select_download, 2, tx_level_max))) return -1;
	if (dblog(sqlite3_bind_int(Relay_Objs_select_download, 3, maxobjs))) return -1;

	bool have_blocks = false;
	int nfound = 0;
	uint32_t bufpos = 0;

	while (nfound < maxobjs)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Relay_Objs_select_download), DB_STMT_SELECT)) return -1;

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsFindDownloads simulating database error post-select";

			return -1;
		}

		if (rc == SQLITE_DONE)
			break;

		if (rc != SQLITE_ROW)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsFindDownloads select returned " << rc;

			return -1;
		}

		if (sqlite3_data_count(Relay_Objs_select_download) != 7)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsFindDownloads select returned " << sqlite3_data_count(Relay_Objs_select_download) << " columns";

			return -1;
		}

		// Seqnum, Time, ObjId, Size, Level, PriorOid, Witness
		int64_t seqnum = sqlite3_column_int64(Relay_Objs_select_download, 0);
		uint32_t announce_time = sqlite3_column_int(Relay_Objs_select_download, 1);
		auto objid_blob = sqlite3_column_blob(Relay_Objs_select_download, 2);
		int64_t size = sqlite3_column_int64(Relay_Objs_select_download, 3);
		int64_t level = sqlite3_column_int64(Relay_Objs_select_download, 4);
		auto priorid_blob = sqlite3_column_blob(Relay_Objs_select_download, 5);
		int witness = sqlite3_column_int64(Relay_Objs_select_download, 6);

		if (seqnum < 0)
			have_blocks = true;
		else if (have_blocks)	// don't mix Blocks and Tx's
			break;

		if (!objid_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsFindDownloads ObjId is null";

			return -1;
		}
		else if (sqlite3_column_bytes(Relay_Objs_select_download, 2) != sizeof(ccoid_t))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsFindDownloads ObjId size " << sqlite3_column_bytes(Relay_Objs_select_download, 2) << " != " << sizeof(ccoid_t);

			return -1;
		}

		if (priorid_blob && sqlite3_column_bytes(Relay_Objs_select_download, 5) != sizeof(ccoid_t))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsFindDownloads Prior_ObjId size " << sqlite3_column_bytes(Relay_Objs_select_download, 5) << " != " << sizeof(ccoid_t);

			return -1;
		}

		if (dblog(sqlite3_extended_errcode(Relay_Objs_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsFindDownloads simulating database error post-error check";

			return -1;
		}

		total_size += size;
		timeout = RELAY_DOWLOAD_RETRY_SECS + total_size/RELAY_DOWLOAD_RETRY_BYTES_PER_SEC;

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsFindDownloads found seqnum " << seqnum << " announced " << announce_time << " peer Conn-" << conn_index << " oid " << buf2hex(objid_blob, sizeof(ccoid_t)) << " size " << size << " total size " << total_size << " timeout " << timeout;

		// UPDATE PeerStatus so the object will not get downloaded again from this peer

		// Seqnum, PeerStatus
		if (dblog(sqlite3_bind_int64(Relay_Peers_update_seqnum, 1, seqnum))) return -1;
		if (dblog(sqlite3_bind_int(Relay_Peers_update_seqnum, 2, RELAY_PEER_STATUS_STARTED))) return -1;

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsFindDownloads simulating database error pre-peer status update";

			return -1;
		}

		if (dblog(sqlite3_step(Relay_Peers_update_seqnum), DB_STMT_STEP)) return -1;

		sqlite3_reset(Relay_Peers_update_seqnum);	// release lock

		if (!IsWitness() || seqnum > 0)
		{
			// UPDATE Relay_Objs so the object will not get downloaded again from a different peer until after the timeout
			// note: if sqlite "read uncommitted" is disabled, this update to the timeout in Relay_Objs will not been seen until after the sqlite transaction commits,
			//		and as a result, the same Block or Tx may get selected for download twice inside this sqlite transaction

			// ObjId, Status, Timeout
			if (dblog(sqlite3_bind_blob(Relay_Objs_update, 1, objid_blob, sizeof(ccoid_t), SQLITE_STATIC))) return -1;
			if (dblog(sqlite3_bind_int(Relay_Objs_update, 2, RELAY_STATUS_ANNOUNCED))) return -1;
			if (dblog(sqlite3_bind_int(Relay_Objs_update, 3, timeout))) return -1;

			if (dblog(sqlite3_step(Relay_Objs_update), DB_STMT_STEP)) return -1;

			if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
			{
				BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsFindDownloads simulating database error post-update";

				return -1;
			}

			sqlite3_reset(Relay_Objs_update);	// release lock
		}

		// output object in SEND command

		BOOST_LOG_TRIVIAL(debug) << "DbConnRelayObjs::RelayObjsFindDownloads preparing to send CC_CMD_SEND_BLOCK/CC_CMD_SEND_TX seqnum " << seqnum << " oid " << buf2hex(objid_blob, sizeof(ccoid_t));

		if (!bufpos)
		{
			copy_to_buf(&bufpos, sizeof(bufpos), bufpos, output, bufsize);  // save space for size word

			uint32_t tag = seqnum < 0 ? CC_CMD_SEND_BLOCK : CC_CMD_SEND_TX;

			copy_to_buf(&tag, sizeof(tag), bufpos, output, bufsize);
		}

		copy_to_buf(objid_blob, sizeof(ccoid_t), bufpos, output, bufsize);

		memcpy(&req_params[nfound].oid, objid_blob, sizeof(ccoid_t));
		req_params[nfound].size = size;
		if (priorid_blob)
			memcpy(&req_params[nfound].prior_oid, priorid_blob, sizeof(ccoid_t));
		req_params[nfound].level = level;
		req_params[nfound].witness = witness;
		req_params[nfound].announce_time = announce_time;
		++nfound;

		if (timeout >= RELAY_DOWNLOAD_TIME_MAX)
			break;
	}

	// finish output buffer

	if (bufpos > bufsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsFindDownloads buffer overflow bufpos " << bufpos << " bufsize " << bufsize;

		return -1;
	}

	*(uint32_t*)output = bufpos;		// set size

	// COMMIT

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsFindDownloads simulating database error pre-commit";

		return -1;
	}

	if (dblog(sqlite3_step(Relay_Objs_commit), DB_STMT_STEP)) return -1;

	DoRelayObjsFinish(0);	// don't rollback

	finally.Clear();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsFindDownloads done, bufpos " << bufpos;

	nobjs = nfound;
	nbytes = bufpos;

	return 0;
}

int DbConnRelayObjs::RelayObjsSetStatus(const ccoid_t& oid, int obj_status, int timeout)
{
	lock_guard<mutex> lock(Relay_Objs_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnRelayObjs::DoRelayObjsFinish, this, 0));		// 0 = reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsSetStatus obj status " << obj_status << " oid " << buf2hex(&oid, sizeof(ccoid_t));

	int64_t seqnum = -1;

	while (obj_status == RELAY_STATUS_DOWNLOADED)	// use "while" so we can use "break" if there's an error
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsSetStatus fetching seqnum for obj with status " << obj_status;

		if (dblog(sqlite3_bind_blob(Relay_Objs_select_seqnum, 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) break;

		int rc;

		if (dblog(rc = sqlite3_step(Relay_Objs_select_seqnum), DB_STMT_SELECT)) break;

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsSetStatus simulating database error post-select";

			break;
		}

		if (rc == SQLITE_DONE)	// should only happen if there was an earlier db error; if so, the update below will also result in changes == 0 error and log a warning
			break;

		if (rc == SQLITE_ROW)
		{
			if (sqlite3_data_count(Relay_Objs_select_seqnum) != 2)
			{
				BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsSetStatus Relay_Objs_select_seqnum returned " << sqlite3_data_count(Relay_Objs_select_seqnum) << " columns";

				break;
			}

			// fetch existing Seqnum

			auto s = sqlite3_column_int64(Relay_Objs_select_seqnum, 0);

			if (dblog(sqlite3_extended_errcode(Relay_Objs_db), DB_STMT_SELECT)) break;	// check if error retrieving results

			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsSetStatus found seqnum " << s;

			seqnum = s;
		}
		else
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsSetStatus Relay_Objs_select_seqnum returned " << rc;
		}

		break;
	}

	sqlite3_reset(Relay_Objs_select_seqnum);

	// ObjId, Status, Timeout
	if (dblog(sqlite3_bind_blob(Relay_Objs_update, 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_int(Relay_Objs_update, 2, obj_status))) return -1;
	if (dblog(sqlite3_bind_int(Relay_Objs_update, 3, timeout))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsSetStatus simulating database error pre-update";

		return -1;
	}

	if (dblog(sqlite3_step(Relay_Objs_update), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Relay_Objs_db);

	if (changes == 1)
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsSetStatus set obj status = " << obj_status << " for oid " << buf2hex(&oid, sizeof(ccoid_t));
	}
	else if (changes == 0)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnRelayObjs::RelayObjsSetStatus sqlite3_changes " << changes << " in Relay_Objs after update of oid " << buf2hex(&oid, sizeof(ccoid_t));
	}
	else
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsSetStatus sqlite3_changes " << changes << " in Relay_Objs after update of oid " << buf2hex(&oid, sizeof(ccoid_t));
	}

	// delete Tx from Relay_Peers but keep blocks to prevent peer from swamping us with many blocks at the same level
	while (obj_status == RELAY_STATUS_DOWNLOADED && seqnum > 0)	// use "while" so we can use "break" if there's an error
	{
		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsSetStatus deleting seqnum " << seqnum << " obj status " << obj_status << " from peers table";

		if (dblog(sqlite3_bind_int64(Relay_Peers_delete_seqnum, 1, seqnum))) break;

		if (dblog(sqlite3_step(Relay_Peers_delete_seqnum), DB_STMT_STEP)) break;

		// the download peer is deleted when downloading starts, so the # of rows changed could range from zero up; it's only useful to see that some are deleted at times

		auto changes = sqlite3_changes(Relay_Objs_db);

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsSetStatus sqlite3_changes " << changes << " in Relay_Peers after delete seqnum " << seqnum << " obj status " << obj_status;

		break;
	}

	return 0;
}

int DbConnRelayObjs::RelayObjsDeletePeer(unsigned peer)
{
	lock_guard<mutex> lock(Relay_Objs_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnRelayObjs::DoRelayObjsFinish, this, 0));		// 0 = reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsDeletePeer peer Conn-" << peer;

	if (dblog(sqlite3_bind_int(Relay_Peers_delete_peer, 1, peer))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsDeletePeer simulating database error pre-delete";

		return -1;
	}

	if (dblog(sqlite3_step(Relay_Peers_delete_peer), DB_STMT_STEP)) return -1;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsDeletePeer deleted peer Conn-" << peer;

	return 0;
}

int DbConnRelayObjs::RelayObjsDeleteSeqnum(int64_t seqnum)
{
	lock_guard<mutex> lock(Relay_Objs_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnRelayObjs::DoRelayObjsFinish, this, 1));		// 1 = rollback

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsDeleteSeqnum seqnum " << seqnum;

	// BEGIN

	if (dblog(sqlite3_step(Relay_Objs_begin), DB_STMT_STEP)) return -1;

	// DELETE seqnum from Relay_Peers

	if (dblog(sqlite3_bind_int64(Relay_Peers_delete_seqnum, 1, seqnum))) return -1;

	if (dblog(sqlite3_step(Relay_Peers_delete_seqnum), DB_STMT_STEP)) return -1;

	while (true)	// so we can use break on error
	{
		// DELETE seqnum from Relay_Objs

		if (dblog(sqlite3_bind_int64(Relay_Objs_delete_seqnum, 1, seqnum))) break;

		if (dblog(sqlite3_step(Relay_Objs_delete_seqnum), DB_STMT_STEP)) break;

		break;
	}

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsDeleteSeqnum simulating database error pre-commit";

		return -1;
	}

	if (dblog(sqlite3_step(Relay_Objs_commit), DB_STMT_STEP)) return -1;

	DoRelayObjsFinish(0);	// don't rollback

	finally.Clear();

	return 0;
}

int DbConnRelayObjs::RelayObjsGetExpires(int64_t min_seqnum, int64_t max_seqnum, int64_t& next_expires_seqnum, uint32_t& next_expires_t0)
{
	lock_guard<mutex> lock(Relay_Objs_db_mutex);	// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnRelayObjs::DoRelayObjsFinish, this, 0));		// 0 = reset only

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsGetExpires min_seqnum " << min_seqnum << " max_seqnum " << max_seqnum;

	// preset these in case of error
	auto last_expires_seqnum = next_expires_seqnum;
	next_expires_seqnum = -1;

	if (dblog(sqlite3_bind_int64(Relay_Objs_select_oldest, 1, min_seqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Relay_Objs_select_oldest, 2, max_seqnum))) return -1;

	int rc;

	if (dblog(rc = sqlite3_step(Relay_Objs_select_oldest), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsGetExpires simulating database error post-select";

		return -1;
	}

	if (rc == SQLITE_DONE)
		return 1;

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsGetExpires select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Relay_Objs_select_oldest) != 2)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsGetExpires select returned " << sqlite3_data_count(Relay_Objs_select_oldest) << " columns";

		return -1;
	}

	// Seqnum, Bufp, Time
	auto seqnum = sqlite3_column_int64(Relay_Objs_select_oldest, 0);
	auto t0 = sqlite3_column_int(Relay_Objs_select_oldest, 1);

	if (seqnum == last_expires_seqnum)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnRelayObjs::RelayObjsGetExpires select returned seqnum " << seqnum << " which should have already been deleted";

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Relay_Objs_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnRelayObjs::RelayObjsGetExpires simulating database error post-select";

		return -1;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnRelayObjs::RelayObjsGetExpires seqnum " << seqnum << " t0 " << t0;

	next_expires_seqnum = seqnum;
	next_expires_t0 = t0;

	return 0;
}
