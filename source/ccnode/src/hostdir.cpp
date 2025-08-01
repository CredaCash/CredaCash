/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * hostdir.cpp
*/

#include "ccnode.h"
#include "hostdir.hpp"
#include "socks.hpp"
#include "relay.hpp"
#include "blockserve.hpp"

#include <CCcrypto.hpp>

#define MAX_SAVED_HOSTS		200
#define MAX_DIR_REPLY_SIZE	((TOR_HOSTNAME_CHARS + 3)*(RELAY_QUERY_MAX_NAMES + BLOCK_QUERY_MAX_NAMES) + 200)

#define QUERY_SERVER_TRIES	1	// set to 1 so retries will use a different server instead of same server

#define TRACE_HOSTDIR	(g_params.trace_host_dir)

bool HostDir::Init()
{
	if (m_rendezvous_servers.size())
	{
		BOOST_LOG_TRIVIAL(trace) << "HostDir::Init already initialized";

		return false;
	}

	BOOST_LOG_TRIVIAL(trace) << "HostDir::Init file \"" << w2s(g_params.rendezvous_servers_file) << "\"";

	CCASSERT(g_params.rendezvous_servers_file.length());

	boost::filesystem::ifstream fs;
	fs.open(g_params.rendezvous_servers_file, fstream::in);
	if(!fs.is_open())
	{
		BOOST_LOG_TRIVIAL(error) << "HostDir::Init error opening rendezvous servers file \"" << w2s(g_params.rendezvous_servers_file) << "\"";

		return true;
	}

	while (!g_shutdown)
	{
		string line;

		fs >> line;

		if (fs.fail() && !fs.eof())
		{
			BOOST_LOG_TRIVIAL(error) << "HostDir::Init error reading rendezvous servers file \"" << w2s(g_params.rendezvous_servers_file) << "\"";

			return true;
		}

		boost::trim(line);
		static const string onion = ".onion";
		int elength = line.length() - onion.length();

		//cerr << "line find " << line.rfind(onion) << " =? " << elength << endl;

		if (elength > 0 && (int)line.rfind(onion) == elength)
		{
			line.erase(elength);
			//cerr << "line erase " << line << endl;
		}

		if (line.length())
		{
			BOOST_LOG_TRIVIAL(trace) << "HostDir::Init read hostname \"" << line << "\"";

			m_rendezvous_servers.push_back(line);
		}

		if (fs.eof())
			break;
	}

	BOOST_LOG_TRIVIAL(debug) << "HostDir::Init loaded " << m_rendezvous_servers.size() << " private rendezvous hostnames";

	return false;
}

void HostDir::DeInit()
{
	BOOST_LOG_TRIVIAL(trace) << "HostDir::DeInit";

	while (m_query_in_progress.load())
		usleep(50*1000);

	if (m_socket.is_open())
	{
		boost::system::error_code ec;
		m_socket.close(ec);
	}
}

string HostDir::GetHostName(HostType type)
{
	if (g_shutdown)
		return string();

	lock_guard<mutex> lock(classlock);

	if (type < 0 || type >= N_HostTypes || hostnames[type].empty())
	{
		auto query = PrepareQuery();

		for (unsigned i = 0; i < QUERY_SERVER_TRIES && query.length() && !g_shutdown; ++i)
		{
			auto rc = QueryServer(query);
			if (!rc)
				break;
		}
	}

	if (type < 0 || type >= N_HostTypes)
		return string();

	while (!hostnames[type].empty())
	{
		auto name = hostnames[type].front();
		hostnames[type].pop_front();

		if (type == Relay && name == g_relay_service.TorHostname())
			continue;

		if (type == Blockserve && name == g_blockserve_service.TorHostname())
			continue;

		if (TRACE_HOSTDIR) BOOST_LOG_TRIVIAL(trace) << "HostDir::GetHostName returning " << name;

		return name;
	}

	if (TRACE_HOSTDIR) BOOST_LOG_TRIVIAL(trace) << "HostDir::GetHostName returning none";

	return string();
}

string HostDir::PrepareQuery()
{
	if (!m_rendezvous_servers.size())
		return string();

	unsigned server;
	CCPseudoRandom(&server, sizeof(server));
	server %= m_rendezvous_servers.size();
	string name = m_rendezvous_servers[server];

	if (TRACE_HOSTDIR) BOOST_LOG_TRIVIAL(info) << "HostDir::PrepareQuery server " << name; // << " torproxy port " << g_params.torproxy_port;

	string str = Socks::ConnectString(name, string());

	auto clen = str.length();

	//str += "X:" + to_string(rand()) + "\n"; // for testing

	str += "T:" + to_string((unixtime() - TX_TIME_OFFSET) / 600) + "\n"; // 10 minute granularity

	if (g_relay_service.TorHostname().length())
		str += "R:" + g_relay_service.TorHostname() + "\n";

	if (g_blockserve_service.TorHostname().length())
		str += "B:" + g_blockserve_service.TorHostname() + "\n";

	auto trunc = str.length();

	str += name;

	uint64_t nonce = g_params.rendezvous_magic_nonce;

	auto rc = ComputePOW(str.data() + clen, str.size() - clen, nonce ? 0 : g_params.rendezvous_server_difficulty * 100000, unixtime() + 5*60, nonce);
	if (rc)
	{
		//cerr << "ComputePOW rc " << rc << " @ " << unixtime() << endl;

		return string();
	}

	str.resize(trunc);

	str += "W:" + to_string(nonce) + "\n";

	str += "QRB";

	str.push_back(0);

	//cerr << "rendezvous query length " << str.length() << "\n" << str << endl;
	//cerr << buf2hex(str.data(), str.length()) << endl;

	if (str.length() - clen + name.length() + 6 >= 256)
	{
		// due to toproxy implementation, total string length including target onion hostname must be <= MAX_SOCKS_ADDR_LEN = 256

		BOOST_LOG_TRIVIAL(warning) << "HostDir::PrepareQuery query string may exceed max allowed length " << str;
	}

	return str;
}

bool HostDir::QueryServer(const string& query)
{
	string reply(SOCKS_REPLY_SIZE + MAX_DIR_REPLY_SIZE, 0);

	m_query_in_progress = true;

	boost::system::error_code e(boost::system::errc::operation_canceled, boost::system::generic_category());

	if (!g_shutdown)
		e = Socks::SendString(m_socket, g_params.torproxy_port, query, reply);

	m_query_in_progress = false;

	if (g_shutdown)
		return 0;

	if (e)
		return -1;

	//cerr << "rendezvous reply " << reply.length() << " bytes: " << reply << endl;

	if (TRACE_HOSTDIR) BOOST_LOG_TRIVIAL(debug) << "HostDir::QueryServer reply: " << reply;

	Json::CharReaderBuilder builder;
	Json::CharReaderBuilder::strictMode(&builder.settings_);
	Json::Value root;
	string errs;
	int rc;

	auto reader = builder.newCharReader();

	try
	{
		rc = reader->parse(reply.data(), reply.data() + reply.length(), &root, &errs);
	}
	catch (const exception& e)
	{
		errs = e.what();
		rc = false;
	}
	catch (...)
	{
		errs = "unknown";
		rc = false;
	}

	delete reader;

	if (!rc)
	{
		BOOST_LOG_TRIVIAL(error) << "HostDir::QueryServer error parsing reply: " << errs;

		return -1;
	}

	#if 0
	cerr << "json root.size() " << root.size() << endl;
	for (unsigned i = 0; i < root.size(); ++i)
		cerr << "json root[" << i << "] = " << root.getMemberNames().at(i) << endl;
	#endif

	ParseNameArray(root, "Relay", Relay);
	ParseNameArray(root, "Block", Blockserve);

	return 0;
}

void HostDir::ParseNameArray(Json::Value& root, const char* label, const HostType type)
{
	Json::Value value;

	if (!root.removeMember(label, &value))
		BOOST_LOG_TRIVIAL(error) << "HostDir::QueryServer warning: no " << label << " found in reply";
	else if (!value.isArray())
		BOOST_LOG_TRIVIAL(error) << "HostDir::QueryServer error: " << label << " value is not a json array";
	else for (unsigned i = 0; i < value.size(); ++i)
	{
		auto name = value[i].asString();
		if (name.empty())
			BOOST_LOG_TRIVIAL(error) << "HostDir::QueryServer error: empty " << label << " name";
		else
		{
			if (TRACE_HOSTDIR) BOOST_LOG_TRIVIAL(info) << "HostDir::QueryServer found " << label << " name " << name;

			if (hostnames[type].size() > MAX_SAVED_HOSTS)
				hostnames[type].pop_front();

			hostnames[type].push_back(name);
		}
	}
}
