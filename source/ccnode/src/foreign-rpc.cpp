/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * foreign-rpc.cpp
*/

#include "ccnode.h"
#include "foreign-rpc.hpp"
#include "witness.hpp"

#include <encode.h>
#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#define TRACE_FORN_CONN		g_params.trace_foreign_conn
#define TRACE_FORN_RPC		g_params.trace_foreign_rpc

#define NON_WITNESS_BUFSIZE		128*1024
//#define NON_WITNESS_BUFSIZE		8*1024		// for testing

//!#define RTEST_CUZZ			32

#ifndef RTEST_CUZZ
#define RTEST_CUZZ				0	// don't test
#endif

#define CRLF	"\x0d\x0a"

int ForeignRpc::TryQuery(unsigned port, const string& query)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::TryQuery conn state " << m_conn_state;

	// send query

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	WaitForStopped();

	if (g_shutdown) return -1;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 3);

	m_pquery = &query;

	auto read_count_start = m_read_count;

	if (m_conn_state == CONN_STOPPED)
	{
		InitNewConnection();

		if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::TryQuery posting query; m_stopping " << m_stopping.load();

		auto rc = Post("ForeignRpc::TryQuery", boost::bind(&Connection::HandleConnectOutgoing, this, LOCALHOST, port, AutoCount(this)));

		if (rc)
		{
			if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::TryQuery post failed; m_stopping " << m_stopping.load();

			Stop();
			return -1;
		}

		if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::TryQuery query posted; m_stopping " << m_stopping.load();
	}
	else
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " ForeignRpc::TryQuery submitting to open connection not yet supported";

		Stop();
		return -1;	// FUTURE: need a way to submit query to already open connection
	}

	WaitForReadComplete(read_count_start);

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::TryQuery read_count_start " << read_count_start << " m_read_count " << m_read_count << " g_shutdown " << g_shutdown;

	Stop();	// !!! for now

	if (g_shutdown) return -1;

	if (RandTest(RTEST_CUZZ)) ccsleep(rand() & 7);

	return 0;
}

int ForeignRpc::SubmitQuery(unsigned port, const string& auth, const string& query, Json::Value *root, const char* *content, bool debug)
{
	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery port " << port << " query " << query;

	int result_code = -1;

	if (root)
		root->clear();

	string request = "POST / HTTP/1.1" CRLF;
	request += "Authorization: Basic " + auth + CRLF;
	//request += "Accept: */*" CRLF;
	//request += "Content-Type: application/x-www-form-urlencoded" CRLF;
	//request += "Content-Type: text/plain" CRLF;
	request += "Content-Length: " + to_string(query.length()) + CRLF;
	request += CRLF + query;

	while (true)	// break on error
	{
		auto rc = TryQuery(port, request);

		if (rc) break;

		m_pread[m_nred] = 0;

		if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery reply start " << m_content_start << " length " << m_content_length;

		if (m_nred < 1 || m_nred <= m_content_start || m_content_length <= 0 || m_nred < m_content_start + m_content_length)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery empty response";

			break;
		}

		if (debug) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery reply " << &m_pread[m_content_start];
		else if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery reply " << &m_pread[m_content_start];
		//if (m_nred > 20000) BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery large reply " << m_nred << " bytes; content: " << &m_pread[m_content_start]; // for debugging

		if (content)
			*content = &m_pread[m_content_start];

		if (!(m_pread[m_content_start] == '{' || (m_pread[m_content_start] == '[' && m_pread[m_content_start+1] == '{')))
		{
			result_code = 1;

			break;
		}

		auto content_end = m_content_start + m_content_length;

		while (content_end)
		{
			auto c = m_pread[content_end-1];

			if (c == ' ' || c == '\r' || c == '\n' || c == '\0')
				--content_end;
			else
				break;
		}

		if (root)
		{
			if (content_end < 2 || !(m_pread[content_end-1] == '}' || (m_pread[content_end-2] == '}' && m_pread[content_end-1] == ']')))
			{
				BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery incomplete response content_end " << content_end << " last char " << (int)m_pread[content_end >= 1 ? content_end-1 : 0];

				break;
			}

			Json::CharReaderBuilder builder;
			Json::CharReaderBuilder::strictMode(&builder.settings_);

			auto reader = builder.newCharReader();

			bool rc;

			try
			{
				rc = reader->parse(&m_pread[m_content_start], &m_pread[content_end], root, NULL);
			}
			catch (...)
			{
				rc = false;
			}

			delete reader;

			if (!rc)
			{
				BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery json parse error " << &m_pread[m_content_start];

				root->clear();

				break;
			}
		}

		result_code = 0;
		break;
	}

	if (TRACE_FORN_RPC) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignRpc::SubmitQuery result " << result_code;

	return result_code;
}

void ForeignRpcClient::ConfigPostset()
{
	for (unsigned i = 1; i <= XREQ_BLOCKCHAIN_MAX; ++i)
		base64_encode_string(base64sym, rpc_username[i] + ":" + rpc_password[i], rpc_auth[i]);
}

void ForeignRpcClient::DumpExtraConfigBottom() const
{
	cout << "   Bitcoin (BTC) verify level = " << verify_level[XREQ_BLOCKCHAIN_BTC] << endl;
	cout << "   Bitcoin (BTC) RPC port = " << rpc_port[XREQ_BLOCKCHAIN_BTC] << endl;
	cout << "   Bitcoin (BTC) RPC username = " << rpc_username[XREQ_BLOCKCHAIN_BTC] << endl;

	cout << "   Bitcoin Cash (BCH) verify level = " << verify_level[XREQ_BLOCKCHAIN_BCH] << endl;
	cout << "   Bitcoin Cash (BCH) RPC port = " << rpc_port[XREQ_BLOCKCHAIN_BCH] << endl;
	cout << "   Bitcoin Cash (BCH) RPC username = " << rpc_username[XREQ_BLOCKCHAIN_BCH] << endl;
}

void ForeignRpcClient::Start()
{
	if (!enabled)
		return;

	BOOST_LOG_TRIVIAL(trace) << Name() << " ForeignRpcClient Start";

	unsigned TXCONN_READ_MAX = NON_WITNESS_BUFSIZE;

	for (unsigned i = 1; i <= XREQ_BLOCKCHAIN_MAX; ++i)
	{
		if (GetVerifyLevel(i) > VerifyLevel::If_Possible)
			TXCONN_READ_MAX = WITNESS_FOREIGN_CONTENT_LENGTH_MAX + 8*1024;
	}

	// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
	CCServer::ConnectionFactoryInstantiation<ForeignRpc> connfac(TXCONN_READ_MAX, 0, -1, -1, 0, 1, 0);
	CCThreadFactoryInstantiation<ForeignRpcThread> threadfac;

	unsigned maxconns = (unsigned)(max_inconns + max_outconns);
	unsigned nthreads = maxconns * threads_per_conn;

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	m_service.Start(boost::asio::ip::tcp::endpoint(),
			nthreads, maxconns, 0, 0, connfac, threadfac);
}

// TODO: separate connection pools for each foreign_blockchain -- implement using multiple m_service servers?
ForeignRpc* ForeignRpcClient::GetConnection(bool autofree)
{
	if (!enabled)
		return NULL;

	while (true)
	{
		if (g_shutdown)
			return NULL;

		auto pconn = (ForeignRpc*)m_service.GetServer(0).GetConnectionManager().GetFreeConnection();

		if (pconn)
		{
			pconn->m_autofree = autofree;

			return pconn;
		}

		BOOST_LOG_TRIVIAL(debug) << "ForeignRpcClient::GetConnection waiting for connection";

		sleep(1);
	}
}

void ForeignRpcClient::StartShutdown()
{
	m_service.StartShutdown();
}

void ForeignRpcClient::WaitForShutdown()
{
	if (!enabled)
		return;

	m_service.WaitForShutdown();
}

void ForeignRpcThread::ThreadProc(boost::function<void()> threadproc)
{
	BOOST_LOG_TRIVIAL(info) << "ForeignRpcThread::ThreadProc start " << (uintptr_t)this;

	threadproc();

	BOOST_LOG_TRIVIAL(info) << "ForeignRpcThread::ThreadProc end " << (uintptr_t)this;
}
