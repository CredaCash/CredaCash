/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * dirserver.cpp
*/

#include "CCdef.h"
#include "dirserver.h"
#include "dir.hpp"

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#include <boost/bind.hpp>

#define TIMEOUT					10

#define RELAY_QUERY_RETURNS		20
#define BLOCK_QUERY_RETURNS		10

class DirConnection : public CCServer::Connection
{
	mt19937 m_random;
	string m_namestr;

public:
	DirConnection(class CCServer::ConnectionManager& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactory& connfac)
	:	CCServer::Connection(manager, io_service, connfac),
		m_namestr(NAME_CHARS, 0)
	{
		static int rndseed = 0;
		m_random.seed(time_t() + (rndseed++));
	}

private:

	void StartConnection()
	{
		BOOST_LOG_TRIVIAL(trace) << "StartConnection";

		auto op_counter = AutoCount();
		if (AsyncTimerWait("BlockServeConnection::SetTimer", TIMEOUT*1000, boost::bind(&DirConnection::HandleTimeout, this, boost::asio::placeholders::error, op_counter), op_counter))
			return;

		Connection::StartConnection();
	}

	void HandleTimeout(const boost::system::error_code& e, AutoCount pending_op_counter)
	{
		if (e == boost::asio::error::operation_aborted)
		{
			//BOOST_LOG_TRIVIAL(trace) << "HandleTimeout " << uintptr_t(this) << " e = " << e << " " << e.message();

			return;
		}

		BOOST_LOG_TRIVIAL(info) << "HandleTimeout " << uintptr_t(this) << " e = " << e << " " << e.message();

		Stop();
	}

	void HandleReadComplete()
	{
#if 0
		static const std::string outbuf = "HTTP/1.0 200 OK\r\n\r\nOK";
		//static const std::string outbuf = "OK";

		WriteAsync("DirConnection::HandleReadComplete", boost::asio::buffer(outbuf.c_str(), outbuf.size() + 1),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));

		return;
#endif

		// request is:
		//		str += "R:" + g_relay_service.TorHostname() + "\n"
		//		str += "B:" + g_blockserve_service.TorHostname() + "\n"
		//		str += "QRB\0";
		// TorHostname's are 16 chars

		const unsigned max_msg_len = (NAME_CHARS+3)*2 + 4;

		if (m_nred > max_msg_len)
		{
			BOOST_LOG_TRIVIAL(debug) << "DirConnection request size " << m_nred << " expected max of " << max_msg_len;

			return Stop();
		}

		m_pread[m_nred] = 0;

		BOOST_LOG_TRIVIAL(debug) << "DirConnection received " << m_pread;

		for (unsigned i = 0; i < m_nred; ++i)
			if (m_pread[i] == '\n')
				m_pread[i] = 0;

		string relayname, blockname;

		unsigned scan = 0;

		for (unsigned i = 0; i < 2; ++i)
		{
			if (m_pread[scan] == 'R' && m_pread[scan+1] == ':')
			{
				relayname = (char*)&m_pread[scan+2];
				scan += 3 + relayname.length();
				BOOST_LOG_TRIVIAL(trace) << "DirConnection relayname " << relayname;
			}
			else if (m_pread[scan] == 'B' && m_pread[scan+1] == ':')
			{
				blockname = (char*)&m_pread[scan+2];
				scan += 3 + blockname.length();
				BOOST_LOG_TRIVIAL(trace) << "DirConnection blockname " << blockname;
			}
		}

		if (strcmp((char*)&m_pread[scan], "QRB"))
		{
			BOOST_LOG_TRIVIAL(debug) << "DirConnection unrecognized query " << &m_pread[scan];

			return Stop();
		}

		if (relayname.length())
			g_relaydir.Add(relayname);

		if (blockname.length())
			g_blockdir.Add(blockname);

		const unsigned nrelay = RELAY_QUERY_RETURNS;
		const unsigned nblock = BLOCK_QUERY_RETURNS;
		unsigned seed = m_random();
		unsigned bufpos = 0;

		char json1[] = "{\"Relay\":[";
		strcpy((char*)&m_pread[bufpos], json1);
		bufpos += sizeof(json1) - 1;

		g_relaydir.PickN(seed, nrelay, m_namestr, &m_pread[0], bufpos);

		char json2[] = "],\"Block\":[";
		strcpy((char*)&m_pread[bufpos], json2);
		bufpos += sizeof(json2) - 1;

		g_blockdir.PickN(seed, nblock, m_namestr, &m_pread[0], bufpos);

		char json3[] = "]}";
		strcpy((char*)&m_pread[bufpos], json3);
		bufpos += sizeof(json3) - 1;

		m_pread[bufpos++] = 0;

		WriteAsync("DirConnection::HandleReadComplete", boost::asio::buffer(m_pread, bufpos),
				boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this)));
	}
};

void add_test_names(Dir &dir, char prefix)
{
	for (unsigned i = 0; i < 200; ++i)
	{
		char name[17];
		sprintf(name, "%016d", i);
		for (unsigned j = 0; j < strlen(name); ++j)
			name[j] += 'a' - '0';
		name[0] = prefix;
		dir.Add(name);
	}
}

void RunServer()
{
	g_relaydir.Init("RelayDir", 100 - g_blockfrac);
	g_blockdir.Init("BlockDir", g_blockfrac);

	//add_test_names(g_relaydir, 'a');	// for testing
	//add_test_names(g_blockdir, 'z');	// for testing

	// unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister
	CCServer::ConnectionFactoryInstantiation<DirConnection> connfac((NAME_CHARS+3)*(RELAY_QUERY_RETURNS+BLOCK_QUERY_RETURNS+1) + 80, 0, 0, 0, 0, 0, 0);

	CCServer::Service s("Directory Service");

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	s.Start(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(LOCALHOST), g_port),
			g_nthreads, g_nthreads*g_nconns, g_nthreads*g_nconns, 0, connfac);

	s.WaitForShutdown();

	g_relaydir.DeInit();
	g_blockdir.DeInit();
}




