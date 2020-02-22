/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * blockserve.cpp
*/

#include "ccnode.h"
#include "blockserve.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "processblock.hpp"
#include "transact.hpp"
#include "hostdir.hpp"
#include "dbconn.hpp"

#include <CCobjects.hpp>

#include <transaction.h>
#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#define TRACE_BLOCKSERVE		(g_params.trace_block_serve)

#define BLOCKSERVE_DIR_REFRESH	(30*60)
//#define BLOCKSERVE_DIR_REFRESH	10		// for testing

#define BLOCKSERVE_TIMEOUT			15
#define BLOCKSERVE_BYTES_PER_SEC	500

#define BLOCKSERVE_MSG_SIZE		(CC_MSG_HEADER_SIZE + 8 + 2)	// incoming size: level + nblocks

#pragma pack(push, 1)

static const uint32_t No_Level_Reply[2] =				{CC_MSG_HEADER_SIZE, CC_RESULT_NO_LEVEL};

#pragma pack(pop)

thread_local DbConn *blockserve_dbconn;

void BlockServeConnection::StartConnection()
{
	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockServeConnection::StartConnection";

	m_conn_state = CONN_CONNECTED;

	m_nreqlevels.store(0);

	if (SetTimer(BLOCKSERVE_TIMEOUT))
		return;

	StartRead();
}

void BlockServeConnection::HandleReadComplete()
{
	if (CancelTimer())
		return;

	if (m_nred != BLOCKSERVE_MSG_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleReadComplete error wrong read size " << m_nred;

		return Stop();
	}

	unsigned size = *(uint32_t*)m_pread;
	unsigned tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleReadComplete read " << m_nred << " bytes msg size " << size << " tag " << tag;

	if (size != BLOCKSERVE_MSG_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleReadComplete error wrong msg size " << size;

		return Stop();
	}

	if (tag != CC_CMD_SEND_LEVELS)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleReadComplete error wrong tag " << tag;

		return Stop();
	}

	uint64_t reqlevel = *(uint64_t*)(m_pread + 8);
	uint16_t reqlevels = *(uint16_t*)(m_pread + 8 + 8);

	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleReadComplete reqlevel " << reqlevel << " reqlevels " << reqlevels;

	if (!reqlevel)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleReadComplete error reqlevel " << reqlevel;

		return Stop();
	}

	if (!reqlevels)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleReadComplete error reqlevels " << reqlevels;

		return Stop();
	}

	if (m_nreqlevels.load())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleReadComplete error busy m_nreqlevels " << m_nreqlevels.load();

		return Stop();
	}

	//--reqlevel;	// for testing

	m_reqlevel.store(reqlevel);
	m_nreqlevels.store(reqlevels);

	DoSend();
}

void BlockServeConnection::DoSend()
{
	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockServeConnection::DoSend m_reqlevel " << m_reqlevel.load() << " m_nreqlevels " << m_nreqlevels.load();

	if (!m_nreqlevels.fetch_sub(1))
	{
		// done sending, so queue another read

		m_nreqlevels.store(0);

		StartRead();

		if (SetTimer(BLOCKSERVE_TIMEOUT))
			return;

		return;
	}

	uint64_t level = m_reqlevel.fetch_add(1);
	uint64_t last_indelible_level;

	auto rc = blockserve_dbconn->BlockchainSelectMax(last_indelible_level);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " BlockServeConnection::DoSend error BlockchainSelectMax failed";

		return Stop();
	}

	if (level > last_indelible_level)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::DoSend level " << level << " > last_indelible_level " << last_indelible_level;

		WriteAsync("BlockServeConnection::DoSend", boost::asio::buffer(No_Level_Reply, sizeof(No_Level_Reply)),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
	}

	SmartBuf smartobj;

	blockserve_dbconn->BlockchainSelect(level, &smartobj);
	if (!smartobj)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " BlockServeConnection::DoSend BlockchainSelect failed level " << level;

		return Stop();
	}

	auto obj = (CCObject*)smartobj.data();
	CCASSERT(obj);

	auto size = obj->ObjSize();

	//size = 20;	// for testing

	if (size < CC_MSG_HEADER_SIZE || size > CC_BLOCK_MAX_SIZE)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " BlockServeConnection::DoSend object invalid size " << size;

		return Stop();
	}

	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockServeConnection::DoSend level " << level << " size " << obj->ObjSize() << " tag " << obj->ObjTag();

	WriteAsync("BlockServeConnection::DoSend", boost::asio::buffer(obj->ObjPtr(), size),
			boost::bind(&BlockServeConnection::HandleBlockWrite, this, boost::asio::placeholders::error, smartobj, AutoCount(this)));

	if (SetTimer(BLOCKSERVE_TIMEOUT + size / BLOCKSERVE_BYTES_PER_SEC))
		return;
}

void BlockServeConnection::HandleBlockWrite(const boost::system::error_code& e, SmartBuf smartobj, AutoCount pending_op_counter)
{
	m_write_in_progress.clear();

	smartobj.ClearRef();	// we're done with this, so might as well free it now

	if (CheckOpCount(pending_op_counter))
		return;

	if (CancelTimer())
		return;

	bool sim_err = RandTest(RTEST_WRITE_ERRORS);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleBlockWrite simulating write error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleBlockWrite after error " << e << " " << e.message();

		return Stop();
	}

	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockServeConnection::HandleBlockWrite ok";

	DoSend();
}

void BlockService::Start()
{
	if (!enabled)
		return;

	g_hostdir.Init();

	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " BlockService port " << port;

	// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
	CCServer::ConnectionFactoryInstantiation<BlockServeConnection> connfac(BLOCKSERVE_MSG_SIZE + 2, 0, -1, -1, BLOCKSERVE_MSG_SIZE, 0, 0);
	CCThreadFactoryInstantiation<BlockServeThread> threadfac;

	unsigned maxconns = (unsigned)(max_inconns + max_outconns);
	unsigned nthreads = maxconns * threads_per_conn;

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	m_service.Start(boost::asio::ip::tcp::endpoint(address, port),
			nthreads, maxconns, max_inconns, 0, connfac, threadfac);

	// ConnMonitorProc runs even when the server isn't running

	BOOST_LOG_TRIVIAL(trace) << Name() << " BlockService creating thread for ConnMonitorProc this = " << this;
	thread worker(&BlockService::ConnMonitorProc, this);
	m_conn_monitor_thread.swap(worker);
}

void BlockService::ConnMonitorProc()
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " BlockService::ConnMonitorProc(" << this << ") started";

	ccsleep(10);

	while (!g_shutdown)
	{
		if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(info) << Name() << " BlockService::ConnMonitorProc refreshing peer directory entry...";

		g_hostdir.GetHostName((HostDir::HostType)(-1));

		if (RTEST_READ_ERRORS || RTEST_WRITE_ERRORS)
		{
			for (unsigned i = 0; i < 10; ++i)	// make sure name gets uploaded even if there is a simulated error
				g_hostdir.GetHostName((HostDir::HostType)(-1));
		}

		ccsleep(BLOCKSERVE_DIR_REFRESH);
	}

	BOOST_LOG_TRIVIAL(trace) << Name() << " BlockService::ConnMonitorProc(" << this << ") ended";
}

void BlockService::StartShutdown()
{
	m_service.StartShutdown();
}

void BlockService::WaitForShutdown()
{
	if (m_conn_monitor_thread.joinable())
		m_conn_monitor_thread.join();

	if (!enabled)
		return;

	m_service.WaitForShutdown();
}

void BlockServeThread::ThreadProc(boost::function<void()> threadproc)
{
	blockserve_dbconn = new DbConn;

	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << "BlockServeThread::ThreadProc start " << (uintptr_t)this << " dbconn " << (uintptr_t)blockserve_dbconn;

	threadproc();

	if (TRACE_BLOCKSERVE) BOOST_LOG_TRIVIAL(trace) << "BlockServeThread::ThreadProc end " << (uintptr_t)this << " dbconn " << (uintptr_t)blockserve_dbconn;

	delete blockserve_dbconn;
}
