/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * polling.cpp
*/

#include "ccwallet.h"
#include "polling.hpp"
#include "lpcserve.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "walletdb.hpp"

#include <dblog.h>

#define TRACE_POLLING	(g_params.trace_polling)

void Polling::Start(unsigned nthreads)
{
	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(info) << "Polling::Start nthreads " << nthreads;

	m_pthreads.reserve(m_pthreads.size() + nthreads);

	for (unsigned i = 0; i < nthreads; ++i)
	{
		auto t = new PollThread();
		t->Start();
		m_pthreads.emplace_back(t);
	}

	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(debug) << "Polling::Start done";
}

void Polling::StartShutdown()
{
	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(info) << "Polling::StartShutdown";

	for (auto t = m_pthreads.rbegin(); t != m_pthreads.rend(); ++t)
		(*t)->StartShutdown();
}

void Polling::WaitForShutdown()
{
	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(info) << "Polling::WaitForShutdown";

	while (m_pthreads.size())
	{
		m_pthreads.back()->WaitForShutdown();
		m_pthreads.pop_back();
	}

	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(debug) << "Polling::WaitForShutdown done";
}

void PollThread::Start()
{
	//if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::Start";

	m_dbconn = new DbConn;
	CCASSERT(m_dbconn);

	m_txquery = g_lpc_service.GetConnection(false);
	CCASSERT(m_txquery);

	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::Start starting thread with m_dbconn " << (uintptr_t)m_dbconn << " m_txquery " << (uintptr_t)m_txquery;

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
static uint64_t last_empty_time;

void PollThread::ThreadProc()
{
	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::ThreadProc m_dbconn " << (uintptr_t)m_dbconn << " m_txquery " << (uintptr_t)m_txquery;

	time_t t0 = 0;

	while (!g_shutdown)
	{
		timeb t1;

		while (!g_shutdown)
		{
			ftime(&t1);

			//if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::ThreadProc now " << t1.time << "." << t1.millitm;

			if (t1.time != t0)
				break;

			int millisec = 1000 - t1.millitm;

			if (millisec > 0)
				wait_for_shutdown(millisec);
		}

		t0 = t1.time;

		//t0 = INT64_MAX;	// for testing -- allow polling to free run

		unsigned poll_count = 0;

		while (!g_shutdown)
		{
			auto rc = DoPoll(t0);
			if (rc)
				break;

			++poll_count;
		}

		if (poll_count && TRACE_POLLING) BOOST_LOG_TRIVIAL(info) << "PollThread::DoPoll polled " << poll_count << " addresses";
	}

	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "PollThread::ThreadProc done";
}

int PollThread::DoPoll(uint64_t checktime)
{
	auto dbconn = m_dbconn;

	Secret secret;

	{
		lock_guard<mutex> lock(poll_mutex);

		if (g_shutdown)
			return 1;

		if (checktime <= last_empty_time)
			return 1;

		if (TRACE_POLLING) BOOST_LOG_TRIVIAL(debug) << "PollThread::DoPoll m_dbconn " << (uintptr_t)m_dbconn << " m_txquery " << (uintptr_t)m_txquery << " checktime " << checktime << " last_empty_time " << last_empty_time;

		auto rc = dbconn->BeginWrite();
		if (rc)
		{
			dbconn->DoDbFinishTx(-1);

			return -1;
		}

		Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

		rc = dbconn->SecretSelectNextCheck(checktime, secret);
		if (rc)
		{
			last_empty_time = checktime;

			dbconn->DoDbFinishTx(1);

			finally.Clear();

			//Billet::NotifyNewBillet(true);	// for testing--send spurious new billet notification

			return rc;
		}

		CCASSERT(secret.TypeIsAddress());
		CCASSERT(secret.next_check <= checktime);

		secret.UpdatePollingTimes(checktime, true);

		rc = dbconn->SecretInsert(secret);
		if (rc) return -1;

		// commit db writes

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
		return 1;

	secret.PollAddress(dbconn, *m_txquery, false);

	return 0;
}
