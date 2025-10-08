/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * polling.cpp
*/

#include "ccwallet.h"
#include "polling.hpp"
#include "lpcserve.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "exchange.hpp"
#include "walletdb.hpp"

#include <xmatch.hpp>
#include <BlockChainStatus.hpp>
#include <dblog.h>
#include <SpinLock.hpp>

static FastSpinLock lastblocktime_lock(__FILE__, __LINE__);
static atomic<uint64_t> lastblocktime;
static atomic<uint64_t> lastblocktime_update_time;

//#define TEST_NO_POLL_DELAY	1

#ifndef TEST_NO_POLL_DELAY
#define TEST_NO_POLL_DELAY	0	// don't test
#endif

#if TEST_NO_POLL_DELAY
#define CONSERVATIVE_LASTBLOCKTIME_DELAY	0
#else
#define CONSERVATIVE_LASTBLOCKTIME_DELAY	(2*60)
#endif

#define TRACE_POLLING	(g_params.trace_polling)

uint64_t Polling::EstimatedBlocktime(uint64_t checktime, uint64_t *conservative_lastblocktime)
{
	lock_guard<FastSpinLock> lock(lastblocktime_lock);

	uint64_t estimated_lastblocktime;

	if (!lastblocktime_update_time)
	{
		estimated_lastblocktime = checktime;
		if (conservative_lastblocktime)
			*conservative_lastblocktime = checktime - CONSERVATIVE_LASTBLOCKTIME_DELAY;
	}
	else
	{
		auto elapsed = max((int64_t)0, (int64_t)(checktime - lastblocktime_update_time));
		estimated_lastblocktime = lastblocktime + elapsed;
		if (conservative_lastblocktime)
		{
			elapsed = max((int64_t)0, (int64_t)(checktime - lastblocktime_update_time) - CONSERVATIVE_LASTBLOCKTIME_DELAY);
			*conservative_lastblocktime = lastblocktime + elapsed;
		}
	}

	return estimated_lastblocktime;
}

void Polling::StartPolling(unsigned nthreads)
{
	BOOST_LOG_TRIVIAL(info) << "Polling::StartPolling nthreads " << nthreads;

	m_pthreads.clear();
	m_pthreads.reserve(m_pthreads.size() + nthreads);

	for (unsigned i = 0; i < nthreads; ++i)
	{
		auto t = new PollThread();
		t->StartPollThread();
		m_pthreads.emplace_back(t);
	}

	BOOST_LOG_TRIVIAL(debug) << "Polling::StartPolling done";
}

void Polling::StartShutdown()
{
	BOOST_LOG_TRIVIAL(info) << "Polling::StartShutdown";

	for (auto t = m_pthreads.rbegin(); t != m_pthreads.rend(); ++t)
		(*t)->StartShutdown();
}

void Polling::WaitForShutdown()
{
	BOOST_LOG_TRIVIAL(info) << "Polling::WaitForShutdown";

	while (m_pthreads.size())
	{
		m_pthreads.back()->WaitForShutdown();
		delete m_pthreads.back();
		m_pthreads.pop_back();
	}

	BOOST_LOG_TRIVIAL(debug) << "Polling::WaitForShutdown done";
}

void PollThread::StartPollThread()
{
	//if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::StartPollThread";

	m_dbconn = new DbConn;
	CCASSERT(m_dbconn);

	m_txquery = g_lpc_service.GetConnection(false);
	CCASSERT(m_txquery);

	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::StartPollThread starting thread with m_dbconn " << (uintptr_t)m_dbconn << " m_txquery " << (uintptr_t)m_txquery;

	m_thread = new thread(&PollThread::ThreadProc, this);
	CCASSERT(m_thread);
}

void PollThread::StartShutdown()
{
	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::StartShutdown";

	if (m_txquery)
		m_txquery->Stop();
}

void PollThread::WaitForShutdown()
{
	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::WaitForShutdown";

	if (m_thread)
	{
		m_thread->join();
		delete m_thread;
		m_thread = NULL;
	}

	if (m_txquery)
	{
		m_txquery->Stop();
		m_txquery->WaitForStopped();
		m_txquery->FreeConnection();
		m_txquery = NULL;
	}

	if (m_dbconn)
	{
		delete m_dbconn;
		m_dbconn = NULL;
	}

	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::WaitForShutdown done";
}

static mutex poll_mutex;
static atomic<uint64_t> last_empty_time;

void PollThread::ThreadProc()
{
	//cc_malloc_logging_not_this_thread(true);

	BOOST_LOG_TRIVIAL(info) << "PollThread::ThreadProc m_dbconn " << (uintptr_t)m_dbconn << " m_txquery " << (uintptr_t)m_txquery;

	time_t t0 = 0;

	while (!g_shutdown)
	{
		timeval t1;

		while (!g_shutdown)
		{
			gettimeofday(&t1, NULL);
			t1.tv_sec += g_clock_offset;

			//if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::ThreadProc now " << t1.tv_sec << "." << t1.tv_usec;

			if (t1.tv_sec != t0)
				break;

			// wait for the time to tick to the next second

			int millisec = 1200 - t1.tv_usec/1000;

			if (millisec > 0)
				wait_for_shutdown(millisec);
		}

		t0 = t1.tv_sec;

		//t0 = INT64_MAX;	// for testing -- allow polling to free run

		unsigned poll_count = 0;

		while (!g_shutdown)
		{
			auto rc = DoPoll(t0);
			if (rc < 0)
				break;
			else if (!rc)
				++poll_count;
		}

		if (TRACE_POLLING  && poll_count > 1)  BOOST_LOG_TRIVIAL(info) << "PollThread::DoPoll polled " << poll_count << " addresses";
		else if (TRACE_POLLING && poll_count) BOOST_LOG_TRIVIAL(debug) << "PollThread::DoPoll polled " << poll_count << " addresses";
	}

	BOOST_LOG_TRIVIAL(info) << "PollThread::ThreadProc done";
}

/* returns:
	-1 = nothing polled
	0 = one address polled
	1 = nothing polled but retry
*/

int PollThread::DoPoll(uint64_t checktime)
{
	auto dbconn = m_dbconn;

	bool poll_xreq = false;
	Xmatchreq xreq;
	Transaction tx;

	bool poll_secret = false;
	Secret secret;

	bool poll_xmatch = false;
	Xmatch xmatch;

	bool full_round = false;

	{
		lock_guard<mutex> lock(poll_mutex);

		if (g_shutdown)
			return -1;

		if (checktime <= last_empty_time)
			return -1;

		if (TRACE_POLLING) BOOST_LOG_TRIVIAL(debug) << "PollThread::DoPoll m_dbconn " << (uintptr_t)m_dbconn << " m_txquery " << (uintptr_t)m_txquery << " checktime " << checktime << " last_empty_time " << last_empty_time;

		auto rc = dbconn->BeginWrite();
		if (rc)
		{
			dbconn->DoDbFinishTx(-1);

			return -1;
		}

		Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

		while (true) // break to exit
		{
			static unsigned round;

			round = (round + 1) & 31;	// make sure all queues get attention

			if (!(round < 3))
				full_round = true;		// only used to log # addresses polled in this pass

			// check exchange first since matches can't wait
			rc = (round < 3 ? 1 : dbconn->ExchangeRequestSelectNextPoll(checktime, xreq, &tx));
			if (!rc)
			{
				rc = ExchangeRequest::UpdatePollTime(dbconn, xreq.id, false);
				if (rc) return -1;

				poll_xreq = true;

				break;
			}

			rc = (round < 1 ? 1 : dbconn->SecretSelectNextPoll(checktime, secret));
			if (!rc)
			{
				CCASSERT(secret.TypeIsAddress());
				CCASSERT(secret.next_poll <= checktime);

				secret.UpdatePollTime(checktime, true);

				rc = dbconn->SecretInsert(secret);
				if (rc) return -1;

				poll_secret = true;

				break;
			}

			uint64_t conservative_lastblocktime;
			auto estimated_lastblocktime = Polling::EstimatedBlocktime(checktime, &conservative_lastblocktime);

			rc = dbconn->ExchangeMatchSelectNextPoll(conservative_lastblocktime, xmatch);
			if (!rc)
			{
				ExchangeMatch::UpdatePollTime(xmatch, estimated_lastblocktime);

				rc = dbconn->ExchangeMatchInsert(xmatch);
				if (rc) return -1;

				poll_xmatch = true;

				break;
			}

			last_empty_time = checktime;

			break;
		}

		// commit db writes

		if (g_shutdown)
			return -1;

		rc = dbconn->Commit();
		if (rc)
		{
			BOOST_LOG_TRIVIAL(fatal) << "Secret::DoPoll error committing db transaction";

			return -1;
		}

		dbconn->DoDbFinishTx();

		finally.Clear();
	}

	unsigned jitter = rand() % 1000;	// for privacy, pause a random time before checking

	wait_for_shutdown(jitter);

	if (g_shutdown)
		return -1;

	if (poll_xreq)
	{
		BlockChainStatus blockchain_status;

		ExchangeRequest::PollXmatchreq(dbconn, *m_txquery, xreq, tx, blockchain_status);

		if (blockchain_status.last_indelible_timestamp)
		{
			//BOOST_LOG_TRIVIAL(info) << "ExchangeRequest::PollXmatchreq returned lastblocktime " << blockchain_status.last_indelible_timestamp;

			lock_guard<FastSpinLock> lock(lastblocktime_lock);

			lastblocktime = blockchain_status.last_indelible_timestamp;
			lastblocktime_update_time = checktime;
		}

		return 0;
	}

	if (poll_secret)
	{
		secret.PollAddress(dbconn, *m_txquery, false);

		return 0;
	}

	if (poll_xmatch)
	{
		BlockChainStatus blockchain_status;

		ExchangeMatch::PollXmatch(dbconn, *m_txquery, xmatch, blockchain_status);

		if (blockchain_status.last_indelible_timestamp)
		{
			//BOOST_LOG_TRIVIAL(info) << "ExchangeRequest::PollXmatch returned lastblocktime " << blockchain_status.last_indelible_timestamp;

			lock_guard<FastSpinLock> lock(lastblocktime_lock);

			lastblocktime = blockchain_status.last_indelible_timestamp;
			lastblocktime_update_time = checktime;
		}

		return 0;
	}

	//Billet::NotifyNewBillet(true);	// for testing--send spurious new billet notification

	return !full_round;
}
