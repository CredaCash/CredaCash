/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * relay.cpp
*/

#include "ccnode.h"
#include "relay.hpp"
#include "seqnum.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "witness.hpp"
#include "processtx.hpp"
#include "processblock.hpp"
#include "transact.hpp"
#include "hostdir.hpp"
#include "dbconn.hpp"
#include "dbparamkeys.h"

#include <CCobjects.hpp>
#include <CCmint.h>
#include <transaction.h>
#include <xtransaction-xpay.hpp>
#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#define TRACE_RELAY		(g_params.trace_relay)

#define RELAY_HEARTBEAT				100
#define RELAY_TIMEOUT				40		// TODO: is this the best value

#define RELAY_DOWNLOAD_LOW_WATER	12	//((CC_TX_SEND_MAX)/2)
#define RELAY_DOWNLOAD_HIGH_WATER	5

#define RELAY_DIR_REFRESH				(25*60)

#define RELAY_TIMESTAMP_PAST_ALLOWANCE		(60*60) // >= TRANSACT_TIMESTAMP_PAST_ALLOWANCE + relay expire_age + 10
#define RELAY_TIMESTAMP_FUTURE_ALLOWANCE	(10*60)

//!#define RTEST_CUZZ					3
//!#define RTEST_NO_SEND_TX				4
//!#define RTEST_NO_SEND_BLOCK			16
//!#define RTEST_DELAY_TXS				4		// when set, tx's will only be sent on the private relay
//!#define RTEST_DELAY_BLOCKS			4		// when set, blocks will only be sent on the private relay
//!#define TEST_NO_TX_PREVALIDATION		1
//#define TEST_DOUBLECHECK_BLOCK_OIDS	1

#ifndef RTEST_CUZZ
#define RTEST_CUZZ						0	// don't test
#endif

#ifndef RTEST_NO_SEND_TX
#define RTEST_NO_SEND_TX				0	// don't test
#endif

#ifndef RTEST_NO_SEND_BLOCK
#define RTEST_NO_SEND_BLOCK				0	// don't test
#endif

#ifndef RTEST_DELAY_TXS
#define RTEST_DELAY_TXS					0	// don't test
#endif

#ifndef RTEST_DELAY_BLOCKS
#define RTEST_DELAY_BLOCKS				0	// don't test
#endif

#ifndef TEST_NO_TX_PREVALIDATION
#define TEST_NO_TX_PREVALIDATION		0	// don't test
#endif

#ifndef TEST_DOUBLECHECK_BLOCK_OIDS
#define TEST_DOUBLECHECK_BLOCK_OIDS		0	// don't test
#endif

#pragma pack(push, 1)

//static const uint32_t Success_Reply[2] =			{CC_MSG_HEADER_SIZE, CC_SUCCESS};
//static const uint32_t Success_Reply_Queue_Len[3] =	{CC_MSG_HEADER_SIZE + sizeof(uint32_t), CC_SUCCESS_QUEUE_LEN, 0};
static const uint32_t Buffer_Full_Reply[2] =			{CC_MSG_HEADER_SIZE, CC_RESULT_BUFFER_FULL};
//static const uint32_t Buffer_Busy_Reply[2] =		{CC_MSG_HEADER_SIZE, CC_RESULT_BUFFER_BUSY};
//static const uint32_t Server_Busy_Reply[2] =		{CC_MSG_HEADER_SIZE, CC_RESULT_SERVER_BUSY};
//static const uint32_t Server_Err_Reply[2] =			{CC_MSG_HEADER_SIZE, CC_RESULT_SERVER_ERR};
static const uint32_t Bad_Cmd_Reply[2] =				{CC_MSG_HEADER_SIZE, CC_ERROR_BAD_CMD};
//static const uint32_t Bad_Param_Reply[2] =			{CC_MSG_HEADER_SIZE, CC_ERROR_BAD_PARAM};
static const uint32_t No_Obj_Reply[2] =				{CC_MSG_HEADER_SIZE, CC_NO_OBJ};
//static const uint32_t Send_Q_Reset_Reply[2] =		{CC_MSG_HEADER_SIZE, CC_ERROR_SEND_Q_RESET};

#pragma pack(pop)

thread_local static DbConn *relay_dbconn;

void RelayConnection::StartConnection()
{
	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::StartConnection";

	m_conn_state = CONN_CONNECTED;

	if (private_peer_index >= 0)
		g_privrelay_service.PrivateConnected(private_peer_index);

	peer_error_count = 0;

	db_next_new_block_seqnum = g_seqnum[BLOCKSEQ][VALIDSEQ].seqmin;	// announce existing blocks
	db_next_new_xreq_seqnum = g_seqnum[XREQSEQ][VALIDSEQ].seqmin;	// announce existing exchange requests

	if ((g_transact_service.enabled && !g_transact_service.relay_only) || (Implement_CCMint(g_params.blockchain) && g_blockchain.GetLastIndelibleLevel() < CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN))
		db_next_new_tx_seqnum = g_seqnum[TXSEQ][VALIDSEQ].seqmin;	// announce existing tx's
	else
		db_next_new_tx_seqnum = g_seqnum[TXSEQ][VALIDSEQ].Peek();	// don't announce existing tx's

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RelayConnection::StartConnection starting seqnum's: block " << db_next_new_block_seqnum << " tx " << db_next_new_tx_seqnum << " xreq " << db_next_new_xreq_seqnum;

	request_msg_buf_in_use.clear();
	request_objs_pending.store(0);
	request_bytes_pending.store(0);
	request_param_queue.clear();

	send_queue.clear();
	send_one.clear();

	if (SetHeartbeatTimer())
		return;

	StartRead();
}

void RelayConnection::HandleReadComplete()
{
	if (m_nred < CC_MSG_HEADER_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleReadComplete error short read " << m_nred;

		return Stop();
	}

	unsigned size = *(uint32_t*)m_pread;
	unsigned tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleReadComplete read " << m_nred << " bytes msg size " << size << " tag " << hex << tag << dec;

	if (size < CC_MSG_HEADER_SIZE || size > CC_BLOCK_MAX_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleReadComplete error invalid msg size " << size;

		return Stop();
	}

	SmartBuf smartobj;

	switch (tag)
	{
	case CC_TAG_BLOCK:
	case CC_TAG_TX:
	case CC_TAG_MINT:
	case CC_TAG_TX_XDOMAIN:
	case CC_TAG_XCX_NAKED_BUY:
	case CC_TAG_XCX_NAKED_SELL:
	case CC_TAG_XCX_SIMPLE_BUY:
	case CC_TAG_XCX_SIMPLE_SELL:
	case CC_TAG_XCX_SIMPLE_TRADE:
	case CC_TAG_XCX_PAYMENT:
	{
		CCASSERT(CC_MSG_HEADER_SIZE == sizeof(CCObject::Header));

		smartobj = SmartBuf(size + sizeof(CCObject::Preamble));
		if (!smartobj)
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleReadComplete error smartobj failed";

			return;
		}

		auto obj = (CCObject*)smartobj.data();

		memcpy(obj->ObjPtr(), m_pread, m_nred);

		m_pread = (char*)obj->ObjPtr();

		break;
	}

	default:
		if (size > CC_MAX_MSG_SIZE)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleReadComplete error msg tag " << hex << tag << dec << " size " << size << " exceeds " << CC_MAX_MSG_SIZE;

			return Stop();
		}
	}

	m_maxread = size;

	if (m_nred < m_maxread)
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleReadComplete queueing read size " << m_maxread - m_nred;

		ReadAsync("RelayConnection::HandleReadComplete", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(m_maxread - m_nred),
				boost::bind(&RelayConnection::HandleMsgReadComplete, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, smartobj, AutoCount(this)));
	}
	else
	{
		HandleMsgReadComplete(boost::system::error_code(), 0, smartobj, AutoCount(this));	// don't need to increment op count, but too much effort to chain the AutoCount from the function calling HandleReadComplete
	}
}

void RelayConnection::HandleMsgReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	m_nred += bytes_transferred;

	bool sim_err = RandTest(RTEST_READ_ERRORS);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete simulating read error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

		return Stop();
	}

	if (m_nred != m_maxread)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error short read " << m_nred;

		return Stop();
	}

	auto msgsize = *(uint32_t*)m_pread;
	auto tag = *(uint32_t*)(m_pread + 4);

	if (msgsize != m_nred)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error size mismatch msgsize " << msgsize << " != m_nred " << m_nred;

		return Stop();
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete read " << m_nred << " bytes msg size " << msgsize << " tag " << hex << tag << dec;

	CCASSERT(msgsize >= CC_MSG_HEADER_SIZE);

	// !!! need some protection from peer overloading us with data

	int objs_pending = -1;
	Process_Q_Priority priority = PROCESS_Q_PRIORITY_TX;

	switch (tag)
	{

	case CC_MSG_HAVE_BLOCK:

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_MSG_HAVE_BLOCK";

		goto handle_have;

	case CC_MSG_HAVE_TX:

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_MSG_HAVE_TX";
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
				copy_from_buf(req_params, sizeof(req_params), bufpos, m_pread, msgsize);

				if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_BLOCK at level " << req_params.level << " prune_level " << prune_level;
			}
			else if (tag == CC_MSG_HAVE_TX)
			{
				copy_from_buf(req_params.oid, sizeof(req_params.oid), bufpos, m_pread, msgsize);
				copy_from_buf(req_params.level, sizeof(req_params.level), bufpos, m_pread, msgsize);
				copy_from_buf(req_params.size, sizeof(req_params.size), bufpos, m_pread, msgsize);

				if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_TX at param_level " << req_params.level << " size " << req_params.size << " oid " << buf2hex(&req_params.oid, CC_OID_TRACE_SIZE);
			}
			else
				CCASSERT(0);

			if (bufpos > msgsize)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_BLOCK/CC_MSG_HAVE_TX tag " << hex << tag << dec << " error msg overrun msgsize " << msgsize << " bufpos " << bufpos;

				return Stop();
			}

			if (req_params.level > INT64_MAX)
			{
				// must be invalid because sqlite think it's a negative value
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_BLOCK/CC_MSG_HAVE_TX tag " << hex << tag << dec << " error invalid level " << req_params.level;

				return Stop();
			}

			if (tag == CC_MSG_HAVE_BLOCK && req_params.level < prune_level)
			{
				if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_BLOCK skipping download of block at level " << req_params.level << " prune_level " << prune_level;

				continue;
			}

			if (tag == CC_MSG_HAVE_TX && SmartBuf::ByteTotal() > ((int64_t)g_params.max_obj_mem << 20))
			{
				BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_MSG_HAVE_TX skipping download of tx because memory usage " << SmartBuf::ByteTotal() << " > " << ((int64_t)g_params.max_obj_mem << 20);

				continue;
			}

			#if RTEST_DELAY_BLOCKS && RTEST_DELAY_TXS
			#error both RTEST_DELAY_BLOCKS and RTEST_DELAY_TXS are set
			#endif
			if ((!RTEST_DELAY_BLOCKS || tag != CC_MSG_HAVE_BLOCK) && (!RTEST_DELAY_TXS || tag != CC_MSG_HAVE_TX))
				relay_dbconn->RelayObjsInsert(m_conn_index, (tag == CC_MSG_HAVE_BLOCK) ? CC_TYPE_BLOCK : CC_TYPE_TXPAY, req_params, RELAY_STATUS_ANNOUNCED, RELAY_PEER_STATUS_READY);
			else if (private_peer_index >= 0)
			{
				// with TEST_DELAY_BLOCKS, blocks are requested on the private relay only, after a delay
				//	as a result, undelayed tx's on the public relay should end up being requested first
				// with RTEST_DELAY_TXS, blocks are requested on the private relay only, after a delay
				//	as a result, undelayed blocks on the public relay should end up being requested first
				if (RandTest(RTEST_DELAY_BLOCKS)) usleep(rand() & (1024*1024-1));
				if (RandTest(RTEST_DELAY_TXS)) usleep(rand() & (1024*1024-1));
				relay_dbconn->RelayObjsInsert(m_conn_index, (tag == CC_MSG_HAVE_BLOCK) ? CC_TYPE_BLOCK : CC_TYPE_TXPAY, req_params, RELAY_STATUS_ANNOUNCED, RELAY_PEER_STATUS_READY);
			}
		}

		objs_pending = 0;	// so we'll CheckForDownload

		break;
	}

	case CC_CMD_SEND_BLOCK:
	case CC_CMD_SEND_TX:
	{
		unsigned nobjs = (msgsize - CC_MSG_HEADER_SIZE) / sizeof(ccoid_t);

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " received nobjs " << nobjs;

		if (msgsize != CC_MSG_HEADER_SIZE + nobjs * sizeof(ccoid_t))
		{
			BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " error invalid msgsize " << msgsize << " (nobjs " << nobjs << " -> msgsize " << CC_MSG_HEADER_SIZE + nobjs * sizeof(ccoid_t) << ") sending CC_ERROR_BAD_CMD";

			WriteAsync("RelayConnection::HandleMsgReadComplete", boost::asio::buffer(Bad_Cmd_Reply, sizeof(Bad_Cmd_Reply)),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			break;

			CCASSERT(0);
		}

		unique_lock<FastSpinLock> lock(send_queue_lock);

		if (send_queue.space() < nobjs)
		{
			BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " error insufficient space in send queue; nobjs " << nobjs << " space " << send_queue.space() << " sending CC_RESULT_BUFFER_FULL";

			lock_guard<mutex> write_pending_lock(next_writer_mutex);

			lock.unlock();	// unlock this after taking next_writer_mutex, so replies don't get out of order

			WriteAsync("RelayConnection::HandleMsgReadComplete", boost::asio::buffer(Buffer_Full_Reply, sizeof(Buffer_Full_Reply)),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)), true);

			break;

			CCASSERT(0);
		}

		for (unsigned i = 0; i < nobjs; ++i)
		{
			auto poid = m_pread + CC_MSG_HEADER_SIZE + i * sizeof(ccoid_t);

			BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " oid " << buf2hex(poid, CC_OID_TRACE_SIZE);

			auto rc = send_queue.push(poid, sizeof(ccoid_t));
			if (rc)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error queuing tag " << hex << tag << dec << " oid " << buf2hex(poid, CC_OID_TRACE_SIZE);

				return Stop();
			}
		}

#if 0 // CC_SUCCESS_QUEUE_LEN not implemented
		{
			auto qlen = send_queue.size();

			lock_guard<mutex> write_pending_lock(next_writer_mutex);

			lock.unlock();	// unlock this after taking next_writer_mutex, so replies don't get out of order

			auto msgbuf = SmartBuf(sizeof(Success_Reply_Queue_Len));
			if (!msgbuf)
			{
				BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error msgbuf failed";

				return Stop();
			}

			memcpy(msgbuf.data(), Success_Reply_Queue_Len, sizeof(Success_Reply_Queue_Len));

			*(uint32_t*)(msgbuf.data() + 8) = qlen;

			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete sending CC_SUCCESS_QUEUE_LEN " << qlen;

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
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_SUCCESS_QUEUE_LEN";

		if (msgsize != sizeof(Success_Reply_Queue_Len))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error CC_SUCCESS_QUEUE_LEN size mismatch msgsize " << msgsize << " != " << sizeof(Success_Reply_Queue_Len);

			return Stop();
		}

		auto nobjs = *(uint32_t*)(m_pread + 8);

		// would need to sanity check this nobjs

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_SUCCESS_QUEUE_LEN " << nobjs;

		request_objs_pending.store(nobjs);

		break;
	}
#endif

	case CC_NO_OBJ:
	{
		int64_t bytes_pending;

		{
			lock_guard<FastSpinLock> lock(request_queue_lock);

			auto params = (relay_request_params_extended_t*)request_param_queue.pop(sizeof(relay_request_params_extended_t));
			if (!params)
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error received CC_NO_OBJ but no object was expected";

				return Stop();
			}

			objs_pending = request_objs_pending.fetch_sub(1) - 1;
			bytes_pending = request_bytes_pending.fetch_sub(params->size) - params->size;
			last_valid_obj_time = unixtime();

			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_NO_OBJ; still pending " << objs_pending << " objects in " << bytes_pending << " bytes; skipped requested obj oid " << buf2hex(&params->oid, CC_OID_TRACE_SIZE);

			CCASSERT(objs_pending >= 0);
			CCASSERT(bytes_pending >= 0);
		}

		break;
	}

	case CC_TAG_XCX_NAKED_BUY:
	case CC_TAG_XCX_NAKED_SELL:
	case CC_TAG_XCX_SIMPLE_BUY:
	case CC_TAG_XCX_SIMPLE_SELL:
	case CC_TAG_XCX_SIMPLE_TRADE:

		if (priority == PROCESS_Q_PRIORITY_TX)
			priority = PROCESS_Q_PRIORITY_X_REQ;

		// no break, intentional
		// FALLTHROUGH

	case CC_TAG_TX:
	case CC_TAG_MINT:
	case CC_TAG_TX_XDOMAIN:
	case CC_TAG_XCX_PAYMENT:

		if (TEST_NO_TX_PREVALIDATION && !IsWitness() && RandTest(2))
			break; // for testing, don't pre-validate this tx

		// no break, intentional
		// FALLTHROUGH

	case CC_TAG_BLOCK:
	{
		relay_request_params_extended_t req_params;

		CCObject* obj = NULL;
		block_hash_t block_hash;
		ccoid_t oid;
		bool need_oid = true;

		bool disregard_object = false;
		ccoid_t *prior_oid = NULL;
		int64_t level = 0;

		for (bool firstpass = true; ; firstpass = false)
		{
			{
				lock_guard<FastSpinLock> lock(request_queue_lock);

				auto params = (relay_request_params_extended_t*)request_param_queue.pop(sizeof(relay_request_params_extended_t));
				if (!params)
				{
					if (firstpass)
						BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error received tag " << hex << tag << dec << " but no object was expected";
					else
						BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error received tag " << hex << tag << dec << " but no matching object found in request queue";

					return Stop();
				}

				memcpy(&req_params, params, sizeof(req_params));
			}

			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete request params size " << req_params.size << " level " << req_params.level << " witness " << (unsigned)req_params.witness << " oid " << buf2hex(&req_params.oid, CC_OID_TRACE_SIZE) << " prior_oid " << buf2hex(&req_params.prior_oid, CC_OID_TRACE_SIZE);

			objs_pending = request_objs_pending.fetch_sub(1) - 1;
			auto bytes_pending = request_bytes_pending.fetch_sub(req_params.size) - req_params.size;

			CCASSERT(objs_pending >= 0);
			CCASSERT(bytes_pending >= 0);

			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete still pending " << objs_pending << " objects in " << bytes_pending << " bytes";

			if (firstpass)
			{
				obj = (CCObject*)smartobj.data();

				CCASSERT(m_pread == (char*)obj->ObjPtr());

				if (!obj->IsValid())
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " error object IsValid false";

					return Stop();
				}
			}

			if (tag != CC_TAG_BLOCK)
			{
				if (firstpass)
				{
					obj->SetObjId();

					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received tag " << hex << tag << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
				}

				if (memcmp(&req_params.oid, obj->OidPtr(), sizeof(ccoid_t)))
				{
					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " error next requested tx oid " << buf2hex(&req_params.oid, CC_OID_TRACE_SIZE) << " != received oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

					continue;	// assume this requested object will not be sent, but check if the received object matches a later requested object
				}

				if (req_params.size != msgsize)
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " error expected object size " << req_params.size << " != received size " << msgsize;

					return Stop();
				}

				auto param_level = txpay_param_level_from_wire(obj);
				if (param_level == (uint64_t)(-1))
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " error invalid tx size " << obj->DataSize();

					return Stop();
				}

				if (req_params.level != param_level)
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " error expected param_level " << req_params.level << " != received param_level " << param_level;

					return Stop();
				}

				if (Implement_CCMint(g_params.blockchain))
				{
					auto block_level = g_blockchain.GetLastIndelibleLevel();

					if (tag == CC_TAG_MINT)
					{
						if (!param_level
							||	(param_level == 1 &&				block_level > CC_MINT_ACCEPT_SPAN + 1)
							||	(param_level  > 1 &&				param_level + CC_MINT_ACCEPT_SPAN + 1 < block_level)
							||										param_level	>= CC_MINT_COUNT
							||										param_level	> block_level + 1
							||	(g_witness.WitnessIndex() == 0 &&	param_level	> block_level))
						{
							BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_MINT INVALID param level " << param_level << " for mint tx at blockchain level " << block_level;

							disregard_object = true;

							break;
						}
					}
					else
					{
						if (param_level < CC_MINT_COUNT + CC_MINT_ACCEPT_SPAN)
						{
							BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " INVALID param level " << param_level << " for non-mint tx at blockchain level " << block_level;

							disregard_object = true;

							break;
						}
					}
				}
				else if (tag == CC_TAG_MINT && !IsTestnet(g_params.blockchain))
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_MINT INVALID on non-testnet";

					return Stop();
				}

				auto timestamp = *(uint64_t*)(obj->ObjPtr() + CC_MSG_HEADER_SIZE);

				if (tx_check_timestamp(timestamp, RELAY_TIMESTAMP_PAST_ALLOWANCE, RELAY_TIMESTAMP_FUTURE_ALLOWANCE))
				{
					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " timestamp " << timestamp << " not valid";

					return Stop();
				}

				auto difficulty = g_params.tx_work_difficulty;
				if (tag == CC_TAG_XCX_PAYMENT)
					 difficulty = g_params.xcx_pay_work_difficulty;

				if (tx_set_work_internal((char*)(obj->ObjPtr()), obj->OidPtr(), 0, TX_POW_NPROOFS, 1, difficulty))
				{
					// note: Proof of Work might fail because the peer tampered with the nonces. To prevent this from be used as a Denial of Service attack,
					// we have not yet set the object status to RELAY_STATUS_DOWNLOADED, so it can be downloaded again from a different peer that thinks the Tx is valid

					BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " proof-of-work not valid";

					return Stop();
				}

				auto mint_level = max_mint_level.load();

				if (Implement_CCMint(g_params.blockchain) && tag == CC_TAG_MINT)
				{
					if (g_witness.WitnessIndex() == 0 && (param_level == mint_level || (param_level == 1 && mint_level <= CC_MINT_ACCEPT_SPAN)))
					{
						// check if this is a valid mint tx at the next higher level

						TxPay txbuf;

						auto rc = ProcessTx::TxValidate(relay_dbconn, txbuf, smartobj);
						if (rc)
						{
							BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_MINT INVALID mint tx at mint level " << mint_level;

							disregard_object = true;

							break;
						}
						else
						{
							if (max_mint_level.compare_exchange_strong(mint_level, mint_level + 1))
								BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_MINT set max_mint_level = " << mint_level + 1;
						}
					}

					relay_dbconn->ParameterIncrement(DB_KEY_CCMINT_COUNT, param_level); // changes PersistentDb
				}

				break;
			}
			else
			{
				auto block = (Block*)obj;
				auto wire = block->WireData();

				if (firstpass)
				{
					if (block->BodySize() < sizeof(BlockWireHeader))
					{
						BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error object too small size " << block->BodySize();

						return Stop();
					}

					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received CC_TAG_BLOCK size " << obj->ObjSize() << " prior oid " << buf2hex(&wire->prior_oid, CC_OID_TRACE_SIZE);
				}

				if (memcmp(&req_params.prior_oid, &wire->prior_oid, sizeof(ccoid_t)))
				{
					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error next requested block prior oid " << buf2hex(&req_params.prior_oid, CC_OID_TRACE_SIZE) << " != received block prior oid " << buf2hex(&wire->prior_oid, CC_OID_TRACE_SIZE);

					continue;
				}

				if (req_params.level != wire->level.GetValue())
				{
					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error requested block level " << req_params.level << " != received block level " << wire->level.GetValue();

					continue;
				}

				if (req_params.witness != wire->witness)
				{
					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error requested block witness " << (unsigned)req_params.witness << " != received block witness " << (unsigned)wire->witness;

					continue;
				}

				if (req_params.size != msgsize)
				{
					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error expected size " << req_params.size << " != received size " << msgsize;

					continue;
				}

				if (need_oid)
				{
					block->CalcHash(block_hash);

					block->CalcOid(block_hash, oid);

					need_oid = false;
				}

				if (TEST_SEQ_BLOCK_OID)
					memcpy(&oid, &req_params.oid, sizeof(ccoid_t));

				if (memcmp(&req_params.oid, &oid, sizeof(ccoid_t)))
				{
					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error requested block size " << block->ObjSize() << " oid " << buf2hex(&req_params.oid, CC_OID_TRACE_SIZE) << "!= computed oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);
					//BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete block dump " << buf2hex(block, block->ObjSize());

					continue;
				}

				auto auxp = block->SetupAuxBuf(smartobj, true);
				if (!auxp)
				{
					BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete CC_TAG_BLOCK error SetupAuxBuf failed";

					return Stop();
				}

				auxp->SetHash(block_hash);
				auxp->SetOid(oid);

				auxp->announce_ticks = req_params.announce_ticks;

				prior_oid = &wire->prior_oid;
				level = wire->level.GetValue();

				break;
			}
		}

		{
			lock_guard<FastSpinLock> lock(request_queue_lock);

			last_valid_obj_time = unixtime();
		}

		if (disregard_object)
		{
			BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete tag " << hex << tag << dec << " disregarding received obj bufp " << (uintptr_t)smartobj.BasePtr() << " tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

			break;
		}

		if (TRACE_XPAYS && obj->ObjTag() == CC_TAG_XCX_PAYMENT)
		      BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received obj bufp " << (uintptr_t)smartobj.BasePtr() << " tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
		else BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete received obj bufp " << (uintptr_t)smartobj.BasePtr() << " tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

		relay_dbconn->RelayObjsSetStatus(*obj->OidPtr(), RELAY_STATUS_DOWNLOADED, 0);

		if (Implement_CCMint(g_params.blockchain) && tag == CC_TAG_MINT)
		{
			auto rc = relay_dbconn->ValidObjsInsert(smartobj);
			if (rc < 0)
				BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete ValidObjsInsert failed";
			else
				g_witness.NotifyNewWork(TXSEQ);
		}
		else if (tag == CC_TAG_BLOCK)
		{
			relay_dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, prior_oid, level, PROCESS_Q_STATUS_PENDING, PROCESS_Q_PRIORITY_BLOCK, false, m_conn_index, m_use_count.load());
		}
		else
		{
			auto rc = ProcessTx::TxEnqueueValidate(relay_dbconn, false, false, priority, smartobj, m_conn_index, m_use_count.load());
			if (TRACE_XPAYS && obj->ObjTag() == CC_TAG_XCX_PAYMENT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete TxEnqueueValidate returned " << rc << " for obj bufp " << (uintptr_t)smartobj.BasePtr() << " tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

			if (rc == 1)
			{
				// tx is already validated; ok to skip following line in this situation, since it does nothing when the tx is valid:
				//HandleValidateDone(0, m_use_count.load(), 0);
			}
		}

		break;
	}

	default:
		BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleMsgReadComplete error unrecognized message tag " << hex << tag << dec;
		break;
	}

	StartRead();

	if (objs_pending >= 0 && objs_pending < RELAY_DOWNLOAD_LOW_WATER)
		CheckForDownload();
}

void RelayConnection::CheckToSend()
{
	if (send_one.test_and_set())		// make sure only one thread is sending so the send_queue objects are sent in the order they were queued
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend already in progress";

		return;
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend";

	while (!g_shutdown)
	{
		ccoid_t oid;

		{
			lock_guard<FastSpinLock> lock(send_queue_lock);

			auto oidp = send_queue.pop(sizeof(ccoid_t));
			if (!oidp)
			{
				if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend nothing in queue";

				send_one.clear();

				return;
			}

			memcpy(&oid, oidp, sizeof(ccoid_t));
		}

		SmartBuf smartobj;

		auto rc = relay_dbconn->ValidObjsGetObj(oid, &smartobj);
		if (rc)
		{
			if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend unable to retrieve object oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

			WriteAsync("RelayConnection::HandleMsgReadComplete", boost::asio::buffer(No_Obj_Reply, sizeof(No_Obj_Reply)),
					boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

			continue;		// try the next object in the queue
		}

		auto obj = (CCObject*)smartobj.data();
		CCASSERT(obj);

		auto size = obj->ObjSize();

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend buf " << (uintptr_t)smartobj.BasePtr() << " tag " << hex << obj->ObjTag() << dec << " size " << size << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

		if (size < CC_MSG_HEADER_SIZE || size > CC_BLOCK_MAX_SIZE)
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend error object invalid size " << size;

			continue;		// try the next object in the queue
		}

		switch (obj->ObjTag())
		{
		case CC_TAG_BLOCK:

			if (RandTest(RTEST_NO_SEND_BLOCK))
			{
				BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend test skipping send of block oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

				continue;
			}
			//if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend sending CC_TAG_BLOCK size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE); // << " block dump " << buf2hex(obj, obj->ObjSize());
			if (TEST_DOUBLECHECK_BLOCK_OIDS) ((Block*)obj)->SetOrVerifyOid(false);
			break;

		case CC_TAG_TX:
		case CC_TAG_MINT:
		case CC_TAG_TX_XDOMAIN:
		case CC_TAG_XCX_NAKED_BUY:
		case CC_TAG_XCX_NAKED_SELL:
		case CC_TAG_XCX_SIMPLE_BUY:
		case CC_TAG_XCX_SIMPLE_SELL:
		case CC_TAG_XCX_SIMPLE_TRADE:
		case CC_TAG_XCX_PAYMENT:

			if (RandTest(RTEST_NO_SEND_TX))
			{
				BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend test skipping send of tx tag " << hex << obj->ObjTag() << dec << " oid " << buf2hex(&oid, CC_OID_TRACE_SIZE);

				// sending No_Obj_Reply is not required, however, if the relay skips sending too many objects in a row without a No_Obj_Reply,
				// the peer request queue will fill and it will stall until it internally generates a "peer send timeout"

				WriteAsync("RelayConnection::HandleMsgReadComplete", boost::asio::buffer(No_Obj_Reply, sizeof(No_Obj_Reply)),
						boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

				continue;
			}
			//if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend sending tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
			if (TRACE_XPAYS && obj->ObjTag() == CC_TAG_XCX_PAYMENT) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend sending tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
			break;

		default:
			//BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend unknown object tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);
			continue;		// try the next object in the queue
		}

		if (RTEST_CUZZ) ccsleep(rand() & RTEST_CUZZ);

		if (WriteAsync("RelayConnection::CheckToSend", boost::asio::buffer(obj->ObjPtr(), size),
				boost::bind(&RelayConnection::HandleObjWrite, this, boost::asio::placeholders::error, smartobj, AutoCount(this))))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckToSend WriteAsync error";

			send_one.clear();
		}

		return;
	}
}

void RelayConnection::HandleObjWrite(const boost::system::error_code& e, SmartBuf smartobj, AutoCount pending_op_counter)
{
	send_one.clear();

	smartobj.ClearRef();	// we're done with this, so might as well free it now

	if (CheckOpCount(pending_op_counter))
		return;

	if (e) return Stop();

	m_write_in_progress.clear();

	CheckToSend();
}

void RelayConnection::CheckForDownload()
{
	if (request_msg_buf_in_use.test_and_set())
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload buffer in use";

		return;
	}

	auto objs_pending = request_objs_pending.load();

	if (objs_pending > RELAY_DOWNLOAD_LOW_WATER)
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload still have " << objs_pending << " objects pending";

		request_msg_buf_in_use.clear();

		return;
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload checking for downloads...";

	bool have_blocks = false;
	unsigned nobjs = 0;
	unsigned nbytes = 0;

	//cerr << "CC_TX_SEND_MAX " << CC_TX_SEND_MAX << " RELAY_DOWNLOAD_HIGH_WATER " << RELAY_DOWNLOAD_HIGH_WATER << " objs_pending " << objs_pending << " max req " << CC_TX_SEND_MAX - RELAY_DOWNLOAD_HIGH_WATER - objs_pending << endl;

	relay_dbconn->RelayObjsFindDownloads(m_conn_index, g_blockchain.GetLastIndelibleLevel(), &request_msg_buf[0], sizeof(request_msg_buf), req_param_buf, CC_TX_SEND_MAX - RELAY_DOWNLOAD_HIGH_WATER - objs_pending, request_bytes_pending.load(), have_blocks, nobjs, nbytes);

	if (nobjs)
	{
		lock_guard<FastSpinLock> lock(request_queue_lock);

		int64_t reqsize = 0;

		for (unsigned i = 0; i < nobjs; ++i)
		{
			auto rc = request_param_queue.push(&req_param_buf[i], sizeof(req_param_buf[i]));
			if (rc)
			{
				BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload error queuing request params size " << req_param_buf[i].size << " level " << req_param_buf[i].level << " witness " << (unsigned)req_param_buf[i].witness << " oid " << buf2hex(&req_param_buf[i].oid, CC_OID_TRACE_SIZE);

				return Stop();
			}

			reqsize += req_param_buf[i].size;
			last_valid_obj_time = unixtime();

			if (TRACE_RELAY)
			{
				if (have_blocks)
					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload queued request params size " << req_param_buf[i].size << " level " << req_param_buf[i].level << " witness " << (unsigned)req_param_buf[i].witness << " oid " << buf2hex(&req_param_buf[i].oid, CC_OID_TRACE_SIZE) << " prior_oid " << buf2hex(&req_param_buf[i].prior_oid, CC_OID_TRACE_SIZE);
				else
					BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload queued request params size " << req_param_buf[i].size << " level " << req_param_buf[i].level << " witness " << (unsigned)req_param_buf[i].witness << " oid " << buf2hex(&req_param_buf[i].oid, CC_OID_TRACE_SIZE);
			}
		}

		auto objs_pending = request_objs_pending.fetch_add(nobjs) + nobjs;
		auto bytes_pending = request_bytes_pending.fetch_add(reqsize) + reqsize;

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload found " << nobjs << " objects in " << reqsize << " bytes; total now pending " << objs_pending << " objects in " << bytes_pending;
	}

	if (TRACE_RELAY && nbytes) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload RelayObjsFindDownloads returned " << nobjs << " objects in " << nbytes << " bytes sending CC_CMD_SEND_BLOCK/CC_CMD_SEND_TX";
	else if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::CheckForDownload RelayObjsFindDownloads returned " << nobjs << " objects in " << nbytes;

	if (!nbytes)
	{
		request_msg_buf_in_use.clear();

		return;
	}

	if (WriteAsync("RelayConnection::CheckForDownload", boost::asio::buffer(&request_msg_buf, nbytes),
			boost::bind(&RelayConnection::HandleSendMsgWrite, this, boost::asio::placeholders::error, AutoCount(this))))
	{
		request_msg_buf_in_use.clear();
	}
}

void RelayConnection::HandleSendMsgWrite(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	request_msg_buf_in_use.clear();

	if (CheckOpCount(pending_op_counter))
		return;

	if (e) return Stop();

	m_write_in_progress.clear();

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleSendMsgWrite ok";

	//CheckForDownload(); // commented out cause we don't need to check again here
}

bool RelayConnection::SetHeartbeatTimer()
{
	//if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::SetHeartbeatTimer";

	uint32_t ms = RELAY_HEARTBEAT;
	if (g_params.trace_level >= 6 && TRACE_RELAY && ms < CCTICKS_PER_SEC)
		ms = CCTICKS_PER_SEC;		// slow down so it doesn't overload the log output

	return TimerWaitAsync("RelayConnection::SetHeartbeatTimer", ms, boost::bind(&RelayConnection::HandleHeartbeat, this, boost::asio::placeholders::error, AutoCount(this)));
}

void RelayConnection::HandleHeartbeat(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	if (e == boost::asio::error::operation_aborted)
		return;

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleHeartbeat " << uintptr_t(this) << " e = " << e << " " << e.message();

	if (request_objs_pending.load())
	{
		lock_guard<FastSpinLock> lock(request_queue_lock);

		uint32_t now = unixtime();
		if ((int32_t)(now - last_valid_obj_time) > RELAY_TIMEOUT)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleHeartbeat peer send timeout ref time " << last_valid_obj_time << " current time " << now;

			return Stop();
		}
	}

	CheckForDownload();	// we do this on the same timer just to make it easier

	unsigned nbytes;

	nbytes = relay_dbconn->ValidObjsFindNew(db_next_new_block_seqnum, g_seqnum[BLOCKSEQ][VALIDSEQ].seqmax, CC_HAVE_MAX, true, &announce_msg_buf[0], sizeof(announce_msg_buf));

	if (!nbytes)
		nbytes = relay_dbconn->ValidObjsFindNew(db_next_new_xreq_seqnum, g_seqnum[XREQSEQ][VALIDSEQ].seqmax, CC_HAVE_MAX, true, &announce_msg_buf[0], sizeof(announce_msg_buf));

	if (!nbytes)
		nbytes = relay_dbconn->ValidObjsFindNew(db_next_new_tx_seqnum, g_seqnum[TXSEQ][VALIDSEQ].seqmax, CC_HAVE_MAX, true, &announce_msg_buf[0], sizeof(announce_msg_buf));

	if (nbytes)
	{
		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleHeartbeat sending CC_MSG_HAVE_BLOCK/CC_MSG_HAVE_TX";

		WriteAsync("RelayConnection::HandleHeartbeat", boost::asio::buffer(&announce_msg_buf, nbytes),
				boost::bind(&RelayConnection::HandleAnnounceMsgWrite, this, boost::asio::placeholders::error, AutoCount(this)));
	}
	else
		SetHeartbeatTimer();
}

void RelayConnection::HandleAnnounceMsgWrite(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	if (e) return Stop();

	m_write_in_progress.clear();

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::HandleAnnounceMsgWrite ok";

	// announce_msg_buf is no longer in use, so we can now restart the timer to look for more objects to announce

	HandleHeartbeat(e, pending_op_counter);		// check for more
}

void RelayConnection::FinishConnection()
{
	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RelayConnection::FinishConnection";

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

	auto last_dir_refresh_time = ccticks() - RELAY_DIR_REFRESH * CCTICKS_PER_SEC;

	while (!g_shutdown)
	{
		unsigned outcount = m_service.GetServer(si).GetConnectionManager().GetOutgoingConnectionCount();

		if (TRACE_RELAY) BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::ConnMonitorProc currently " << outcount << " outgoing connections";

		if ((int)outcount < max_outconns)
		{
			ConnectOutgoing();
			last_dir_refresh_time = ccticks();
		}
		else if (ccticks_elapsed(last_dir_refresh_time, ccticks()) >= RELAY_DIR_REFRESH * CCTICKS_PER_SEC)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " RelayService::ConnMonitorProc refreshing peer directory entry...";

			g_hostdir.GetHostName((HostDir::HostType)(-1));	// let rendezvous know we're here
			last_dir_refresh_time = ccticks();
		}

		ccsleep(12);
	}

	BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::ConnMonitorProc(" << this << ") ended";
}

void RelayService::ConnectOutgoing()
{
	BOOST_LOG_TRIVIAL(info) << Name() << " RelayService::ConnectOutgoing calling GetHostName()";

	auto peer = g_hostdir.GetHostName(HostDir::Relay);

	if (!peer.size())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " RelayService::ConnectOutgoing no relay peers found";

		return;
	}

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(info) << Name() << " RelayService::ConnectOutgoing connecting to " << peer;

	m_service.GetServer(0).Connect(peer, g_params.torproxy_port, true);
}

void RelayService::ConfigPrivateRelay()
{
	//BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::ConfigPrivateRelay enabled " << enabled << " priv_hosts_file " << w2s(priv_hosts_file);
	//cout << "RelayService::ConfigPrivateRelay enabled " << enabled << " priv_hosts_file " << w2s(priv_hosts_file) << endl;

	if (enabled) BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::ConfigPrivateRelay";

	if (enabled && priv_hosts_file.length())
	{
		LoadPrivateHosts();

		max_outconns = m_nprivhosts;
	}

	if (max_inconns < 0)
		max_inconns = max_outconns + 1 + 3; // 1 extra for possible round down when dividing by 2, plus 3 extra to ensure there's a free connection when a host attempts to reconnect
}

int RelayService::LoadPrivateHosts()
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " RelayService::LoadPrivateHosts file \"" << w2s(priv_hosts_file) << "\"";

	CCASSERT(priv_hosts_file.length());

	boost::filesystem::ifstream fs;
	fs.open(priv_hosts_file, fstream::in);
	if(!fs.is_open())
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " RelayService::LoadPrivateHosts error opening private relay hosts file \"" << w2s(priv_hosts_file) << "\"";

		return -1;
	}

	vector<string> hosts;

	while (!g_shutdown)
	{
		string line;

		fs >> line;

		if (fs.fail() && !fs.eof())
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " RelayService::LoadPrivateHosts error reading private relay hosts file \"" << w2s(priv_hosts_file) << "\"";

			return -1;
		}

		boost::trim(line);

		if (line.length())
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

	auto ticks = ccticksnz();

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
		uint32_t when = 0;

		auto next = GetNextPrivateConnectPeer(when);

		auto delay = ccticks_elapsed(ccticks(), when);

		if (next < 0)
			delay = 10*CCTICKS_PER_SEC;

		//delay = CCTICKS_PER_SEC;	// for testing

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
		connection = m_service.GetServer(0).Connect(host, g_params.torproxy_port, use_tor);
	else
	{
		unsigned port = 0;
		auto colon = host.rfind(':');
		if (colon < host.length())
		{
			port = atoi(&host[colon + 1]);
			host.erase(colon);
		}
		if (!port)
			port = g_params.base_port + PRIVRELAY_PORT;

		connection = m_service.GetServer(0).Connect(host, port, use_tor);
	}

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
	if (g_shutdown)
		return;

	if (m_connect_error_count[peer] < 5*60/20)
		++m_connect_error_count[peer];

	unsigned delay = 5 * m_connect_error_count[peer];

	//delay = 10;	// for testing

	m_connect_time[peer] = ccticks() + delay * CCTICKS_PER_SEC;
	if (!m_connect_time[peer])
		m_connect_time[peer] = 1;

	if (TRACE_RELAY) BOOST_LOG_TRIVIAL(debug) << Name() << " RelayService::PrivateConnectReschedule scheduling connection to peer " << peer << " in " << delay << " seconds";
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

void RelayService::StartShutdown()
{
	m_service.StartShutdown();
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

	BOOST_LOG_TRIVIAL(info) << "RelayThread::ThreadProc start " << (uintptr_t)this << " dbconn " << (uintptr_t)relay_dbconn;

	threadproc();

	BOOST_LOG_TRIVIAL(info) << "RelayThread::ThreadProc end " << (uintptr_t)this << " dbconn " << (uintptr_t)relay_dbconn;

	delete relay_dbconn;
}
