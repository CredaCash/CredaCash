/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * rpcserve.cpp
*/

#include "ccwallet.h"
#include "rpcserve.hpp"
#include "jsonrpc.h"
#include "walletdb.hpp"

#include <ccserver/server.hpp>
#include <ccserver/connection_manager.hpp>

#define TRACE_RPCSERVE	(g_params.trace_rpcserve)

#define RPC_READ_MAX				(64*1024)	//@@! make this a param?
#define RPC_RESPONSE_HEADER_MAX		1000

#define CRLF	"\x0d\x0a"
#define CLZ		CRLF "Content-Length: 0" CRLF CRLF

thread_local static DbConn *dbconn;

void RpcConnection::StartRead()
{
	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::StartRead";

	m_parse_point = 0;
	m_content_start = 0;
	m_content_length = -1;
	m_has_auth = false;

	Connection::StartRead();
}

void RpcConnection::HandleReadComplete()
{
	// !!! add simulated errors???

	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete read " << m_nred;

	while (true)
	{
		//if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete buffer " << buf2hex(&m_pread[m_parse_point], m_nred - m_parse_point);
		if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete parse " << m_parse_point << " of " << m_nred;

		// find terminator
		auto termscan = m_parse_point;
		while (true)
		{
			if (g_shutdown)
				return Stop();

			if (termscan >= m_nred)
				return QueueRead(1);

			if (m_pread[termscan] == m_terminator)
			{
				if (termscan == m_parse_point)
					break;					// blank line

				if (termscan > 0 && m_pread[termscan-1] == 13 && termscan-1 == m_parse_point)
					break;					// blank line

				if (termscan+1 >= m_nred)
					return QueueRead(1);	// read more to peek ahead to next time

				if (m_pread[termscan+1] != ' ' && m_pread[termscan+1] != 9)
					break;					// next line is not a continuation line

				// next line is a continuation line, so replace terminators with spaces and then continue

				m_pread[termscan] = ' ';
				if (termscan > 0 && m_pread[termscan-1] == 13)
					m_pread[termscan-1] = ' ';
			}

			++termscan;
		}

		m_pread[termscan] = 0;
		if (termscan > 0 && m_pread[termscan-1] == 13)
			m_pread[termscan-1] = 0;

		const char authheader[] = "Authorization:";

		if (TRACE_RPCSERVE && strncasecmp((const char*)&m_pread[m_parse_point], authheader, sizeof(authheader)-1))		// don't log auth
			BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete header line len " << strlen(&m_pread[m_parse_point]) << " >>" << &m_pread[m_parse_point] << "<<";
		else if (TRACE_RPCSERVE)
			BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete header line len " << strlen(&m_pread[m_parse_point]) << " >>" << authheader << " *****" << "<<";

		// the code below shouldn't read past the end of the buffer, but this restriction also seems prudent
		if (termscan + 200 >= m_readbuf.size())
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete header parsing " << termscan << " may overrun buffer " << m_readbuf.size();

			return Stop();
		}

		if (m_parse_point == 0)
		{
			// parse request line
			// this code requires the request line to start with "POST " or "OPTIONS "(case insensitive), and ignores the rest of the line

			//if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete buffer " << buf2hex(m_pread, termscan);

			const char post[] = "POST";
			const char options[] = "OPTIONS";
			unsigned pos = 0;

			m_keepalive = false;		// assume this is not a persistent connection

			if (!strncmp((const char*)m_pread, post, sizeof(post)-1))
			{
				m_method = POST;
				pos = sizeof(post)-1;
			}
			else if ((!strncmp((const char*)m_pread, options, sizeof(options)-1)))
			{
				m_method = OPTIONS;
				m_content_length = 0;
				pos = sizeof(options)-1;
			}
			else
				return SendMethodNotImplemented();

			if (m_pread[pos] == 0)
				return SendBadRequest();

			if (m_pread[pos] != ' ')
				return SendMethodNotImplemented();

			// currently no connection keep alive
			// !!! FUTURE: set m_keepalive if HTTP version is 1.1 or above
			//m_keepalive = true;
		}

		// !!! handle Expect: 100-continue?

		const char connheader[] = "Connection:";
		if (!strncasecmp((const char*)&m_pread[m_parse_point], connheader, sizeof(connheader)-1))
		{
			auto *bufp = &m_pread[m_parse_point + sizeof(connheader)-1];

			if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete " << connheader << "\"" << bufp << "\"";

			for (; *bufp; ++bufp)
			{
				// !!! should also look for "keep-alive"

				const char close[] = "close";
				if (!strncasecmp((const char*)bufp, close, sizeof(close)-1))
				{
					// !!! should really look for a terminator after "close"

					if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete " << connheader << close;

					m_keepalive = false;

					break;
				}
			}
		}

		const char clheader[] = "Content-Length:";
		if (!strncasecmp((const char*)&m_pread[m_parse_point], clheader, sizeof(clheader)-1))
		{
			auto *bufp = &m_pread[m_parse_point + sizeof(clheader)-1];

			if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete " << clheader << "\"" << bufp << "\"";

			m_content_length = buf2uint(bufp);

			if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete " << clheader << m_content_length;
		}

		if (!strncasecmp((const char*)&m_pread[m_parse_point], authheader, sizeof(authheader)-1))
		{
			auto *bufp = &m_pread[m_parse_point + sizeof(authheader)-1];

			if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete " << authheader;
			//if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete " << authheader << "\"" << bufp << "\"";	// logging this would be a security leak

			m_has_auth = !g_rpc_service.auth_string.empty() && !strcmp(bufp, g_rpc_service.auth_string.c_str());

			if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete m_has_auth " << m_has_auth;
		}

		bool blank = !m_pread[m_parse_point];

		m_parse_point = termscan + 1;		// continue parsing at start of next line

		if (blank)
			break;
	}

	// blank line signals the end of the headers

	if ((int)m_content_length == -1)
		return SendBadRequest();	// note: does not support chunked transfer

	m_maxread = m_parse_point + m_content_length;

	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete headers done parse " << m_parse_point << " of " << m_nred << " content-length " << m_content_length << " need " << (int)m_maxread - (int)m_nred;

	if (m_maxread >= m_readbuf.size() - 1)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleReadComplete content " << m_content_length << " will overrun buffer " << m_readbuf.size();

		return Stop();
	}

	if (m_nred < m_maxread)
	{
		// read the request body

		ReadAsync("RpcConnection::HandleReadComplete", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(m_maxread - m_nred),
				boost::bind(&RpcConnection::HandleContentReadComplete, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, AutoCount(this)));
	}
	else
	{
		// already have the request body

		HandleContentReadComplete(boost::system::error_code(), 0, AutoCount(this));	// don't need to increment op count, but too much effort to chain the AutoCount from the function calling HandleReadComplete
	}
}

void RpcConnection::HandleContentReadComplete(const boost::system::error_code& e, size_t bytes_transferred, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	m_nred += bytes_transferred;

	bool sim_err = RandTest(RTEST_READ_ERRORS);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleContentReadComplete simulating read error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleContentReadComplete error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

		return Stop();
	}

	if (m_nred < m_maxread)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleContentReadComplete error short read " << m_nred << " max " << m_maxread;

		return Stop();
	}

	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleContentReadComplete read " << m_nred;

	m_pread[m_nred] = 0;

	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleContentReadComplete content " << &m_pread[m_parse_point];

	auto rbuf = make_shared<string>();
	int result = 0;

	if (m_method == POST && m_has_auth)
	{
		if (0)
		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
			cerr << time(NULL) << " " << m_conn_index << " in  >>" << &m_pread[m_parse_point] << "<<" << endl;
		}

		if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleContentReadComplete do_json_rpc";

		ostringstream rstream;

		result = do_json_rpc(&m_pread[m_parse_point], dbconn, m_txquery, rstream);
		if (result)
			m_keepalive = false;

		*rbuf = rstream.str();

		if (0)
		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
			cerr << time(NULL) << " " << m_conn_index << " out  <" << *rbuf << "<<" << endl;
		}

		if (!result && !rbuf->size())
			return Stop();	// no response
	}

	// send headers

	auto bufp = m_writebuf.data();
	auto bufsize = m_writebuf.size();

	const char format1[] = "HTTP/1.1 %s" CRLF;
	auto n = snprintf(bufp, bufsize, format1, (!m_has_auth ? "403 Forbidden" : (result < 0 ? "400 Bad Request" : "200 OK")));
	CCASSERT(n >= 0 && (unsigned)n < bufsize);
	bufp += n;
	bufsize -= n;

	auto now = unixtime();
	auto n2 = strftime(bufp, bufsize, "Date: %a, %d %b %Y %H:%M:%S GMT" CRLF, gmtime(&now));
	CCASSERT(n2);
	bufp += n2;
	bufsize -= n2;

	const char format2[] = "Content-Type: application/json" CRLF "Content-Length: %lu" CRLF CRLF;
	n = snprintf(bufp, bufsize, format2, (unsigned long)rbuf->size());
	CCASSERT(n >= 0 && (unsigned)n < bufsize);
	bufp += n;
	bufsize -= n;

	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleContentReadComplete response headers >>" << m_writebuf.data() << "<<";
	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleContentReadComplete response body >>" << *rbuf << "<<";

	m_read_after_write = m_keepalive;

	WriteAsync("RpcConnection::HandleContentReadComplete", boost::asio::buffer(m_writebuf.data(), bufp - m_writebuf.data()),
			boost::bind(&RpcConnection::HandleWriteHeader, this, boost::asio::placeholders::error, rbuf, AutoCount(this)));

	// can't StartRead here because the client may have already half-closed the connection
	// StartRead would then get an eof error and hard stop the connection before the response can be sent
	// so instead use m_read_after_write
	// Maybe eventually StartRead can be fixed to defer Stop until the result has been written
	#if 0
	if (m_noclose)
		StartRead();
	#endif
}

void RpcConnection::HandleWriteHeader(const boost::system::error_code& e, shared_ptr<string> buf, AutoCount pending_op_counter)
{
	if (g_disable_malloc_logging)
	{
		g_disable_malloc_logging = false;

		cc_malloc_logging(false);
	}

	if (CheckOpCount(pending_op_counter))
		return;

	if (e) return Stop();

	// Note: keep m_write_in_progress lock and call WriteAsync with already_own_mutex = true

	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleWriteHeader ok";

	if (!buf->size())
		return Stop();

	// write the response body

	//if (buf->size() > 64*1024) sleep(1);	// for testing

	WriteAsync("RpcConnection::HandleWriteHeader", boost::asio::buffer(buf->data(), buf->size()),
			boost::bind(&Connection::HandleWriteString, this, boost::asio::placeholders::error, buf, AutoCount(this)), true);
}

void RpcConnection::SendServerError()
{
	static const string outbuf = "HTTP/1.1 500 Server Error" CLZ;

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RpcConnection::SendServerError sending " << outbuf;

	m_noclose = false;

	WriteAsync("RpcConnection::SendServerError", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void RpcConnection::SendBadRequest()
{
	static const string outbuf = "HTTP/1.1 400 Bad Request" CLZ;

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RpcConnection::SendBadRequest sending " << outbuf;

	m_noclose = false;

	WriteAsync("RpcConnection::SendBadRequest", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void RpcConnection::SendMethodNotImplemented()
{
	static const string outbuf = "HTTP/1.1 501 Not Implemented" CLZ;

	BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " RpcConnection::SendMethodNotImplemented sending " << outbuf;

	m_noclose = false;

	WriteAsync("RpcConnection::SendMethodNotImplemented", boost::asio::buffer(outbuf.c_str(), outbuf.size()),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}


void RpcService::Start()
{
	if (!enabled)
		return;

	if (TRACE_RPCSERVE) BOOST_LOG_TRIVIAL(trace) << Name() << " RpcService port " << port;

	// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
	CCServer::ConnectionFactoryInstantiation<RpcConnection> connfac(RPC_READ_MAX, RPC_RESPONSE_HEADER_MAX, -1, -1, 0, 0, 0);
	CCThreadFactoryInstantiation<RpcThread> threadfac;

	unsigned maxconns = (unsigned)(max_inconns + max_outconns);
	unsigned nthreads = maxconns * threads_per_conn;

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	m_service.Start(boost::asio::ip::tcp::endpoint(address, port),
			nthreads, maxconns, max_inconns, 0, connfac, threadfac);
}

void RpcService::StartShutdown()
{
	m_service.StartShutdown();
}

void RpcService::WaitForShutdown()
{
	m_service.WaitForShutdown();
}

void RpcThread::ThreadProc(boost::function<void()> threadproc)
{
	dbconn = new DbConn;

	BOOST_LOG_TRIVIAL(info) << "RpcThread::ThreadProc start " << (uintptr_t)this << " dbconn " << (uintptr_t)dbconn;

	threadproc();

	BOOST_LOG_TRIVIAL(info) << "RpcThread::ThreadProc end " << (uintptr_t)this << " dbconn " << (uintptr_t)dbconn;

	delete dbconn;
}
