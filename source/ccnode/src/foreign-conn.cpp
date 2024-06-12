/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * foreign-conn.cpp
*/

#include "ccnode.h"
#include "foreign-conn.hpp"
#include "witness.hpp"

#define TRACE_FORN_CONN		g_params.trace_foreign_conn

void ForeignConnection::StartConnection()
{
	CCASSERT(m_pquery);

	m_conn_state = CONN_CONNECTED;
	m_parse_point = 0;
	m_content_start = 0;
	m_content_length = -1;

	if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::StartConnection request " << *m_pquery;

	// this function could call SetTimer, but instead it just leaves connect timer running

	// send the request

	auto data = m_pquery->data();
	auto size = m_pquery->size();

	m_pquery = NULL;

	WriteAsync("ForeignConnection::StartConnection", boost::asio::buffer(data,size),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
}

void ForeignConnection::StartRead()
{
	if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::StartRead";

	m_parse_point = 0;
	m_content_start = 0;
	m_content_length = -1;

	Connection::StartRead();
}

void ForeignConnection::HandleReadComplete()
{
	// !!! add simulated errors???

	if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete read " << m_nred;

	while (true)
	{
		//if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete buffer " << buf2hex(&m_pread[m_parse_point], m_nred - m_parse_point);
		if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete parse " << m_parse_point << " of " << m_nred;

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

		if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete header line len " << strlen(&m_pread[m_parse_point]) << " >>" << &m_pread[m_parse_point] << "<<";

		// the code below shouldn't read past the end of the buffer, but this restriction also seems prudent
		if (termscan + 200 >= m_readbuf.size())
		{
			BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete header parsing " << termscan << " may overrun buffer " << m_readbuf.size();

			return Stop();
		}

		if (m_parse_point == 0)
		{
			// parse status line

			//if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete buffer " << buf2hex(m_pread, termscan);

			const char http[] = "HTTP/";
			const char ok[] = "200 ";

			if (strncmp((const char*)m_pread, http, sizeof(http)-1))
				return Stop();

			auto pos = sizeof(http)-1;

			//if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete status line after version: " << " >>" << &m_pread[pos] << "<<";

			// find space
			for ( ; ; ++pos)
			{
				if (!m_pread[pos])
					return Stop();

				if (m_pread[pos] == ' ')
					break;
			}

			//if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete status line after space: " << " >>" << &m_pread[pos] << "<<";

			// find non-space
			for ( ; ; ++pos)
			{
				if (!m_pread[pos])
					return Stop();

				if (m_pread[pos] != ' ')
					break;
			}

			//if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete status line after non-space: " << " >>" << &m_pread[pos] << "<<";

			if (strncmp(&m_pread[pos], ok, sizeof(ok)-1))
				return Stop();
		}

		const char clheader[] = "Content-Length:";
		if (!strncasecmp((const char*)&m_pread[m_parse_point], clheader, sizeof(clheader)-1))
		{
			auto *bufp = &m_pread[m_parse_point + sizeof(clheader)-1];

			if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete " << clheader << "\"" << bufp << "\"";

			m_content_length = buf2uint(bufp);

			if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete " << clheader << m_content_length;

			if (m_content_length > WITNESS_FOREIGN_CONTENT_LENGTH_MAX && IsWitness())
			{
				BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete content length " << m_content_length << " > " << WITNESS_FOREIGN_CONTENT_LENGTH_MAX;

				return Stop();
			}
		}

		bool blank = !m_pread[m_parse_point];

		m_parse_point = termscan + 1;		// continue parsing at start of next line

		if (blank)
			break;
	}

	// blank line signals the end of the headers

	if ((int)m_content_length == -1)
		return Stop();

	m_content_start = m_parse_point;

	m_maxread = m_content_start + m_content_length;

	if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete headers done parse " << m_parse_point << " of " << m_nred << " content-length " << m_content_length << " need " << (int)m_maxread - (int)m_nred;

	if (m_maxread >= m_readbuf.size() - 1)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleReadComplete content " << m_content_length << " will overrun buffer " << m_readbuf.size();

		return Stop();
	}

	if (m_nred < m_maxread)
	{
		// read the request body

		ReadAsync("ForeignConnection::HandleReadComplete", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(m_maxread - m_nred),
				boost::bind(&ForeignConnection::HandleContentReadComplete, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, AutoCount(this)));
	}
	else
	{
		// already have the request body

		HandleContentReadComplete(boost::system::error_code(), 0, AutoCount(this));	// don't need to increment op count, but too much effort to chain the AutoCount from the function calling HandleReadComplete
	}
}

void ForeignConnection::HandleContentReadComplete(const boost::system::error_code& e, size_t bytes_transferred, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	m_nred += bytes_transferred;

	bool sim_err = RandTest(RTEST_READ_ERRORS);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleContentReadComplete simulating read error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleContentReadComplete error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

		return Stop();
	}

	if (m_nred < m_maxread)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleContentReadComplete error short read " << m_nred << " max " << m_maxread;

		return Stop();
	}

	if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleContentReadComplete read " << m_nred;

	m_pread[m_nred] = 0;

	if (TRACE_FORN_CONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleContentReadComplete content " << &m_pread[m_parse_point];

	if (m_content_length > WITNESS_FOREIGN_CONTENT_LENGTH_MAX - 100*1024) BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " ForeignConnection::HandleContentReadComplete m_content_length " << m_content_length << " ~> " << WITNESS_FOREIGN_CONTENT_LENGTH_MAX << " content: " << &m_pread[m_parse_point];

	Connection::HandleReadComplete();	// signal done
}
