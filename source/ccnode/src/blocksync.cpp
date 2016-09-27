/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * blocksync.cpp
*/

#include "CCdef.h"
#include "blocksync.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "processblock.hpp"
#include "transact.hpp"
#include "hostdir.hpp"
#include "expire.hpp"
#include "dbconn.hpp"
#include "util.h"

#include <CCobjects.hpp>

#include <transaction.h>
#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#include <boost/bind.hpp>

#include <utility>

#define TRACE_BLOCKSYNC		(g_params.trace_block_sync)

#define BLOCKSYNC_TIMEOUT			20
#define BLOCKSYNC_BYTES_PER_SEC		500

#define BLOCKSYNC_LOST_SECS			120
#define BLOCKSYNC_FINISH_CONNS		5

#define BLOCKSYNC_NLEVELS_PER_REQ	10

thread_local DbConn *blocksync_dbconn;

void BlockSyncConnection::StartConnection()
{
	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::StartConnection";

	req_msg.entry.nlevels = 0;

	SendReq();
}

void BlockSyncConnection::SendReq()
{
	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::SendReq";

	if (req_msg.entry.nlevels)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::SendReq unexpected req_msg.nlevels " << req_msg.entry.nlevels;

		return Stop();
	}

	req_msg.entry = g_blocksync_client.m_sync_list.GetNextEntry();

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::SendReq requesting level " << req_msg.entry.level << " nlevels " << req_msg.entry.nlevels;

	WriteAsync("BlockSyncConnection::SendReq", boost::asio::buffer(&req_msg, sizeof(req_msg)),
			boost::bind(&BlockSyncConnection::HandleSendMsgWrite, this, boost::asio::placeholders::error, AutoCount(this)));

	SetTimer(BLOCKSYNC_TIMEOUT);
}

void BlockSyncConnection::HandleSendMsgWrite(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	m_write_in_progress.clear();

	CancelTimer();

	bool sim_err = ((TEST_RANDOM_WRITE_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleSendMsgWrite simulating write error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleSendMsgWrite after error " << e << " " << e.message();

		return Stop();
	}

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleSendMsgWrite ok";

	StartRead();

	SetTimer(BLOCKSYNC_TIMEOUT);
}

void BlockSyncConnection::HandleReadComplete()
{
	if (m_nred < CC_MSG_HEADER_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleReadComplete error short read " << m_nred;

		return Stop();
	}

	unsigned size = *(uint32_t*)m_pread;
	unsigned tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleReadComplete read " << m_nred << " bytes msg size " << size << " tag " << tag;

	if (size < CC_MSG_HEADER_SIZE || size > CC_BLOCK_MAX_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleReadComplete error invalid msg size " << size;

		return Stop();
	}

	if (tag == CC_RESULT_NO_LEVEL)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleReadComplete received CC_RESULT_NO_LEVEL";

		g_blocksync_client.IncFinishedConns();

		return Stop();
	}

	if (tag != CC_TAG_BLOCK)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleReadComplete error unrecognized msg tag " << tag;

		return Stop();
	}

	SmartBuf smartobj;

	CCASSERT(CC_MSG_HEADER_SIZE == sizeof(CCObject::Header));

	smartobj = SmartBuf(size + sizeof(CCObject::Preamble));
	if (!smartobj)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleReadComplete smartobj failed";

		return;
	}

	auto obj = (CCObject*)smartobj.data();

	memcpy(obj->ObjPtr(), m_pread, m_nred);

	m_pread = obj->ObjPtr();

	m_maxread = size;

	if (m_maxread > m_nred)
	{
		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleReadComplete queueing read size " << m_maxread - m_nred;

		ReadAsync("BlockSyncConnection::HandleReadComplete", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(m_maxread - m_nred),
				boost::bind(&BlockSyncConnection::HandleObjReadComplete, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, smartobj, AutoCount(this)));

		SetTimer(BLOCKSYNC_TIMEOUT + (m_maxread - m_nred) / BLOCKSYNC_BYTES_PER_SEC);
	}
	else
	{
		HandleObjReadComplete(boost::system::error_code(), 0, smartobj, AutoCount());	// don't need to increment op count
	}
}

void BlockSyncConnection::HandleObjReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter)
{
	CancelTimer();

	m_nred += bytes_transferred;

	bool sim_err = ((TEST_RANDOM_READ_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete simulating read error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

		return Stop();
	}

	if (m_nred != m_maxread)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error short read " << m_nred;

		return Stop();
	}

	auto msgsize = *(uint32_t*)m_pread;

	if (msgsize != m_nred)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error size mismatch msgsize " << msgsize << " != m_nred " << m_nred;

		return Stop();
	}

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete read " << m_nred << " bytes msg size " << msgsize;

	CCASSERT(msgsize >= CC_MSG_HEADER_SIZE);

	ccoid_t *prior_oid = NULL;
	int64_t level = 0;

	auto obj = (CCObject*)smartobj.data();

	CCASSERT(m_pread == obj->ObjPtr());

	if (!obj->IsValid())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error object IsValid false";

		return Stop();
	}

	auto block = (Block*)obj;
	auto wire = block->WireData();

	if (block->BodySize() < sizeof(BlockWireHeader))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error CC_TAG_BLOCK object too small size " << block->BodySize();

		return Stop();
	}

	if (req_msg.entry.level != wire->level)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error requested block level " << req_msg.entry.level << "; received block level " << wire->level;

		return Stop();
	}

	if (!req_msg.entry.nlevels)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error unexpected object";

		return Stop();
	}

	auto auxp = block->SetupAuxBuf(smartobj);
	if (!auxp)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error SetupAuxBuf failed";

		return Stop();
	}

	block->SetOrVerifyOid(true);

	int64_t priority = 0;

	prior_oid = &wire->prior_oid;
	level = wire->level;

	BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleObjReadComplete received obj bufp " << (uintptr_t)smartobj.BasePtr() << " tag " << obj->ObjTag() << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), sizeof(ccoid_t));

	blocksync_dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, prior_oid, level, PROCESS_Q_STATUS_PENDING, priority, m_conn_index, m_use_count.load());

	++req_msg.entry.level;
	--req_msg.entry.nlevels;

	if (req_msg.entry.nlevels)
	{
		StartRead();

		SetTimer(BLOCKSYNC_TIMEOUT);
	}
	else
		SendReq();
}

bool BlockSyncConnection::SetTimer(unsigned sec)
{
	//if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::SetTimer " << sec;

	auto op_counter = AutoCount();
	return AsyncTimerWait("BlockSyncConnection::SetTimer", sec*1000, boost::bind(&BlockSyncConnection::HandleTimeout, this, boost::asio::placeholders::error, op_counter), op_counter);
}

void BlockSyncConnection::HandleTimeout(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (e == boost::asio::error::operation_aborted)
	{
		//if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleTimeout " << uintptr_t(this) << " e = " << e << " " << e.message();

		return;
	}

	if (g_shutdown)
		return;

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::HandleTimeout " << uintptr_t(this) << " e = " << e << " " << e.message();

	Stop();
}

void BlockSyncConnection::FinishConnection()
{
	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " BlockSyncConnection::FinishConnection";

	if (req_msg.entry.nlevels)
	{
		g_blocksync_client.m_sync_list.RequeueEntry(req_msg.entry);

		req_msg.entry.nlevels = 0;
	}
}

void BlockSyncClient::Start()
{
	if (!enabled)
		return;

	g_hostdir.Init();

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient port " << port;

	// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
	CCServer::ConnectionFactoryInstantiation<BlockSyncConnection> connfac(CC_MAX_MSG_SIZE + 2, 0, -1, -1, CC_MSG_HEADER_SIZE, 1, 1);
	CCThreadFactoryInstantiation<BlockSyncThread> threadfac;

	unsigned maxconns = (unsigned)(max_inconns + max_outconns);
	unsigned nthreads = maxconns * threads_per_conn;

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	m_service.Start(boost::asio::ip::tcp::endpoint(address, port),
			nthreads, maxconns, max_inconns, 0, connfac, threadfac);

	if (!m_service.GetNServers())
		return;

	BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient creating thread for ConnMonitorProc this = " << this;
	thread worker(&BlockSyncClient::ConnMonitorProc, this);
	m_conn_monitor_thread.swap(worker);
}

void BlockSyncClient::ConnMonitorProc()
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient::ConnMonitorProc(" << this << ") started";

	ccsleep(4);

	while (!g_shutdown)
	{
		DoSync();

		ccsleep(BLOCKSYNC_LOST_SECS);	// wait at least this long before rechecking sync

		// wait until out-of-sync

		while (!g_shutdown)
		{
			if (ccticks_elapsed(g_processblock.GetLastBlockTicks(), ccticks()) > BLOCKSYNC_LOST_SECS * CCTICKS_PER_SEC)
				break;

			ccsleep(10);
		}
	}

	BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient::ConnMonitorProc(" << this << ") ended";
}

uint64_t BlockSyncList::Init()
{
	lock_guard<FastSpinLock> lock(m_lock);

	m_list.clear();
	m_next_level = g_blockchain.GetLastIndelibleLevel() + 1;

	return m_next_level;
}

class BlockSyncEntry BlockSyncList::GetNextEntry()
{
	lock_guard<FastSpinLock> lock(m_lock);

	if (!m_list.empty())
	{
		auto rv = m_list.front();
		m_list.pop_front();

		return rv;
	}

	//--m_next_level;	// for testing

	auto rv = BlockSyncEntry(m_next_level, BLOCKSYNC_NLEVELS_PER_REQ);
	m_next_level += BLOCKSYNC_NLEVELS_PER_REQ;

	return rv;
}

void BlockSyncList::RequeueEntry(class BlockSyncEntry& entry)
{
	if (entry.nlevels)
	{
		lock_guard<FastSpinLock> lock(m_lock);

		m_list.push_back(entry);
	}
}

void BlockSyncClient::DoSync()
{
	m_conns_finished.store(0);

	auto next_level = m_sync_list.Init();

	BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::DoSync starting at level " << next_level;

	g_expire.ChangeExpireAge(0, 2 * CCTICKS_PER_SEC);

	unsigned si = 0;

	while (!g_shutdown)
	{
		unsigned outcount = m_service.GetServer(si).GetConnectionManager().GetOutgoingConnectionCount();

		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient::DoSync currently " << outcount << " outgoing connections";

		if ((int)outcount < max_outconns)
			ConnectOutgoing();

		ccsleep(4);

		auto finished = m_conns_finished.load();

		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient::DoSync currently " << finished << " connections finished";

		if (finished >= BLOCKSYNC_FINISH_CONNS)
			break;
	}

	while (!g_shutdown)
	{
		unsigned outcount = m_service.GetServer(si).GetConnectionManager().GetOutgoingConnectionCount();

		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient::DoSync finishing sync currently " << outcount << " outgoing connections";

		if (!outcount)
			break;

		ccsleep(10);
	}

	ccsleep(20);						// pause to give the recently sync'ed blocks time to expire

	g_expire.ChangeExpireAge(0, -1);	// reset to normal value
}

void BlockSyncClient::ConnectOutgoing()
{
	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::ConnectOutgoing calling GetHostName()";

	auto peer = g_hostdir.GetHostName(HostDir::Blockserve);

	if (!peer.size())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::ConnectOutgoing no blockchain servers found";

		return;
	}

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::ConnectOutgoing connecting to " << peer;

	m_service.GetServer(0).ConnectThruTor(peer, g_params.torproxy_port);
}

void BlockSyncClient::WaitForShutdown()
{
	if (m_conn_monitor_thread.joinable())
		m_conn_monitor_thread.join();

	if (!enabled)
		return;

	m_service.WaitForShutdown();
}

void BlockSyncThread::ThreadProc(boost::function<void()> threadproc)
{
	blocksync_dbconn = new DbConn;

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << "BlockSyncThread::ThreadProc start " << (uintptr_t)this << " dbconn " << (uintptr_t)blocksync_dbconn;

	threadproc();

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << "BlockSyncThread::ThreadProc end " << (uintptr_t)this << " dbconn " << (uintptr_t)blocksync_dbconn;

	delete blocksync_dbconn;
}
