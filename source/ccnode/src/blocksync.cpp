/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * blocksync.cpp
*/

#include "ccnode.h"
#include "blocksync.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "processblock.hpp"
#include "transact.hpp"
#include "hostdir.hpp"
#include "expire.hpp"
#include "dbconn.hpp"

#include <CCobjects.hpp>

#include <transaction.h>
#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#define TRACE_BLOCKSYNC		(g_params.trace_block_sync)

// TODO: add mutex cuzz tests to this file
// TODO: add sim of bad block
// TODO: test with random read and write errors
// TODO: prune slower connections

#define BLOCKSYNC_REQ_TIMEOUT					60
#define BLOCKSYNC_READ_TIMEOUT					15
#define BLOCKSYNC_BYTES_PER_SEC					500

#define BLOCKSYNC_LOST_SECS						420

#define BLOCKSYNC_NLEVELS_PER_REQ				100

#define BLOCKSYNC_MAX_BLOCK_PROCESSING_TIME		30

#pragma pack(push, 1)

static const uint32_t Ping_Req[2] =			{CC_MSG_HEADER_SIZE, CC_CMD_PING};

#pragma pack(pop)

thread_local static DbConn *blocksync_dbconn;

void BlockSyncConnection::StartConnection()
{
	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::StartConnection";

	m_conn_state = CONN_CONNECTED;

	m_cur_req_msg.entry.nlevels = 0;
	m_next_req_msg.entry.nlevels = 0;

	m_validations_pending = 0;

	m_has_requeues = false;
	m_finished = false;

	// start with a ping to make sure the connection is working

	if (SetTimer(BLOCKSYNC_REQ_TIMEOUT))
		return;

	m_read_in_progress.test_and_set();

	WriteAsync("BlockSyncConnection::StartConnection", boost::asio::buffer(Ping_Req, sizeof(Ping_Req)),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void BlockSyncConnection::CheckSendReq(unique_lock<mutex> &lock)
{
	BlockSyncEntry next;

	if (m_read_in_progress.test_and_set())
	{
		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq deferring check until post read";

		return;
	}

	while (!g_shutdown)
	{
		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq requeues " << m_has_requeues << " finished " << m_finished << " current reqs " << m_cur_req_msg.entry.nlevels << " next reqs " << m_next_req_msg.entry.nlevels;

		if (m_has_requeues || m_finished)
		{
			if (m_validations_pending.load() > 0)
			{
				if (SetValidationTimer())
					return;

				m_read_in_progress.clear();

				return;
			}

			return Stop();
		}

		if (m_next_req_msg.entry.nlevels)
		{
			if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq no req needed";

			return StartNextRead();
		}

		// get next level to request, ensuring it's above the last_indelible_level and not past the maximum request span

		next = g_blocksync_client.m_sync_list.GetNextEntry(Name(), m_conn_index);

		if (next.nlevels)
			break;

		if (m_cur_req_msg.entry.nlevels)
		{
			if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq deferring max span req post read";

			return StartNextRead();
		}

		if (m_validations_pending.load() > 0)
		{
			if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq deferring max span req post validate";

			if (SetValidationTimer())
				return;

			m_read_in_progress.clear();

			return;
		}

		lock.unlock();

		if (!g_blocksync_client.UnconnectedCount() && RandTest(5U * g_blocksync_client.max_outconns))
		{
			// randomly stop connection to ensure we rotate peers to get past invalid blocks

			if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq random stop at max span";

			return Stop();
		}

		if (SetTimer(BLOCKSYNC_REQ_TIMEOUT))
			return;

		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq SetTimer " << BLOCKSYNC_REQ_TIMEOUT;

		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq waiting for blockchain to advance at max span";

		ccsleep(5);

		lock.lock();
	}

	m_next_req_msg.entry = next;

	lock.unlock();

	if (SetTimer(BLOCKSYNC_REQ_TIMEOUT))
		return;

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq SetTimer " << BLOCKSYNC_REQ_TIMEOUT;

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::CheckSendReq requesting level " << m_next_req_msg.entry.level << " nlevels " << m_next_req_msg.entry.nlevels;

	WriteAsync("BlockSyncConnection::CheckSendReq", boost::asio::buffer(&m_next_req_msg, sizeof(m_next_req_msg)),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void BlockSyncConnection::StartNextRead()
{
	unsigned sec = BLOCKSYNC_READ_TIMEOUT;

	if (g_blocksync_client.UnconnectedCount())
		sec *= 2;

	if (SetTimer(sec))
		return;

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::StartNextRead SetTimer " << sec;

	return StartRead();
}

void BlockSyncConnection::HandleReadComplete()
{
	if (m_nred < CC_MSG_HEADER_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete error short read " << m_nred;

		return Stop();
	}

	unsigned size = *(uint32_t*)m_pread;
	unsigned tag = *(uint32_t*)(m_pread + 4);

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete read " << m_nred << " bytes msg size " << size << " tag " << hex << tag << dec;

	if (size < CC_MSG_HEADER_SIZE || size > CC_BLOCK_MAX_SIZE)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete error invalid msg size " << size;

		return Stop();
	}

	if (tag == CC_ACK)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete received CC_ACK";

		unique_lock<mutex> lock(req_lock);

		m_read_in_progress.clear();

		return CheckSendReq(lock);
	}

	if (tag == CC_RESULT_NO_LEVEL)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete received CC_RESULT_NO_LEVEL";

		unique_lock<mutex> lock(req_lock);

		m_finished = true;

		m_read_in_progress.clear();

		return CheckSendReq(lock);
	}

	if (tag != CC_TAG_BLOCK)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete error unrecognized msg tag " << hex << tag << dec;

		return Stop();
	}

	SmartBuf smartobj;

	CCASSERT(CC_MSG_HEADER_SIZE == sizeof(CCObject::Header));

	smartobj = SmartBuf(size + sizeof(CCObject::Preamble));
	if (!smartobj)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete error smartobj failed";

		return Stop();
	}

	auto obj = (CCObject*)smartobj.data();

	memcpy(obj->ObjPtr(), m_pread, m_nred);

	m_pread = (char*)obj->ObjPtr();

	m_maxread = size;

	if (m_nred < m_maxread)
	{
		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete queueing read size " << m_maxread - m_nred;

		unsigned sec = (m_maxread - m_nred) / BLOCKSYNC_BYTES_PER_SEC + 10;

		if (sec < BLOCKSYNC_READ_TIMEOUT)
			sec = BLOCKSYNC_READ_TIMEOUT;

		if (g_blocksync_client.UnconnectedCount())
			sec *= 2;

		if (SetTimer(sec))
			return;

		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleReadComplete SetTimer " << sec;

		ReadAsync("BlockSyncConnection::HandleReadComplete", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(m_maxread - m_nred),
				boost::bind(&BlockSyncConnection::HandleObjReadComplete, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, smartobj, AutoCount(this)));
	}
	else
	{
		HandleObjReadComplete(boost::system::error_code(), 0, smartobj, AutoCount(this));	// don't need to increment op count, but too much effort to chain the AutoCount from the function calling HandleReadComplete
	}
}

void BlockSyncConnection::HandleObjReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	m_nred += bytes_transferred;

	bool sim_err = RandTest(RTEST_READ_ERRORS);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete simulating read error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

		return Stop();
	}

	if (m_nred != m_maxread)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error short read " << m_nred;

		return Stop();
	}

	auto msgsize = *(uint32_t*)m_pread;

	if (msgsize != m_nred)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error size mismatch msgsize " << msgsize << " != m_nred " << m_nred;

		return Stop();
	}

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete read " << m_nred << " bytes msg size " << msgsize;

	CCASSERT(msgsize >= CC_MSG_HEADER_SIZE);

	ccoid_t *prior_oid = NULL;
	int64_t level = 0;

	auto obj = (CCObject*)smartobj.data();

	CCASSERT(m_pread == (char*)obj->ObjPtr());

	if (!obj->IsValid())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error object IsValid false";

		return Stop();
	}

	auto block = (Block*)obj;
	auto wire = block->WireData();

	if (block->BodySize() < sizeof(BlockWireHeader))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error CC_TAG_BLOCK object too small size " << block->BodySize();

		return Stop();
	}

	{
		lock_guard<mutex> lock(req_lock);

		if (!m_cur_req_msg.entry.nlevels)
		{
			m_cur_req_msg.entry = m_next_req_msg.entry;
			m_next_req_msg.entry.nlevels = 0;
		}

		if (!m_cur_req_msg.entry.nlevels)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error unexpected object";

			return Stop();
		}

		if (m_cur_req_msg.entry.level != wire->level.GetValue())
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error requested block level " << m_cur_req_msg.entry.level << "; received block level " << wire->level.GetValue();

			return Stop();
		}
	}

	auto auxp = block->SetupAuxBuf(smartobj);
	if (!auxp)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete error SetupAuxBuf failed";

		return Stop();
	}

	block->SetOrVerifyOid(true);

	prior_oid = &wire->prior_oid;
	level = wire->level.GetValue();

	m_validations_pending.fetch_add(1);	// assume ProcessQEnqueueValidate will succeed, because if so, the callback can happen immediately

	auto use_count = m_use_count.load();

	auto rc = blocksync_dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, prior_oid, level, PROCESS_Q_STATUS_PENDING, PROCESS_Q_PRIORITY_BLOCK_HI, false, m_conn_index, use_count);

	BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete enqueue result " << rc << " block level " << level << " validations pending " << m_validations_pending.load() << " obj bufp " << (uintptr_t)smartobj.BasePtr() << " tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

	if (rc < 0)
		return Stop();
	else if (rc)
		HandleValidateDone(level, use_count, 1);
	else
	{
		#if 0
		static atomic<int64_t> last_time;

		auto t1 = unixtime();
		auto dt = t1 - last_time.exchange(t1);

		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleObjReadComplete enqueue block level " << level << " dt " << dt << " validations pending " << m_validations_pending.load() << " obj bufp " << (uintptr_t)smartobj.BasePtr() << " tag " << hex << obj->ObjTag() << dec << " size " << obj->ObjSize() << " oid " << buf2hex(obj->OidPtr(), CC_OID_TRACE_SIZE);

		lock_guard<mutex> lock(g_cerr_lock);
		//check_cerr_newline();
		//cerr << "BlockSync queued for validation level " << level << " time " << t1 << " dt " << dt << endl;

		cerr << ".";
		cerr.flush();
		g_cerr_needs_newline = true;
		#endif
	}

	unique_lock<mutex> lock(req_lock);

	++m_cur_req_msg.entry.level;
	--m_cur_req_msg.entry.nlevels;

	if (!m_cur_req_msg.entry.nlevels)
	{
		m_cur_req_msg.entry = m_next_req_msg.entry;
		m_next_req_msg.entry.nlevels = 0;
	}

	m_read_in_progress.clear();

	CheckSendReq(lock);
}

bool BlockSyncConnection::SetValidationTimer()
{
	auto sec = g_blocksync_client.ConnectedCount() * BLOCKSYNC_NLEVELS_PER_REQ * BLOCKSYNC_MAX_BLOCK_PROCESSING_TIME;

	if (SetTimer(sec))
		return true;

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::SetValidationTimer SetTimer " << sec;

	return false;
}

void BlockSyncConnection::HandleValidateDone(uint64_t level, uint32_t callback_id, int64_t result)
{
	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleValidateDone result " << result << " level " << level << " validations pending " << m_validations_pending.load();

	if (result > 1)
		return;

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	if (callback_id != m_use_count.load())
	{
		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleValidateDone level " << level << " ignoring late or unexpected callback id " << callback_id << " use count " << m_use_count.load();

		return;
	}

	if (result <= PROCESS_RESULT_STOP_THRESHOLD)
	{
		BlockSyncEntry entry(level, 1);

		g_blocksync_client.m_sync_list.RequeueEntry(Name(), m_conn_index, entry);

		m_has_requeues = true;
	}

	unique_lock<mutex> lock(req_lock);

	auto vp = m_validations_pending.fetch_sub(1) - 1;

	while (vp < 0)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::HandleValidateDone m_validations_pending " << vp << " < 0";

		++vp;
		m_validations_pending.fetch_add(1);
	}

	CheckSendReq(lock);
}

void BlockSyncConnection::FinishConnection()
{
	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::FinishConnection requeues " << m_has_requeues << " finished " << m_finished;

	if (m_finished)
	{
		int64_t dt = unixtime() - g_blockchain.GetLastIndelibleTimestamp();

		if (dt > max(g_params.block_future_tolerance, g_expire.GetExpireAge(1)/CCTICKS_PER_SEC - 2*60))
			m_finished = false;

		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::FinishConnection LastIndelibleTimestamp dt " << dt << " block_future_tolerance " << g_params.block_future_tolerance << " ExpireAge " << g_expire.GetExpireAge(1)/CCTICKS_PER_SEC << " finished " << m_finished;
	}

	bool no_requeue = m_finished && g_blocksync_client.IsFinishing();

	lock_guard<mutex> lock(req_lock);

	if (!no_requeue && m_cur_req_msg.entry.nlevels)
	{
		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::FinishConnection requeuing m_cur_req_msg.entry level " << m_cur_req_msg.entry.level << " nlevels " << m_cur_req_msg.entry.nlevels;

		g_blocksync_client.m_sync_list.RequeueEntry(Name(), m_conn_index, m_cur_req_msg.entry);
	}

	if (!no_requeue && m_next_req_msg.entry.nlevels)
	{
		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " BlockSyncConnection::FinishConnection requeuing m_next_req_msg.entry level " << m_next_req_msg.entry.level << " nlevels " << m_next_req_msg.entry.nlevels;

		g_blocksync_client.m_sync_list.RequeueEntry(Name(), m_conn_index, m_next_req_msg.entry);
	}

	if (m_finished && !m_has_requeues)
		g_blocksync_client.SignalFinished();
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

	ccsleep(10);

	while (!g_shutdown)
	{
		DoSync();

		// wait until out-of-sync

		while (!g_shutdown)
		{
			if (ccticks_elapsed(g_processblock.GetLastBlockTicks(), ccticks()) > BLOCKSYNC_LOST_SECS * CCTICKS_PER_SEC)
				break;

			BOOST_LOG_TRIVIAL(debug) << Name() << " BlockSyncClient::ConnMonitorProc sync ok";

			ccsleep(10);
		}
	}

	BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient::ConnMonitorProc(" << this << ") ended";
}

void BlockSyncList::Init(int64_t level)
{
	lock_guard<FastSpinLock> lock(m_block_sync_list_lock);

	m_list.clear();
	m_next_level = level;
}

BlockSyncEntry BlockSyncList::GetNextEntry(const string& name, int conn_index)
{
	BlockSyncEntry entry;

	lock_guard<FastSpinLock> lock(m_block_sync_list_lock);

	auto last_indelible_level = g_blockchain.GetLastIndelibleLevel();

	while (!entry.nlevels && !g_shutdown)
	{
		if (!m_list.empty())
		{
			entry = m_list.front();
			m_list.pop_front();

			//if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << name << " Conn " << conn_index << " BlockSyncList::GetNextEntry last_indelible_level " << last_indelible_level << " entry level " << entry.level << " nlevels " << entry.nlevels;

			while (entry.nlevels && entry.level <= last_indelible_level)
			{
				++entry.level;
				--entry.nlevels;
			}
		}
		else
		{
			//--m_next_level;	// for testing

			auto max_span = 2 * BLOCKSYNC_NLEVELS_PER_REQ * min(max(4, g_blocksync_client.max_outconns + 1), 10);

			auto max_level = last_indelible_level + max_span;

			if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << name << " Conn " << conn_index << " BlockSyncList::GetNextEntry last_indelible_level " << last_indelible_level << " max_span " << max_span << " max_level " << max_level << " m_next_level " << m_next_level;

			if (m_next_level <= last_indelible_level)
				m_next_level = last_indelible_level + 1;

			if (m_next_level > max_level)
				break;

			auto nlevels = BLOCKSYNC_NLEVELS_PER_REQ - (m_next_level + BLOCKSYNC_NLEVELS_PER_REQ - 1) % BLOCKSYNC_NLEVELS_PER_REQ;

			entry = BlockSyncEntry(m_next_level, nlevels);
			m_next_level += nlevels;
		}
	}

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << name << " Conn " << conn_index << " BlockSyncList::GetNextEntry last_indelible_level " << last_indelible_level << " returning level " << entry.level << " nlevels " << entry.nlevels;

	return entry;
}

void BlockSyncList::RequeueEntry(const string& name, int conn_index, const BlockSyncEntry& entry)
{
	if (!entry.nlevels)
		return;

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << name << " Conn " << conn_index << " BlockSyncList::RequeueEntry level " << entry.level << " nlevels " << entry.nlevels;

	lock_guard<FastSpinLock> lock(m_block_sync_list_lock);

	if (entry.level >= m_next_level)
		return;

	m_list.push_back(entry);

	if (m_next_level < entry.level + entry.nlevels)
		m_next_level = entry.level + entry.nlevels;
}

bool BlockSyncList::HasRequeues()
{
	lock_guard<FastSpinLock> lock(m_block_sync_list_lock);

	return !m_list.empty();
}

unsigned BlockSyncClient::ConnectedCount()
{
	unsigned si = 0;

	auto outcount = m_service.GetServer(si).GetConnectionManager().GetOutgoingConnectionCount();

	//if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient::ConnectedCount " << outcount << " of " << max_outconns;

	return outcount;
}

unsigned BlockSyncClient::UnconnectedCount(int outcount)
{
	if (outcount < 0)
		outcount = ConnectedCount();

	auto uncount = (max_outconns > outcount ? max_outconns - outcount : 0);

	//if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(trace) << Name() << " BlockSyncClient::UnconnectedCount " << uncount << " of " << max_outconns;

	return uncount;
}

void BlockSyncClient::SignalFinished()
{
	m_nfinished++;
}

bool BlockSyncClient::IsFinishing()
{
	return (m_nfinished >= min(max(3, (max_outconns + 1)/2), 8));
}

void BlockSyncClient::DoSync()
{
	auto last_indelible_level = g_blockchain.GetLastIndelibleLevel();
	auto last_indelible_time = unixtime();

	BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::DoSync start last_indelible_level " << last_indelible_level;

	m_sync_list.Init(last_indelible_level + 1);

	m_nfinished = 0;

	g_expire.ChangeExpireAge(0, 2 * CCTICKS_PER_SEC);	// while sync'ing, rapidly expire blocks in ValidObjs DB to minimize memory use

	unsigned si = 0;

	while (!g_shutdown)
	{
		auto outcount = m_service.GetServer(si).GetConnectionManager().GetOutgoingConnectionCount();

		auto uncount = UnconnectedCount(outcount);

		auto now = unixtime();

		auto check = g_blockchain.GetLastIndelibleLevel();
		if (check != last_indelible_level)
		{
			last_indelible_level = check;
			last_indelible_time = now;
		}

		int64_t indelible_dt = now - last_indelible_time;

		if (indelible_dt > BLOCKSYNC_LOST_SECS)
		{
			BlockSyncEntry entry(last_indelible_level + 1, BLOCKSYNC_NLEVELS_PER_REQ);

			g_blocksync_client.m_sync_list.RequeueEntry(Name(), 0, entry);

			m_nfinished = 0;
			last_indelible_time = now;
		}

		bool done = IsFinishing() && !m_sync_list.HasRequeues();

		if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(debug) << Name() << " BlockSyncClient::DoSync last_indelible_level " << last_indelible_level << " indelible dt " << indelible_dt << " conns connected " << outcount << " finished " << m_nfinished << " have requeues " << m_sync_list.HasRequeues() << " done " << done;

		if (done && !outcount)
			break;

		if (!done && uncount)
			ConnectOutgoing();

		ccsleep(8);
	}

	BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::DoSync finished";

	ccsleep(20);						// pause to give the recently sync'ed blocks time to expire

	g_expire.ChangeExpireAge(0, -1);	// reset to normal value
}

void BlockSyncClient::ConnectOutgoing()
{
	BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::ConnectOutgoing calling GetHostName()";

	auto peer = g_hostdir.GetHostName(HostDir::Blockserve);

	if (!peer.size())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::ConnectOutgoing no blockchain servers found";

		return;
	}

	if (TRACE_BLOCKSYNC) BOOST_LOG_TRIVIAL(info) << Name() << " BlockSyncClient::ConnectOutgoing connecting to " << peer;

	m_service.GetServer(0).Connect(peer, g_params.torproxy_port, true);
}

void BlockSyncClient::StartShutdown()
{
	m_service.StartShutdown();
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

	BOOST_LOG_TRIVIAL(info) << "BlockSyncThread::ThreadProc start " << (uintptr_t)this << " dbconn " << (uintptr_t)blocksync_dbconn;

	threadproc();

	BOOST_LOG_TRIVIAL(info) << "BlockSyncThread::ThreadProc end " << (uintptr_t)this << " dbconn " << (uintptr_t)blocksync_dbconn;

	delete blocksync_dbconn;
}
