/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * lpcserve.cpp
*/

#include "ccwallet.h"
#include "lpcserve.hpp"

#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#define TRACE_LPCSERVE	(g_params.trace_lpcserve)

void LpcService::Start()
{
	if (!enabled)
		return;

	if (TRACE_LPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " LpcService port " << port;

	CCThreadFactoryInstantiation<LpcThread> threadfac;

	unsigned maxconns = (unsigned)(max_inconns + max_outconns);
	unsigned nthreads = maxconns * threads_per_conn;

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	m_service.Start(boost::asio::ip::tcp::endpoint(address, port),
			nthreads, maxconns, max_inconns, 0, TxQuery::txconnfac, threadfac);
}

TxQuery* LpcService::GetConnection(bool autofree)
{
	auto pconn = (TxQuery*)m_service.GetServer(0).GetConnectionManager().GetFreeConnection();

	if (pconn)
		pconn->m_autofree = autofree;

	return pconn;
}

void LpcService::StartShutdown()
{
	m_service.StartShutdown();
}

void LpcService::WaitForShutdown()
{
	m_service.WaitForShutdown();
}

void LpcThread::ThreadProc(boost::function<void()> threadproc)
{
	if (TRACE_LPCSERVE) BOOST_LOG_TRIVIAL(trace) << "LpcThread::ThreadProc start " << (uintptr_t)this;

	threadproc();

	if (TRACE_LPCSERVE) BOOST_LOG_TRIVIAL(trace) << "LpcThread::ThreadProc end " << (uintptr_t)this;
}
