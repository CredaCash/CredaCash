/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * relay.cpp
*/

#include "CCdef.h"
#include "relay.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "processblock.hpp"
#include "transact.hpp"
#include "hostdir.hpp"
#include "dbconn.hpp"
#include "util.h"

#include <CCobjects.hpp>

#include <transaction.h>
#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#include <boost/filesystem/fstream.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>

#include <utility>

#define TRACE_RELAY		(g_params.trace_relay)

#define RELAY_HEARTBEAT				100

#define RELAY_DOWNLOAD_LOW_WATER	12	//((CC_TX_SEND_MAX)/2)
#define RELAY_DOWNLOAD_HIGH_WATER	5

#define RELAY_DIR_REFRESH			(20*60)

//#define TEST_DELAY_BLOCKS				1	// for testing
//#define TEST_RANDOM_NO_SEND			7	// for testing
//#define TEST_DOUBLECHECK_BLOCK_OIDS	1	// for testing

#ifndef TEST_DELAY_BLOCKS
#define TEST_DELAY_BLOCKS				0	// don't test
#endif

#ifndef TEST_RANDOM_NO_SEND
#define TEST_RANDOM_NO_SEND				0	// don't test
#endif

#ifndef TEST_DOUBLECHECK_BLOCK_OIDS
#define TEST_DOUBLECHECK_BLOCK_OIDS		0	// don't test
#endif

#pragma pack(push, 1)

//static uint32_t Success_Reply[2] =			{CC_MSG_HEADER_SIZE, CC_SUCCESS};
//static uint32_t Success_Reply_Queue_Len[3] =	{CC_MSG_HEADER_SIZE + sizeof(uint32_t), CC_SUCCESS_QUEUE_LEN, 0};
static uint32_t Buffer_Full_Reply[2] =			{CC_MSG_HEADER_SIZE, CC_RESULT_BUFFER_FULL};
//static uint32_t Buffer_Busy_Reply[2] =		{CC_MSG_HEADER_SIZE, CC_RESULT_BUFFER_BUSY};
//static uint32_t Server_Busy_Reply[2] =		{CC_MSG_HEADER_SIZE, CC_RESULT_SERVER_BUSY};
//static uint32_t Server_Err_Reply[2] =			{CC_MSG_HEADER_SIZE, CC_RESULT_SERVER_ERR};
static uint32_t Bad_Cmd_Reply[2] =				{CC_MSG_HEADER_SIZE, CC_ERROR_BAD_CMD};
//static uint32_t Bad_Param_Reply[2] =			{CC_MSG_HEADER_SIZE, CC_ERROR_BAD_PARAM};
static uint32_t No_Obj_Reply[2] =				{CC_MSG_HEADER_SIZE, CC_ERROR_NO_OBJ};
//static uint32_t Send_Q_Reset_Reply[2] =		{CC_MSG_HEADER_SIZE, CC_ERROR_SEND_Q_RESET};

#pragma pack(pop)

thread_local DbConn *relay_dbconn;

void RelayConnection::StartConnection()
{
	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::StartConnection";

	if (private_peer_index >= 0)
		g_privrelay_service.PrivateConnected(private_peer_index);

	peer_error_count = 0;

	db_next_new_block_seqnum = VALID_BLOCK_SEQNUM_START;			// announce existing blocks

	if (g_transact_service.enabled)
		db_next_new_tx_seqnum = 1;									// announce existing tx's (but don't announce genesis block with seqnum = 0)
	else
		db_next_new_tx_seqnum = DbConnValidObjs::GetNextTxSeqnum();	// don't announce existing tx's

	request_msg_buf_in_use.clear();
	request_objs_pending.store(0);
	request_bytes_pending.store(0);
	request_param_queue.clear();

	send_queue.clear();
	send_one.clear();

	if (SetTimer())
		return;

	Connection::StartConnection();
}

void RelayConnection::HandleReadComplete()
{
	if (m_nred < CC_MSG_HEADER_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleReadComplete error short read " << m_nred;

		return Stop();
	}

	unsigned size = *(uint32_t*)m_pread;
	unsigned tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleReadComplete read " << m_nred << " bytes msg size " << size << " tag " << tag;

	if (size < CC_MSG_HEADER_SIZE || size > CC_BLOCK_MAX_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleReadComplete error invalid msg size " << size;

		return Stop();
	}

	SmartBuf smartobj;

	if (tag == CC_TAG_BLOCK || tag == CC_TAG_TX_WIRE)
	{
		CCASSERT(CC_MSG_HEADER_SIZE == sizeof(CCObject::Header));

		smartobj = SmartBuf(size + sizeof(CCObject::Preamble));
		if (!smartobj)
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleReadComplete error smartobj failed";

			return;
		}

		auto obj = (CCObject*)smartobj.data();

		memcpy(obj->ObjPtr(), m_pread, m_nred);

		m_pread = obj->ObjPtr();
	}
	else if (size > CC_MAX_MSG_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleReadComplete error msg tag " << tag << " size " << size << " exceeds " << CC_MAX_MSG_SIZE;

		return Stop();
	}

	m_maxread = size;

	if (m_maxread > m_nred)
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleReadComplete queueing read size " << m_maxread - m_nred;

		ReadAsync("RelayConnection::HandleReadComplete", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(m_maxread - m_nred),
				boost::bind(&RelayConnection::HandleMsgReadComplete, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, smartobj, AutoCount(this)));
	}
	else
	{
		HandleMsgReadComplete(boost::system::error_code(), 0, smartobj, AutoCount());	// don't need to increment op count
	}
}

void RelayConnection::HandleMsgReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter)
{
	m_nred += bytes_transferred;

	bool sim_err = ((TEST_RANDOM_READ_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete simulating read error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

		return Stop();
	}

	if (m_nred != m_maxread)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error short read " << m_nred;

		return Stop();
	}

	auto msgsize = *(uint32_t*)m_pread;
	auto tag = *(uint32_t*)(m_pread + 4);

	if (msgsize != m_nred)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error size mismatch msgsize " << msgsize << " != m_nred " << m_nred;

		return Stop();
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete read " << m_nred << " bytes msg size " << msgsize << " tag " << tag;

	CCASSERT(msgsize >= CC_MSG_HEADER_SIZE);

	// !!! need some protection from peer overloading us with data

	int objs_pending = -1;

	switch (tag)
	{

	case CC_MSG_HAVE_BLOCK:

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_MSG_HAVE_BLOCK";

		goto handle_have;

	case CC_MSG_HAVE_TX:

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_MSG_HAVE_TX";
	{

	handle_have:

		uint32_t bufpos = CC_MSG_HEADER_SIZE;

		uint64_t prune_level = 0;

		if (tag == CC_MSG_HAVE_BLOCK)
			prune_level = g_blockchain.ComputePruneLevel(0, BLOCK_PRUNE_ROUNDS) + 1;

		while (bufpos < msgsize)
		{
			relay_request_wire_params_t req_params;
			memset(&req_params, 0, sizeof(req_params));

			if (tag == CC_MSG_HAVE_BLOCK)
			{
				copy_from_buf(&req_params, sizeof(req_params), bufpos, m_pread, msgsize);

				//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_BLOCK at level " << req_params.level << " prune_level " << prune_level;

				if (req_params.level < prune_level)
				{
					BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_BLOCK skipping download of block at level " << req_params.level << " prune_level " << prune_level;

					continue;
				}
			}
			else
			{
				copy_from_buf(&req_params.oid, sizeof(ccoid_t), bufpos, m_pread, msgsize);
				copy_from_buf(&req_params.size, sizeof(req_params.size), bufpos, m_pread, msgsize);
				copy_from_buf(&req_params.level, sizeof(req_params.level), bufpos, m_pread, msgsize);
			}

			if (bufpos > msgsize)
			{
				BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_BLOCK/CC_MSG_HAVE_TX tag " << tag << " error msg overrun msgsize " << msgsize << " bufpos " << bufpos;

				break;
			}

			if (!TEST_DELAY_BLOCKS || tag != CC_MSG_HAVE_BLOCK)
				relay_dbconn->RelayObjsInsert(m_conn_index, (tag == CC_MSG_HAVE_BLOCK) ? CC_TAG_BLOCK : CC_TAG_TX_WIRE, req_params, RELAY_STATUS_ANNOUNCED, RELAY_PEER_STATUS_READY);
			else if (private_peer_index >= 0)
			{
				// for testing, blocks are requested on the private relay only, after a delay
				// as a result, undelayed tx's on the public relay should end up being requested first
				ccsleep(2);
				relay_dbconn->RelayObjsInsert(m_conn_index, (tag == CC_MSG_HAVE_BLOCK) ? CC_TAG_BLOCK : CC_TAG_TX_WIRE, req_params, RELAY_STATUS_ANNOUNCED, RELAY_PEER_STATUS_READY);
			}
		}

		objs_pending = 0;	// so we'll CheckToDownload

		break;
	}

	case CC_CMD_SEND_BLOCK:
	case CC_CMD_SEND_TX:
	{
		unsigned nobjs = (msgsize - CC_MSG_HEADER_SIZE) / sizeof(ccoid_t);

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << tag << " received CC_CMD_SEND_BLOCK/CC_CMD_SEND_TX nobjs " << nobjs;

		if (msgsize != CC_MSG_HEADER_SIZE + nobjs * sizeof(ccoid_t))
		{
			BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_CMD_SEND_BLOCK/CC_CMD_SEND_TX tag " << tag << " error invalid msgsize " << msgsize << " (nobjs " << nobjs << " -> msgsize " << CC_MSG_HEADER_SIZE + nobjs * sizeof(ccoid_t) << ") sending CC_ERROR_BAD_CMD";

			WriteAsync("RelayConnection::HandleMsgReadComplete", boost::asio::buffer(Bad_Cmd_Reply, sizeof(Bad_Cmd_Reply)),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			break;

			CCASSERT(0);
		}

		unique_lock<FastSpinLock> lock(send_queue_lock);

		if (send_queue.space() < nobjs)
		{
			BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_CMD_SEND_BLOCK/CC_CMD_SEND_TX tag " << tag << " error insufficient space in send queue; nobjs " << nobjs << " space " << send_queue.space() << " sending CC_RESULT_BUFFER_FULL";

			lock_guard<mutex> write_pending_lock(next_writer_mutex);

			lock.unlock();	// unlock this after taking next_writer_mutex, so replies don't get out of order

			WriteAsync("RelayConnection::HandleMsgReadComplete", boost::asio::buffer(Buffer_Full_Reply, sizeof(Buffer_Full_Reply)),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)), true);

			break;

			CCASSERT(0);
		}

		for (unsigned i = 0; i < nobjs; ++i)
		{
			auto rc = send_queue.push(m_pread + CC_MSG_HEADER_SIZE + i * sizeof(ccoid_t));

			CCASSERTZ(rc);
		}

#if 0 // CC_SUCCESS_QUEUE_LEN not implemented
		{
			auto qlen = send_queue.size();

			lock_guard<mutex> write_pending_lock(next_writer_mutex);

			lock.unlock();	// unlock this after taking next_writer_mutex, so replies don't get out of order

			auto msgbuf = SmartBuf(sizeof(Success_Reply_Queue_Len));
			if (!msgbuf)
			{
				BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error msgbuf failed";

				Stop();
			}

			memcpy(msgbuf.data(), Success_Reply_Queue_Len, sizeof(Success_Reply_Queue_Len));

			*(uint32_t*)(msgbuf.data() + 8) = qlen;

			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete sending CC_SUCCESS_QUEUE_LEN " << qlen;

			WriteAsync("RelayConnection::HandleMsgReadComplete", boost::asio::buffer(msgbuf.data(), sizeof(Success_Reply_Queue_Len)),
					boost::bind(&Connection::HandleWriteSmartBuf, this, boost::asio::placeholders::error, msgbuf, AutoCount(this)), true);
		}
#endif

		lock.unlock();

		CheckToSend();

		break;
	}

#if 0 // CC_SUCCESS_QUEUE_LEN not implemented
	case CC_SUCCESS_QUEUE_LEN:
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_SUCCESS_QUEUE_LEN";

		if (msgsize != sizeof(Success_Reply_Queue_Len))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error CC_SUCCESS_QUEUE_LEN size mismatch msgsize " << msgsize << " != " << sizeof(Success_Reply_Queue_Len);

			Stop();
		}

		auto nobjs = *(uint32_t*)(m_pread + 8);

		// would need to sanity check this nobjs

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_SUCCESS_QUEUE_LEN " << nobjs;

		request_objs_pending.store(nobjs);

		break;
	}
#endif

	case CC_ERROR_NO_OBJ:
	{
		int64_t bytes_pending;

		{
			lock_guard<FastSpinLock> lock(request_queue_lock);

			auto params = (relay_request_params_extended_t*)request_param_queue.pop();
			if (!params)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error received CC_ERROR_NO_OBJ but no object was expected";

				return Stop();
			}

			objs_pending = request_objs_pending.fetch_sub(1) - 1;
			bytes_pending = request_bytes_pending.fetch_sub(params->size) - params->size;

			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_ERROR_NO_OBJ; still pending " << objs_pending << " objects in " << bytes_pending << " bytes";

			CCASSERT(objs_pending >= 0);
			CCASSERT(bytes_pending >= 0);
		}

		break;
	}

	case CC_TAG_BLOCK:

		//if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_TAG_BLOCK";

		goto enqueue_obj;

	case CC_TAG_TX_WIRE:

		//if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_TAG_TX_WIRE";
	{

	enqueue_obj:

		relay_request_params_extended_t req_params;
		int64_t priority = 0;
		ccoid_t *prior_oid = NULL;
		int64_t level = 0;

		{
			lock_guard<FastSpinLock> lock(request_queue_lock);

			auto params = (relay_request_params_extended_t*)request_param_queue.pop();
			if (!params)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error received CC_TAG_BLOCK/CC_TAG_TX_WIRE tag " << tag << " but no object was expected";

				return Stop();
			}

			memcpy(&req_params, params, sizeof(req_params));
		}

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << tag << (tag == CC_TAG_BLOCK ? " CC_TAG_BLOCK" : " CC_TAG_TX_WIRE") << " request params oid " << buf2hex(&req_params.oid, sizeof(ccoid_t)) << " size " << req_params.size << " level " << req_params.level << " witness " << (unsigned)req_params.witness;

		if (req_params.size != msgsize)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK/CC_TAG_TX_WIRE tag " << tag << " error expected object size " << req_params.size << "; received size " << msgsize;

			return Stop();
		}

		auto objs_pending = request_objs_pending.fetch_sub(1) - 1;
		auto bytes_pending = request_bytes_pending.fetch_sub(msgsize) - msgsize;

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete still pending " << objs_pending << " objects in " << bytes_pending << " bytes";

		CCASSERT(objs_pending >= 0);
		CCASSERT(bytes_pending >= 0);

		auto obj = (CCObject*)smartobj.data();

		CCASSERT(m_pread == obj->ObjPtr());

		if (!obj->IsValid())
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK/CC_TAG_TX_WIRE tag " << tag << " error object IsValid false";

			return Stop();
		}

		if (tag == CC_TAG_TX_WIRE)
		{
			obj->SetObjId();

			while (memcmp(&req_params.oid, obj->OidPtr(), sizeof(ccoid_t)))
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_TX_WIRE error next request tx oid " << buf2hex(&req_params.oid, sizeof(ccoid_t)) << "; received oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

				lock_guard<FastSpinLock> lock(request_queue_lock);

				auto params = (relay_request_params_extended_t*)request_param_queue.pop();
				if (!params)
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_TX_WIRE error object not requested";

					return Stop();
				}

				memcpy(&req_params, params, sizeof(req_params));
			}

			// note: we could/should check that the tx param_level matches the requested level, but we don't, so that will instead get caught if the param_level is too high and the tx fails validation

			if (tx_set_work((char*)(obj->ObjPtr()), obj->OidPtr(), 0, TX_POW_NPROOFS, 1, g_params.tx_work_difficulty))
			{
				// note: Proof of Work might fail because the peer tampered with the nonces. To prevent this from be used as a Denial of Service attack,
				// we have not yet set the object status to RELAY_STATUS_DOWNLOADED, so it can be downloaded again from a different peer that thinks the Tx is valid

				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error proof-of-work failed";

				return Stop();
			}

			static atomic<int64_t> lowpriority(int64_t(1) << 60);
			priority = lowpriority.fetch_add(1, memory_order_acq_rel);
		}
		else
		{
			auto block = (Block*)obj;
			auto wire = block->WireData();

			if (block->BodySize() < sizeof(BlockWireHeader))
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error object too small size " << block->BodySize();

				return Stop();
			}

			while (memcmp(&req_params.prior_oid, &wire->prior_oid, sizeof(ccoid_t)))
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error next request block prior oid " << buf2hex(&req_params.prior_oid, sizeof(ccoid_t)) << "; received block prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

				lock_guard<FastSpinLock> lock(request_queue_lock);

				auto params = (relay_request_params_extended_t*)request_param_queue.pop();
				if (!params)
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error object not requested";

					return Stop();
				}

				memcpy(&req_params, params, sizeof(req_params));
			}

			if (req_params.level != wire->level)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error requested block level " << req_params.level << "; received block level " << wire->level;

				return Stop();
			}

			if (req_params.witness != wire->witness)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error requested block witness " << (unsigned)req_params.witness << "; received block witness " << (unsigned)wire->witness;

				return Stop();
			}

			block_hash_t block_hash;
			block->CalcHash(block_hash);

			ccoid_t oid;
			block->CalcOid(block_hash, oid);

			if (TEST_SEQ_BLOCK_OID)
				memcpy(&oid, &req_params.oid, sizeof(ccoid_t));

			if (memcmp(&req_params.oid, &oid, sizeof(ccoid_t)))
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error requested block size " << block->ObjSize() << " oid " << buf2hex(&req_params.oid, sizeof(ccoid_t)) << " does not match computed oid " << buf2hex(&oid, sizeof(ccoid_t));
				//BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete block dump " << buf2hex(block, block->ObjSize());

				//raise(SIGTERM);	// for debugging

				return Stop();
			}

			auto auxp = block->SetupAuxBuf(smartobj);
			if (!auxp)
			{
				BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error SetupAuxBuf failed";

				return Stop();
			}

			auxp->SetHash(block_hash);
			auxp->SetOid(oid);

			auxp->announce_time = req_params.announce_time;

			static atomic<int64_t> hipriority(VALID_BLOCK_SEQNUM_START);
			priority = hipriority.fetch_add(1, memory_order_acq_rel);

			prior_oid = &wire->prior_oid;
			level = wire->level;
		}

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK/CC_TAG_TX_WIRE received obj bufp " << (uintptr_t)smartobj.BasePtr() << " tag " << obj->ObjTag() << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

		relay_dbconn->RelayObjsSetStatus(*obj->OidPtr(), RELAY_STATUS_DOWNLOADED, 0);

		relay_dbconn->ProcessQEnqueueValidate((tag == CC_TAG_BLOCK ? PROCESS_Q_TYPE_BLOCK : PROCESS_Q_TYPE_TX), smartobj, prior_oid, level, PROCESS_Q_STATUS_PENDING, priority, m_conn_index, m_use_count.load());

		break;
	}

	default:
		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleMsgReadComplete error unrecognized message tag " << tag;
		break;
	}

	StartRead();

	if (objs_pending >= 0 && objs_pending < RELAY_DOWNLOAD_LOW_WATER)
		CheckToDownload();
}

void RelayConnection::CheckToSend()
{
	if (send_one.test_and_set())		// ensure only one object is "in-flight" at a time
		return;

	while (true)
	{
		ccoid_t oid;

		{
			lock_guard<FastSpinLock> lock2(send_queue_lock);

			auto oidp = send_queue.pop();
			if (!oidp)
			{
				if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToSend nothing in queue";

				send_one.clear();

				return;
			}

			memcpy(&oid, oidp, sizeof(ccoid_t));
		}

		if ((TEST_RANDOM_NO_SEND & rand()) == 1)	// for testing
		{
			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToSend test skipping send of object oid " << buf2hex(&oid, sizeof(ccoid_t));

			continue;
		}

		SmartBuf smartobj;

		auto rc = relay_dbconn->ValidObjsGetObj(oid, &smartobj);
		if (rc)
		{
			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToSend unable to retrieve object oid " << buf2hex(&oid, sizeof(ccoid_t));

			WriteAsync("RelayConnection::HandleMsgReadComplete", boost::asio::buffer(No_Obj_Reply, sizeof(No_Obj_Reply)),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			continue;		// try the next object in the queue
		}

		auto obj = (CCObject*)smartobj.data();
		CCASSERT(obj);

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToSend buf " << (uintptr_t)smartobj.BasePtr() << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t)) << " size " << obj->ObjSize() << " tag " << obj->ObjTag();

		auto size = obj->ObjSize();

		if (size < CC_MSG_HEADER_SIZE || size > CC_BLOCK_MAX_SIZE)
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToSend error object invalid size " << size;

			continue;		// try the next object in the queue
		}

		switch (obj->ObjTag())
		{
		case CC_TAG_BLOCK:
			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToSend sending CC_TAG_BLOCK size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t)); // << " block dump " << buf2hex(obj, obj->ObjSize());

			if (TEST_DOUBLECHECK_BLOCK_OIDS) ((Block*)obj)->SetOrVerifyOid(false);

			break;
		case CC_TAG_TX_WIRE:
			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToSend sending CC_TAG_TX_WIRE size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));
			break;
		default:
			BOOST_LOG_TRIVIAL(warning) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToSend unknown object tag " << obj->ObjTag() << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));
			break;
		}

		if (!WriteAsync("RelayConnection::CheckToSend", boost::asio::buffer(obj->ObjPtr(), size),
				boost::bind(&RelayConnection::HandleObjWrite, this, boost::asio::placeholders::error, smartobj, AutoCount(this))))
		{
			send_one.clear();
		}

		return;
	}
}

void RelayConnection::HandleObjWrite(const boost::system::error_code& e, SmartBuf smartobj, AutoCount pending_op_counter)
{
	m_write_in_progress.clear();

	send_one.clear();

	smartobj.ClearRef();	// we're done with this, so might as well free it now

	bool sim_err = ((TEST_RANDOM_WRITE_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleObjWrite simulating write error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleObjWrite after error " << e << " " << e.message();

		return Stop();
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleObjWrite ok";

	CheckToSend();
}

void RelayConnection::CheckToDownload()
{
	if (request_msg_buf_in_use.test_and_set())
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToDownload buffer in use";

		return;
	}

	auto objs_pending = request_objs_pending.load();

	if (objs_pending > RELAY_DOWNLOAD_LOW_WATER)
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToDownload still have " << objs_pending << " objects pending";

		request_msg_buf_in_use.clear();

		return;
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToDownload checking for downloads...";

	unsigned nobjs = 0;
	unsigned nbytes = 0;

	//cerr << "CC_TX_SEND_MAX " << CC_TX_SEND_MAX << " RELAY_DOWNLOAD_HIGH_WATER " << RELAY_DOWNLOAD_HIGH_WATER << " objs_pending " << objs_pending << " max req " << CC_TX_SEND_MAX - RELAY_DOWNLOAD_HIGH_WATER - objs_pending << endl;

	relay_dbconn->RelayObjsFindDownloads(m_conn_index, g_blockchain.GetLastIndelibleLevel(), &request_msg_buf[0], sizeof(request_msg_buf), req_param_buf, CC_TX_SEND_MAX - RELAY_DOWNLOAD_HIGH_WATER - objs_pending, request_bytes_pending.load(), nobjs, nbytes);

	if (nobjs)
	{
		lock_guard<FastSpinLock> lock(request_queue_lock);

		int64_t reqsize = 0;

		for (unsigned i = 0; i < nobjs; ++i)
		{
			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToDownload queuing request params oid " << buf2hex(&req_param_buf[i].oid, sizeof(ccoid_t)) << " size " << req_param_buf[i].size << " level " << req_param_buf[i].level << " witness " << (unsigned)req_param_buf[i].witness;

			request_param_queue.push(&req_param_buf[i]);
			reqsize += req_param_buf[i].size;
		}

		CCASSERTZ(request_param_queue.full());

		auto objs_pending = request_objs_pending.fetch_add(nobjs) + nobjs;
		auto bytes_pending = request_bytes_pending.fetch_add(reqsize) + reqsize;

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToDownload found " << nobjs << " objects in " << reqsize << " bytes; total now pending " << objs_pending << " objects in " << bytes_pending;
	}

	if (TRACE_RELAY && nbytes) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToDownload RelayObjsFindDownloads returned " << nobjs << " objects in " << nbytes << " bytes sending CC_CMD_SEND_BLOCK/CC_CMD_SEND_TX";
	else if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::CheckToDownload RelayObjsFindDownloads returned " << nobjs << " objects in " << nbytes;

	if (!nbytes)
	{
		request_msg_buf_in_use.clear();

		return;
	}

	if (WriteAsync("RelayConnection::CheckToDownload", boost::asio::buffer(&request_msg_buf, nbytes),
			boost::bind(&RelayConnection::HandleSendMsgWrite, this, boost::asio::placeholders::error, AutoCount(this))))
	{
		request_msg_buf_in_use.clear();
	}
}

void RelayConnection::HandleSendMsgWrite(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	m_write_in_progress.clear();

	request_msg_buf_in_use.clear();

	bool sim_err = ((TEST_RANDOM_WRITE_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleSendMsgWrite simulating write error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleSendMsgWrite after error " << e << " " << e.message();

		return Stop();
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleSendMsgWrite ok";

	//CheckToDownload(); // commented out cause we don't need to check again here
}

bool RelayConnection::SetTimer()
{
	//if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::SetTimer";

	uint32_t ms = RELAY_HEARTBEAT;
	if (g_params.trace_level >= 6 && g_params.trace_relay && ms > CCTICKS_PER_SEC)
		ms = CCTICKS_PER_SEC;		// slow down so it doesn't overload the log output

	auto op_counter = AutoCount();
	return AsyncTimerWait("RelayConnection::SetTimer", ms, boost::bind(&RelayConnection::HandleHeartbeat, this, boost::asio::placeholders::error, op_counter), op_counter);
}

void RelayConnection::HandleHeartbeat(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (e == boost::asio::error::operation_aborted)
		return;

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleHeartbeat " << uintptr_t(this) << " e = " << e << " " << e.message();

	if (g_shutdown)
		return;

	CheckToDownload();	// we do this on the same timer just to make it easier

	unsigned nbytes;

	nbytes = relay_dbconn->ValidObjsFindNew(db_next_new_block_seqnum, db_next_new_tx_seqnum, &announce_msg_buf[0], sizeof(announce_msg_buf));

	if (nbytes)
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleHeartbeat sending CC_MSG_HAVE_BLOCK/CC_MSG_HAVE_TX";

		WriteAsync("RelayConnection::HandleHeartbeat", boost::asio::buffer(&announce_msg_buf, nbytes),
				boost::bind(&RelayConnection::HandleAnnounceMsgWrite, this, boost::asio::placeholders::error, AutoCount(this)));
	}
	else
		SetTimer();
}

void RelayConnection::HandleAnnounceMsgWrite(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	m_write_in_progress.clear();

	bool sim_err = ((TEST_RANDOM_WRITE_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleAnnounceMsgWrite simulating write error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleAnnounceMsgWrite after error " << e << " " << e.message();

		return Stop();
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::HandleAnnounceMsgWrite ok";

	// announce_msg_buf is no longer in use, so we can now restart the timer to look for more objects to announce

	SetTimer();
}

void RelayConnection::FinishConnection()
{
	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " RelayConnection::FinishConnection";

	relay_dbconn->RelayObjsDeletePeer(m_conn_index);

	if (private_peer_index >= 0)
		g_privrelay_service.PrivateDisconnected(private_peer_index);
}

void RelayService::Start()
{
	if (!enabled)
		return;

	g_hostdir.Init();

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService port " << port;

	// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
	CCServer::ConnectionFactoryInstantiation<RelayConnection> connfac(CC_MAX_MSG_SIZE + 2, 0, -1, -1, CC_MSG_HEADER_SIZE, 1, 1);
	CCThreadFactoryInstantiation<RelayThread> threadfac;

	unsigned maxconns = (unsigned)(max_inconns + max_outconns);
	unsigned nthreads = maxconns * threads_per_conn;

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	m_service.Start(boost::asio::ip::tcp::endpoint(address, port),
			nthreads, maxconns, max_inconns, 0, connfac, threadfac);

	if (!m_service.GetNServers())
		return;

	if (!m_bprivate)
	{
		BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService creating thread for ConnMonitorProc this = " << this;
		thread worker(&RelayService::ConnMonitorProc, this);
		m_conn_monitor_thread.swap(worker);
	}
	else if (max_outconns > 0)
	{
		BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService creating thread for PrivateConnMonitorProc this = " << this;
		thread worker(&RelayService::PrivateConnMonitorProc, this);
		m_conn_monitor_thread.swap(worker);
	}
}

void RelayService::ConnMonitorProc()
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::ConnMonitorProc(" << this << ") started";

	ccsleep(6);

	unsigned si = 0;

	auto last_dir_refresh_time = ccticks() - RELAY_DIR_REFRESH;

	while (!g_shutdown)
	{
		unsigned outcount = m_service.GetServer(si).GetConnectionManager().GetOutgoingConnectionCount();

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::ConnMonitorProc currently " << outcount << " outgoing connections";

		if ((int)outcount < max_outconns)
		{
			ConnectOutgoing();
			last_dir_refresh_time = ccticks();
		}
		else if (ccticks_elapsed(last_dir_refresh_time, ccticks()) > RELAY_DIR_REFRESH * CCTICKS_PER_SEC)
		{
			g_hostdir.GetHostName((HostDir::HostType)(-1));	// let dirserver know we're here
			last_dir_refresh_time = ccticks();
		}

		ccsleep(12);
	}

	BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::ConnMonitorProc(" << this << ") ended";
}

void RelayService::ConnectOutgoing()
{
	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(info) << Name() << " RelayService::ConnectOutgoing calling GetHostName()";

	auto peer = g_hostdir.GetHostName(HostDir::Relay);

	if (!peer.size())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " RelayService::ConnectOutgoing no relay peers found";

		return;
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(info) << Name() << " RelayService::ConnectOutgoing connecting to " << peer;

	m_service.GetServer(0).ConnectThruTor(peer, g_params.torproxy_port);
}

void RelayService::PrivateConfigPreset()
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::PrivateConfigPreset";

	if (enabled && priv_hosts_file.length())
	{
		LoadPrivateHosts();

		max_outconns = m_nprivhosts;
	}

	if (max_inconns < 0)
		max_inconns = max_outconns + 1 + 2;	// 1 extra for possible round down when dividing by 2, plus 2 extra to ensure there's a free connection when a host attempts to reconnect
}

int RelayService::LoadPrivateHosts()
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::LoadPrivateHosts file \"" << priv_hosts_file << "\"";

	CCASSERT(priv_hosts_file.length());

	boost::filesystem::ifstream fs;
	fs.open(priv_hosts_file, fstream::in);
	if(!fs.is_open())
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " RelayService::LoadPrivateHosts error opening private relay hosts file \"" << priv_hosts_file << "\"";

		return -1;
	}

	vector<string> hosts;

	while (true)
	{
		string line;

		fs >> line;

		if (fs.fail() && !fs.eof())
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " RelayService::LoadPrivateHosts error reading private relay hosts file \"" << priv_hosts_file << "\"";

			return -1;
		}

		boost::trim(line);

		if (line.length() > 0)
		{
			BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::LoadPrivateHosts read hostname \"" << line << "\"";

			hosts.push_back(line);
		}

		if (fs.eof())
			break;
	}

	BOOST_LOG_TRIVIAL(debug) << Name() << " RelayService::LoadPrivateHosts loaded " << hosts.size() << " private relay hostnames";

	if (priv_host_index < 0)
	{
		// use the entire hosts file

		m_nprivhosts = hosts.size();
		m_privhosts.reserve(m_nprivhosts);
		m_privhosts = hosts;
	}
	else
	{
		// use only half of hosts file entries, starting with entry priv_host_index + 1

		m_nprivhosts = hosts.size()/2;
		if ((hosts.size() & 1) == 0 && (unsigned)priv_host_index >= hosts.size()/2)
			--m_nprivhosts;

		BOOST_LOG_TRIVIAL(debug) << Name() << " RelayService::LoadPrivateHosts set m_nprivhosts = " << m_nprivhosts << " for priv_host_index " << priv_host_index << " and " << hosts.size() << " hostnames";

		m_privhosts.reserve(m_nprivhosts);

		for (int i = 0; i < m_nprivhosts; ++i)
		{
			int index = (priv_host_index + i + 1) % hosts.size();

			m_privhosts.push_back(hosts[index]);

			BOOST_LOG_TRIVIAL(debug) << Name() << " RelayService::LoadPrivateHosts hostname " << i << " = \"" << m_privhosts[i] << "\"";
		}
	}

	m_connect_error_count.reserve(m_nprivhosts);
	m_connect_time.reserve(m_nprivhosts);

	auto ticks = ccticks();
	if (!ticks)
		ticks = 1;

	for (int i = 0; i < m_nprivhosts; ++i)
	{
		m_connect_error_count.push_back(0);
		m_connect_time.push_back(ticks);
	}

	return 0;
}

int RelayService::GetNextPrivateConnectPeer(uint32_t& when)
{
	int next = -1;

	for (int i = 0; i < m_nprivhosts; ++i)
	{
		if (!m_connect_time[i])
			continue;

		if (next == -1 || ccticks_elapsed(m_connect_time[i], when) > 0)
		{
			next = i;
			when = m_connect_time[i];
		}
	}

	return next;
}

void RelayService::PrivateConnMonitorProc()
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::PrivateConnMonitorProc(" << this << ") started";

	ccsleep(4);

	while (true)
	{
		uint32_t when;

		auto next = GetNextPrivateConnectPeer(when);

		auto delay = ccticks_elapsed(ccticks(), when);

		if (next < 0)
			delay = 10*CCTICKS_PER_SEC;

		BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::PrivateRelayConnMonitorProc m_nprivhosts " << m_nprivhosts << " next " << next << " when " << when << " delay " << delay;

		ccsleep((delay + CCTICKS_PER_SEC/2) / CCTICKS_PER_SEC);

		if (g_shutdown)
			break;

		if (next >= 0)
			PrivateConnectOutgoing(next);
	}

	BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::PrivateConnMonitorProc(" << this << ") ended";
}

void RelayService::PrivateConnectOutgoing(int peer)
{
	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::PrivateConnectOutgoing connecting to peer " << peer << " " << m_privhosts[peer];

	auto host = m_privhosts[peer];
	static const string onion = ".onion";
	int elength = host.length() - onion.length();
	bool use_tor = false;

	//cerr << "host find " << host.rfind(onion) << " =? " << elength << endl;

	if (elength > 0 && (int)host.rfind(onion) == elength)
	{
		use_tor = true;
		host.erase(elength);
		//cerr << "host erase " << host << endl;
	}

	CCServer::pconnection_t connection;

	if (use_tor)
		connection = m_service.GetServer(0).ConnectThruTor(host, g_params.torproxy_port);
	else
		connection = m_service.GetServer(0).Connect("127.0.0.1", atoi(host.c_str()));	// for now, use name as port on localhost

	if (connection)
	{
		((RelayConnection *)connection)->private_peer_index = peer;
		m_connect_time[peer] = 0;	// don't retry
	}
	else
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " RelayService::PrivateConnectOutgoing unable to connect to peer " << peer << " " << host;

		PrivateConnectReschedule(peer);
	}
}

void RelayService::PrivateConnectReschedule(int peer)
{
	if (m_connect_error_count[peer] < 5*60/20)
		++m_connect_error_count[peer];

	unsigned delay = 5 * m_connect_error_count[peer];

	//delay = 10;	// for testing

	m_connect_time[peer] = ccticks() + delay * CCTICKS_PER_SEC;
	if (!m_connect_time[peer])
		m_connect_time[peer] = 1;

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " RelayService::PrivateConnectReschedule scehduling connection to peer " << peer << " in " << delay << " seconds";
}

void RelayService::PrivateConnected(int peer)
{
	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::PrivateConnected peer index " << peer;

	m_connect_error_count[peer] = 0;
}

void RelayService::PrivateDisconnected(int peer)
{
	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::PrivateDisconnected peer index " << peer;

	PrivateConnectReschedule(peer);
}

void RelayService::WaitForShutdown()
{
	if (m_conn_monitor_thread.joinable())
		m_conn_monitor_thread.join();

	if (!enabled)
		return;

	m_service.WaitForShutdown();
}

void RelayThread::ThreadProc(boost::function<void()> threadproc)
{
	relay_dbconn = new DbConn;

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << "RelayThread::ThreadProc start " << (uintptr_t)this << " dbconn " << (uintptr_t)relay_dbconn;

	threadproc();

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << "RelayThread::ThreadProc end " << (uintptr_t)this << " dbconn " << (uintptr_t)relay_dbconn;

	delete relay_dbconn;
}
