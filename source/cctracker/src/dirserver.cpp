/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * dirserver.cpp
*/

#include "cctracker.h"
#include "dirserver.h"
#include "dir.hpp"

#include <CCcrypto.hpp>

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#define TIMEOUT					10

class DirConnection : public CCServer::Connection
{
	mt19937 m_random;
	string m_namestr;

public:
	DirConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	:	CCServer::Connection(manager, io_service, connfac),
		m_namestr(TOR_HOSTNAME_CHARS, 0)
	{
		static int rndseed = 0;
		m_random.seed(time_t() + (rndseed++));
	}

private:

	void StartConnection()
	{
		BOOST_LOG_TRIVIAL(trace) << "StartConnection";

		m_conn_state = CONN_CONNECTED;

		if (SetTimer(TIMEOUT))
			return;

		StartRead();
	}

	void HandleReadComplete()
	{
		// request is:
		//		str += "T:" + time + "\n";
		//		str += "R:" + g_relay_service.TorHostname() + "\n"
		//		str += "B:" + g_blockserve_service.TorHostname() + "\n"
		//		str += "W:" + POW_nonce + "\n";
		//		str += "QRB\0";

		m_pread[m_nred] = 0;

		BOOST_LOG_TRIVIAL(debug) << "DirConnection received:\n" << m_pread;
		//cout << buf2hex(m_pread, m_nred) << endl;

		for (unsigned scan = 0; ; ++scan)
		{
			if (scan >= m_nred - 3)
			{
				BOOST_LOG_TRIVIAL(info) << "DirConnection POW not found " << m_pread;

				return Stop();
			}

			if (!(m_pread[scan] == '\n' && m_pread[scan+1] == 'W' && m_pread[scan+2] == ':'))
				continue;

			auto nonce = buf2uint64(&m_pread[scan+3]);

			auto test = string(m_pread, scan+1);
			test += g_tor_hostname;

			auto rc = CheckPOW(test.data(), test.length(), g_difficulty * 100000, nonce);

			if (rc && !(nonce && nonce == (uint64_t)g_magic_nonce))
			{
				BOOST_LOG_TRIVIAL(info) << "DirConnection POW check failed " << m_pread;

				return Stop();
			}

			break;
		}

		for (unsigned scan = 0; scan < m_nred; ++scan)
		{
			if (m_pread[scan] == '\n')
				m_pread[scan] = 0;
		}

		string relayname, blockname;
		bool past_pow = false;
		bool got_time = false;

		for (unsigned scan = 0; scan < m_nred - 3; ++scan)
		{
			bool in_pow = true;

			if (!m_pread[scan])
				continue;
			else if (m_pread[scan] == 'T' && m_pread[scan+1] == ':')
			{
				got_time = true;

				uint64_t timestamp = buf2uint64(&m_pread[scan+2]);
				uint64_t now = (unixtime() - TX_TIME_OFFSET) / 600; // 10 minute granularity

				if (timestamp < now - (g_time_allowance + 9)/10 || timestamp > now + (g_time_allowance + 9)/10)
				{
					BOOST_LOG_TRIVIAL(info) << "DirConnection invalid timestamp " << timestamp << " now " << now;

					return Stop();
				}
			}
			else if (m_pread[scan] == 'R' && m_pread[scan+1] == ':')
			{
				relayname = &m_pread[scan+2];
			}
			else if (m_pread[scan] == 'B' && m_pread[scan+1] == ':')
			{
				blockname = &m_pread[scan+2];
			}
			else if (m_pread[scan] == 'W' && m_pread[scan+1] == ':')
			{
				in_pow = false;
				past_pow = true;
			}
			else if (m_pread[scan] == 'Q')
			{
				in_pow = false;

				if (strcmp(&m_pread[scan+1], "RB"))
				{
					BOOST_LOG_TRIVIAL(debug) << "DirConnection unrecognized query " << &m_pread[scan];

					return Stop();
				}
			}
			else
			{
				in_pow = past_pow = true; // force error
			}

			if (in_pow && past_pow)
			{
				BOOST_LOG_TRIVIAL(info) << "DirConnection invalid request " << m_pread;

				return Stop();
			}

			scan += strlen(&m_pread[scan]);
		}

		if (!got_time)
		{
			BOOST_LOG_TRIVIAL(info) << "DirConnection missing timestamp " << m_pread;

			return Stop();
		}

		if (relayname.length())
		{
			BOOST_LOG_TRIVIAL(trace) << "DirConnection relayname " << relayname;

			g_relaydir.Add(relayname);
		}

		if (blockname.length())
		{
			BOOST_LOG_TRIVIAL(trace) << "DirConnection blockname " << blockname;

			g_blockdir.Add(blockname);
		}

		unsigned seed = m_random();
		auto output = m_readbuf.data();
		uint32_t bufsize = m_readbuf.size();
		uint32_t bufpos = 0;

		char json1[] = "{\"Relay\":[";
		copy_to_buf(json1, sizeof(json1)-1, bufpos, output, bufsize);

		g_relaydir.PickN(seed, RELAY_QUERY_MAX_NAMES, m_namestr, output, bufpos);

		char json2[] = "],\"Block\":[";
		copy_to_buf(json2, sizeof(json2)-1, bufpos, output, bufsize);

		g_blockdir.PickN(seed, BLOCK_QUERY_MAX_NAMES, m_namestr, output, bufpos);

		char json3[] = "]}";
		copy_to_buf(json3, sizeof(json3)-1, bufpos, output, bufsize);

		output[bufpos++] = 0;

		if (bufpos > bufsize)
		{
			output[bufsize-1] = 0;

			BOOST_LOG_TRIVIAL(fatal) << "DirConnection buffer overflow bufpos " << bufpos << " bufsize " << bufsize << " buffer " << output;
		}

		BOOST_LOG_TRIVIAL(trace) << "DirConnection reply: " << output;

		WriteAsync("DirConnection::HandleReadComplete", boost::asio::buffer(output, bufpos),
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
	CCServer::ConnectionFactoryInstantiation<DirConnection> connfac((TOR_HOSTNAME_CHARS + 3)*(RELAY_QUERY_MAX_NAMES + BLOCK_QUERY_MAX_NAMES) + 80, 0, 0, 0, 0, 0, 0);

	CCServer::Service s("Directory Service");

	// unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog
	s.Start(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(LOCALHOST), g_port),
			g_nthreads, g_nthreads*g_nconns, g_nthreads*g_nconns, 0, connfac);

	wait_for_shutdown();

	s.StartShutdown();
	s.WaitForShutdown();

	g_relaydir.DeInit();
	g_blockdir.DeInit();
}
