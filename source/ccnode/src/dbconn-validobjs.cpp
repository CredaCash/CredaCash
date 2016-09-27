/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dbconn-validobjs.cpp
*/

#include "CCdef.h"
#include "dbconn.hpp"
#include "block.hpp"
#include "witness.hpp"

#include <dblog.h>
#include <CCobjects.hpp>
#include <CCobjdefs.h>
#include <Finally.hpp>
#include <CCutil.h>

#define TRACE_DBCONN	(g_params.trace_validation_q_db)

static atomic<int64_t> g_valid_block_seqnum(VALID_BLOCK_SEQNUM_START);
static atomic<int64_t> g_valid_tx_seqnum(1);

static boost::shared_mutex Valid_Objs_db_mutex; 	// to avoid SQLITE_LOCKED errors when writing to database
//static mutex Valid_Objs_db_mutex; 				// to avoid SQLITE_LOCKED errors when writing to database

int64_t DbConnValidObjs::GetNextTxSeqnum()
{
	return g_valid_tx_seqnum.load();
}

DbConnValidObjs::DbConnValidObjs()
{
	lock_guard<boost::shared_mutex> lock(Valid_Objs_db_mutex);
	//lock_guard<mutex> lock(Valid_Objs_db_mutex);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::DbConnValidObjs dbconn " << (uintptr_t)this;

	OpenDb();

	CCASSERTZ(dblog(sqlite3_prepare_v2(Valid_Objs_db, "insert into Valid_Objs (Seqnum, Time, ObjId, Bufp) values (?1, ?2, ?3, ?4);", -1, &Valid_Objs_insert, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Valid_Objs_db, "select Bufp from Valid_Objs where ObjId = ?1;", -1, &Valid_Objs_select_obj, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Valid_Objs_db, "select Seqnum, ObjId, Bufp from Valid_Objs where (Seqnum < 0 and Seqnum >= ?1) or Seqnum >= ?2 order by Seqnum limit " STRINGIFY(CC_HAVE_MAX) ";", -1, &Valid_Objs_select_new, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Valid_Objs_db, "select Seqnum, Time, Bufp from Valid_Objs where Seqnum >= ?1 and Seqnum <= ?2 order by Seqnum limit 1;", -1, &Valid_Objs_select_oldest, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Valid_Objs_db, "delete from Valid_Objs where Seqnum = ?1;", -1, &Valid_Objs_delete_seqnum, NULL)));
	CCASSERTZ(dblog(sqlite3_prepare_v2(Valid_Objs_db, "delete from Valid_Objs where ObjId = ?1;", -1, &Valid_Objs_delete_obj, NULL)));

	//if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::DbConnValidObjs dbconn done " << (uintptr_t)this;
}

DbConnValidObjs::~DbConnValidObjs()
{
	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::~DbConnValidObjs dbconn " << (uintptr_t)this;

	static bool explain = TEST_EXPLAIN_DB_QUERIES;

#if TEST_EXPLAIN_DB_QUERIES
	unique_lock<mutex> elock(g_db_explain_lock);

	if (!explain)
		elock.unlock();

	lock_guard<boost::shared_mutex> lock(Valid_Objs_db_mutex);
	//lock_guard<mutex> lock(Valid_Objs_db_mutex);
#endif

	//if (explain)
	//	CCASSERTZ(dbexec(Valid_Objs_db, "analyze;"));

	DbFinalize(Valid_Objs_insert, explain);
	DbFinalize(Valid_Objs_select_obj, explain);
	DbFinalize(Valid_Objs_select_new, explain);
	DbFinalize(Valid_Objs_select_oldest, explain);
	DbFinalize(Valid_Objs_delete_seqnum, explain);
	DbFinalize(Valid_Objs_delete_obj, explain);

	explain = false;

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::~DbConnValidObjs done dbconn " << (uintptr_t)this;
}

void DbConnValidObjs::DoValidObjsFinish()
{
	if ((TEST_DELAY_DB_RESET & rand()) == 1) sleep(1);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::DoValidObjsFinish dbconn " << uintptr_t(this);

	sqlite3_reset(Valid_Objs_insert);
	sqlite3_reset(Valid_Objs_select_obj);
	sqlite3_reset(Valid_Objs_select_new);
	sqlite3_reset(Valid_Objs_select_oldest);
	sqlite3_reset(Valid_Objs_delete_seqnum);
	sqlite3_reset(Valid_Objs_delete_obj);
}

int DbConnValidObjs::ValidObjsInsert(SmartBuf smartobj)
{
	lock_guard<boost::shared_mutex> lock(Valid_Objs_db_mutex);	// sql statements must be reset before lock is released
	//lock_guard<mutex> lock(Valid_Objs_db_mutex);				// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnValidObjs::DoValidObjsFinish, this));

	auto bufp = smartobj.BasePtr();
	auto obj = (CCObject*)smartobj.data();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsInsert bufp " << (uintptr_t)bufp << " obj tag " << obj->ObjTag() << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	int64_t seqnum;
	if (obj->ObjTag() == CC_TAG_BLOCK)
	{
		seqnum = g_valid_block_seqnum.fetch_add(1);

		// if the first block is the genesis block, assign it seqnum = 0
		if (seqnum == VALID_BLOCK_SEQNUM_START)
		{
			CCASSERT(sizeof(ccoid_t) == 2 * sizeof(uint64_t));
			auto oidv = (uint64_t*)obj->OidPtr();
			if (!oidv[0] && !oidv[1])
				seqnum = 0;
		}
	}
	else
		seqnum = g_valid_tx_seqnum.fetch_add(1);

	// Seqnum, Time, ObjId, Bufp
	if (dblog(sqlite3_bind_int64(Valid_Objs_insert, 1, seqnum))) return -1;
	if (dblog(sqlite3_bind_int(Valid_Objs_insert, 2, ccticks()))) return -1;
	if (dblog(sqlite3_bind_blob(Valid_Objs_insert, 3, obj->OidPtr(), sizeof(ccoid_t), SQLITE_STATIC))) return -1;
	if (dblog(sqlite3_bind_blob(Valid_Objs_insert, 4, &bufp, sizeof(bufp), SQLITE_STATIC))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsInsert simulating database error pre-insert";

		return -1;
	}

	auto rc = sqlite3_step(Valid_Objs_insert);
	if (rc == SQLITE_CONSTRAINT)
	{
		BOOST_LOG_TRIVIAL(warning) << "DbConnValidObjs::ValidObjsInsert object downloaded more than once; bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		return 1;
	}
	if (dblog(rc, DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Valid_Objs_db);

	if (changes)
	{
		if (TRACE_DBCONN || TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnValidObjs::ValidObjsInsert inserted into Valid_Objs seqnum " << seqnum << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		smartobj.IncRef();
	}

	if (changes != 1)
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsInsert sqlite3_changes " << changes << " after insert into Valid_Objs seqnum " << seqnum << " bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	return 0;
}

// returns 0=found, 1=not found, -1=server error
int DbConnValidObjs::ValidObjsGetObj(const ccoid_t& oid, SmartBuf *retobj)
{
	boost::shared_lock<boost::shared_mutex> lock(Valid_Objs_db_mutex);	// sql statements must be reset before lock is released
	//lock_guard<mutex> lock(Valid_Objs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnValidObjs::DoValidObjsFinish, this));
	retobj->ClearRef();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsGetObj dbconn " << uintptr_t(this) << " oid " << buf2hex(&oid, sizeof(ccoid_t));

	if (dblog(sqlite3_bind_blob(Valid_Objs_select_obj, 1, &oid, sizeof(ccoid_t), SQLITE_STATIC))) return -1;

	int rc;

	if (dblog(rc = sqlite3_step(Valid_Objs_select_obj), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsGetObj simulating database error post-select";

		return -1;
	}

	if (rc == SQLITE_DONE) return 1;

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetObj select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Valid_Objs_select_obj) != 1)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetObj select returned " << sqlite3_data_count(Valid_Objs_select_obj) << " columns";

		return -1;
	}

	// Bufp
	auto bufp_blob = sqlite3_column_blob(Valid_Objs_select_obj, 0);

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsGetObj simulating database error; setting bufp_blob = NULL";

		bufp_blob = NULL;
	}

	if (!bufp_blob)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetObj bufp_blob is null";

		return -1;
	}
	else if (sqlite3_column_bytes(Valid_Objs_select_obj, 0) != sizeof(void*))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetObj Bufp size " << sqlite3_column_bytes(Valid_Objs_select_obj, 0) << " != " << sizeof(void*);

		return -1;
	}

	auto bufp = *(void**)bufp_blob;

	if (!bufp)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetObj bufp is null";

		return -1;
	}

	if (TRACE_DBCONN | TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnValidObjs::ValidObjsGetObj db ObjId " << buf2hex(&oid, sizeof(ccoid_t));

	SmartBuf smartobj(bufp);
	auto obj = (CCObject*)smartobj.data();
	CCASSERT(obj);

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsGetObj obj.oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	if (memcmp(obj->OidPtr(), &oid, sizeof(ccoid_t)))
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetObj ObjId mismatch";

		return -1;
	}

	if (dblog(sqlite3_extended_errcode(Valid_Objs_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsGetObj simulating database error post-select";

		return -1;
	}

	*retobj = smartobj;

	return 0;
}

unsigned DbConnValidObjs::ValidObjsFindNew(int64_t& next_block_seqnum, int64_t& next_tx_seqnum, uint8_t *output, unsigned bufsize)
{
	boost::shared_lock<boost::shared_mutex> lock(Valid_Objs_db_mutex);	// sql statements must be reset before lock is released
	//lock_guard<mutex> lock(Valid_Objs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnValidObjs::DoValidObjsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsFindNew next block seqnum " << next_block_seqnum << " next tx seqnum " << next_block_seqnum;

	if (dblog(sqlite3_bind_int64(Valid_Objs_select_new, 1, next_block_seqnum))) return 0;
	if (dblog(sqlite3_bind_int64(Valid_Objs_select_new, 2, next_tx_seqnum))) return 0;

	bool have_blocks = false;
	uint32_t bufpos = 0;

	while (true)
	{
		int rc;

		if (dblog(rc = sqlite3_step(Valid_Objs_select_new), DB_STMT_SELECT))
			break;

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsFindNew simulating database error post-select";

			break;
		}

		if (rc == SQLITE_DONE)
			break;

		if (rc != SQLITE_ROW)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew select returned " << rc;

			break;
		}

		if (sqlite3_data_count(Valid_Objs_select_new) != 3)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew select returned " << sqlite3_data_count(Valid_Objs_select_new) << " columns";

			break;
		}

		// Seqnum, ObjId, Bufp
		int64_t seqnum = sqlite3_column_int64(Valid_Objs_select_new, 0);
		auto objid_blob = sqlite3_column_blob(Valid_Objs_select_new, 1);
		auto bufp_blob = sqlite3_column_blob(Valid_Objs_select_new, 2);

		// set now in case there's an error
		if (seqnum < 0)
		{
			have_blocks = true;
			next_block_seqnum = seqnum + 1;
		}
		else if (!have_blocks)
		{
			next_tx_seqnum = seqnum + 1;
		}

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsFindNew simulating database error; setting bufp_blob = NULL";

			bufp_blob = NULL;
		}

		if (!objid_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew ObjId is null";

			break;
		}
		else if (sqlite3_column_bytes(Valid_Objs_select_new, 1) != sizeof(ccoid_t))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew ObjId size " << sqlite3_column_bytes(Valid_Objs_select_new, 1) << " != " << sizeof(ccoid_t);

			break;
		}

		if (!bufp_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew bufp_blob is null";

			break;
		}
		else if (sqlite3_column_bytes(Valid_Objs_select_new, 2) != sizeof(void*))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew Bufp size " << sqlite3_column_bytes(Valid_Objs_select_new, 2) << " != " << sizeof(void*);

			break;
		}

		auto bufp = *(void**)bufp_blob;

		if (!bufp)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew bufp is null";

			break;
		}

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsFindNew seqnum " << seqnum << " bufp " << (uintptr_t)bufp << " db ObjId " << buf2hex(objid_blob, sizeof(ccoid_t));

		SmartBuf smartobj(bufp);
		auto obj = (CCObject*)smartobj.data();
		CCASSERT(obj);
		auto size = obj->ObjSize();

		if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsFindNew bufp " << (uintptr_t)bufp << " obj.oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		if (memcmp(obj->OidPtr(), objid_blob, sizeof(ccoid_t)))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew ObjId mismatch";

			break;
		}

		if (dblog(sqlite3_extended_errcode(Valid_Objs_db), DB_STMT_SELECT)) break;	// check if error retrieving results

		if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
		{
			BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsFindNew simulating database error post-error check";

			break;
		}

		if (next_block_seqnum == 0)
		{
			// return array of SmartBuf's

			auto objarray = (SmartBuf*)output;

			objarray[bufpos++] = move(smartobj);

			if (bufpos >= bufsize)
				break;
		}
		else if (seqnum < 0)
		{
			// we have a block

			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsFindNew preparing to send CC_MSG_HAVE_BLOCK oid " << buf2hex(objid_blob, sizeof(ccoid_t));

			if (!bufpos)
			{
				copy_to_buf(&bufpos, sizeof(bufpos), bufpos, output, bufsize);  // save space for size word

				uint32_t tag = CC_MSG_HAVE_BLOCK;
				copy_to_buf(&tag, sizeof(tag), bufpos, output, bufsize);
			}

			if (bufpos + sizeof(relay_request_wire_params_t) > bufsize)
			{
				--next_block_seqnum;	// pick this one up on the next pass

				break;
			}

			auto block = (Block*)obj;
			auto wire = block->WireData();

			// make sure req_params is packed
			CCASSERT(  sizeof(relay_request_wire_params_t)
					== sizeof(ccoid_t)
					 + sizeof(wire->prior_oid)
					 + sizeof(wire->level)
					 + sizeof(relay_request_wire_params_t::size)
					 + sizeof(wire->witness));

			copy_to_buf(objid_blob, sizeof(ccoid_t), bufpos, output, bufsize);
			copy_to_buf(&wire->prior_oid, sizeof(wire->prior_oid), bufpos, output, bufsize);
			copy_to_buf(&wire->level, sizeof(wire->level), bufpos, output, bufsize);
			copy_to_buf(&size, sizeof(relay_request_wire_params_t::size), bufpos, output, bufsize);
			copy_to_buf(&wire->witness, sizeof(wire->witness), bufpos, output, bufsize);
		}
		else
		{
			// we have a tx

			if (have_blocks)
				break;			// can't mix blocks and tx's

			const unsigned param_level_offset = TX_POW_SIZE;
			const unsigned param_level_min_size = param_level_offset + sizeof(uint64_t);

			if (obj->DataSize() < param_level_min_size)
			{
				BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew tx invalid data size " << obj->DataSize();

				break;
			}

			uint64_t param_level = *(uint64_t*)(obj->DataPtr() + param_level_offset);

			if (!bufpos)
			{
				copy_to_buf(&bufpos, sizeof(bufpos), bufpos, output, bufsize);  // save space for size word

				uint32_t tag = CC_MSG_HAVE_TX;
				copy_to_buf(&tag, sizeof(tag), bufpos, output, bufsize);
			}

			if (bufpos + sizeof(ccoid_t) + sizeof(relay_request_wire_params_t::size) + sizeof(param_level) > bufsize)
			{
				--next_tx_seqnum;	// pick this one up on the next pass

				break;
			}

			if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsFindNew preparing to send CC_MSG_HAVE_TX oid " << buf2hex(objid_blob, sizeof(ccoid_t)) << " size " << size << " param_level " << param_level;

			copy_to_buf(objid_blob, sizeof(ccoid_t), bufpos, output, bufsize);
			copy_to_buf(&size, sizeof(relay_request_wire_params_t::size), bufpos, output, bufsize);
			copy_to_buf(&param_level, sizeof(param_level), bufpos, output, bufsize);
		}
	}

	DoValidObjsFinish();

	finally.Clear();

	if (bufpos > bufsize)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsFindNew buffer overflow bufpos " << bufpos << " bufsize " << bufsize;

		return 0;
	}

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsFindNew returning " << bufpos << (next_block_seqnum ? " bytes" : " objects") << "; next block seqnum " << next_block_seqnum << " next tx seqnum " << next_block_seqnum;

	if (next_block_seqnum)
		*(uint32_t*)output = bufpos;		// set size

	return bufpos;
}

int DbConnValidObjs::ValidObjsDeleteObj(SmartBuf smartobj)
{
	lock_guard<boost::shared_mutex> lock(Valid_Objs_db_mutex);	// sql statements must be reset before lock is released
	//lock_guard<mutex> lock(Valid_Objs_db_mutex);				// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnValidObjs::DoValidObjsFinish, this));

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnValidObjs::ValidObjsDeleteObj smartobj " << (uintptr_t)&smartobj;

	auto bufp = smartobj.BasePtr();
	auto obj = (CCObject*)smartobj.data();

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsDeleteObj bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	if (dblog(sqlite3_bind_blob(Valid_Objs_delete_obj, 1, obj->OidPtr(), sizeof(ccoid_t), SQLITE_STATIC))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsDeleteObj simulating database error pre-delete";

		return -1;
	}

	if (dblog(sqlite3_step(Valid_Objs_delete_obj), DB_STMT_STEP)) return -1;

	auto changes = sqlite3_changes(Valid_Objs_db);

	if (changes)
	{
		if (TRACE_DBCONN || TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "DbConnValidObjs::ValidObjsDeleteObj deleted bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		smartobj.DecRef();		// it's now deleted from the db
	}

	if (changes != 1)
	{
		// will happen when witness deletes object that expire thread is waiting on
		if (IsWitness() && changes == 0)
			BOOST_LOG_TRIVIAL(debug) << "DbConnValidObjs::ValidObjsDeleteObj sqlite3_changes " << changes << " after delete obj from Valid_Objs bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));
		else
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsDeleteObj sqlite3_changes " << changes << " after delete obj from Valid_Objs bufp " << (uintptr_t)bufp << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));
	}

	return 0;
}

int DbConnValidObjs::ValidObjsDeleteSeqnum(int64_t seqnum)
{
	// Note: this function does not DefRef the smartobj in the Valid_Objs_db, leading to a memory leak.
	// It is used only when ValidObjsDeleteObj fails

	lock_guard<boost::shared_mutex> lock(Valid_Objs_db_mutex);	// sql statements must be reset before lock is released
	//lock_guard<mutex> lock(Valid_Objs_db_mutex);				// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnValidObjs::DoValidObjsFinish, this));

	BOOST_LOG_TRIVIAL(warning) << "DbConnValidObjs::ValidObjsDeleteSeqnum seqnum " << seqnum << " -- will lead to memory leak";

	if (dblog(sqlite3_bind_int64(Valid_Objs_delete_seqnum, 1, seqnum))) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsDeleteSeqnum simulating database error pre-delete";

		return -1;
	}

	if (dblog(sqlite3_step(Valid_Objs_delete_seqnum), DB_STMT_STEP)) return -1;

	return 0;
}

int DbConnValidObjs::ValidObjsGetExpires(int64_t min_seqnum, int64_t max_seqnum, int64_t& next_expires_seqnum, SmartBuf *retobj, uint32_t& next_expires_t0)
{
	boost::shared_lock<boost::shared_mutex> lock(Valid_Objs_db_mutex);	// sql statements must be reset before lock is released
	//lock_guard<mutex> lock(Valid_Objs_db_mutex);						// sql statements must be reset before lock is released
	Finally finally(boost::bind(&DbConnValidObjs::DoValidObjsFinish, this));

	if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsGetExpires min_seqnum " << min_seqnum << " max_seqnum " << max_seqnum;

	// preset these in case of error
	auto last_expires_seqnum = next_expires_seqnum;
	next_expires_seqnum = -1;
	retobj->ClearRef();

	int rc;

	if (dblog(sqlite3_bind_int64(Valid_Objs_select_oldest, 1, min_seqnum))) return -1;
	if (dblog(sqlite3_bind_int64(Valid_Objs_select_oldest, 2, max_seqnum))) return -1;

	if (dblog(rc = sqlite3_step(Valid_Objs_select_oldest), DB_STMT_SELECT)) return -1;

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsGetExpires simulating database error post-select";

		return -1;
	}

	if (rc == SQLITE_DONE)
		return 1;

	if (rc != SQLITE_ROW)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetExpires select returned " << rc;

		return -1;
	}

	if (sqlite3_data_count(Valid_Objs_select_oldest) != 3)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetExpires select returned " << sqlite3_data_count(Valid_Objs_select_oldest) << " columns";

		return -1;
	}

	// Seqnum, Timediff, Bufp
	auto seqnum = sqlite3_column_int64(Valid_Objs_select_oldest, 0);
	auto t0 = sqlite3_column_int(Valid_Objs_select_oldest, 1);
	auto bufp_blob = sqlite3_column_blob(Valid_Objs_select_oldest, 2);

	if (seqnum == last_expires_seqnum)
	{
		BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetExpires select returned seqnum " << seqnum << " which should have already been deleted";

		return -1;
	}

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsGetExpires simulating database error; setting bufp_blob = NULL";

		bufp_blob = NULL;
	}

	SmartBuf smartobj;
	CCObject *obj = NULL;

	while (true)	// to use "break" on error
	{
		if (!bufp_blob)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetExpires bufp_blob is null";

			break;
		}
		else if (sqlite3_column_bytes(Valid_Objs_select_oldest, 2) != sizeof(void*))
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetExpires Bufp size " << sqlite3_column_bytes(Valid_Objs_select_oldest, 2) << " != " << sizeof(void*);

			break;
		}

		auto bufp = *(void**)bufp_blob;

		if (!bufp)
		{
			BOOST_LOG_TRIVIAL(error) << "DbConnValidObjs::ValidObjsGetExpires bufp is null";

			break;
		}

		smartobj.SetBasePtr(bufp);
		obj = (CCObject*)smartobj.data();
		CCASSERT(obj);

		break;
	}

	if (dblog(sqlite3_extended_errcode(Valid_Objs_db), DB_STMT_SELECT)) return -1;	// check if error retrieving results

	if ((TEST_RANDOM_DB_ERRORS & rand()) == 1) // for testing
	{
		BOOST_LOG_TRIVIAL(info) << "DbConnValidObjs::ValidObjsGetExpires simulating database error post-select";

		return -1;
	}

	if (TRACE_SMARTBUF && obj) BOOST_LOG_TRIVIAL(debug) << "DbConnValidObjs::ValidObjsGetExpires seqnum " << seqnum << " t0 " << t0 << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));
	else if (TRACE_DBCONN && obj) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsGetExpires seqnum " << seqnum << " t0 " << t0 << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));
	else if (TRACE_DBCONN) BOOST_LOG_TRIVIAL(trace) << "DbConnValidObjs::ValidObjsGetExpires seqnum " << seqnum << " t0 " << t0 << ", obj = " << (uintptr_t)obj;

	next_expires_seqnum = seqnum;
	*retobj = smartobj;
	next_expires_t0 = t0;

	return 0;
}
