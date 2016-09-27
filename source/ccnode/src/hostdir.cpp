/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * hostdir.cpp
*/

#include "CCdef.h"
#include "hostdir.hpp"
#include "socks.hpp"
#include "service_base.hpp"
#include "relay.hpp"
#include "blockserve.hpp"

#include <CCcrypto.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>

#define MAX_SAVED_HOSTS		200
#define MAX_REPLY_HOSTS		(20+10)
#define MAX_DIR_REPLY_SIZE	((16 + 3)*MAX_REPLY_HOSTS + 200)

#define TRACE_HOSTDIR	(g_params.trace_host_dir)

// .onion addresses are 16 characters in base32 = 10 bytes

bool HostDir::Init()
{
	if (m_directory_servers.size())
	{
		BOOST_LOG_TRIVIAL(trace) << " HostDir::Init already initialized";

		return false;
	}

	BOOST_LOG_TRIVIAL(trace) << " HostDir::Init file \"" << g_params.directory_servers_file << "\"";

	CCASSERT(g_params.directory_servers_file.length());

	boost::filesystem::ifstream fs;
	fs.open(g_params.directory_servers_file, fstream::in);
	if(!fs.is_open())
	{
		BOOST_LOG_TRIVIAL(error) << " HostDir::Init error opening private relay hosts file \"" << g_params.directory_servers_file << "\"";

		return true;
	}

	while (true)
	{
		string line;

		fs >> line;

		if (fs.fail() && !fs.eof())
		{
			BOOST_LOG_TRIVIAL(error) << " HostDir::Init error reading private relay hosts file \"" << g_params.directory_servers_file << "\"";

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

		if (line.length() > 0)
		{
			BOOST_LOG_TRIVIAL(trace) << " HostDir::Init read hostname \"" << line << "\"";

			m_directory_servers.push_back(line);
		}

		if (fs.eof())
			break;
	}

	BOOST_LOG_TRIVIAL(debug) << " HostDir::Init loaded " << m_directory_servers.size() << " private directory hostnames";

	return false;
}

string HostDir::GetHostName(HostType type)
{
	lock_guard<mutex> lock(classlock);

	if (type < 0 || type >= N_HostTypes || hostnames[type].empty())
		QueryServer();

	if (type < 0 || type >= N_HostTypes || hostnames[type].empty())
		return string();
	else
	{
		auto name = hostnames[type].front();
		hostnames[type].pop_front();

		if (TRACE_HOSTDIR) BOOST_LOG_TRIVIAL(trace) << "HostDir::GetHostName returning " << name;

		return name;
	}
}

void HostDir::QueryServer()
{
	if (!m_directory_servers.size())
		return;

	unsigned server;
	CCPseudoRandom(&server, sizeof(server));
	server %= m_directory_servers.size();
	string name = m_directory_servers[server];

	if (TRACE_HOSTDIR) BOOST_LOG_TRIVIAL(trace) << "querying directory server " << name << " torproxy port " << g_params.torproxy_port;

	string str = Socks::ConnectString(name);

	if (g_relay_service.TorHostname().length())
		str += "R:" + g_relay_service.TorHostname() + "\n";

	if (g_blockserve_service.TorHostname().length())
		str += "B:" + g_blockserve_service.TorHostname() + "\n";

	str += "QRB";
	str.push_back(0);

	//cerr << "directory query " << str << endl;

	string reply(MAX_DIR_REPLY_SIZE, 0);

	auto e = Socks::SendString(g_params.torproxy_port, str, reply);

	if (e)
		return;

	//cerr << "directory reply " << reply.length() << " bytes: " << reply << endl;

	if (TRACE_HOSTDIR) BOOST_LOG_TRIVIAL(trace) << "HostDir::QueryServer directory reply: " << reply;

	Json::Reader reader;
	Json::Value root, value;

	reader.parse(reply.data(), reply.data() + reply.length(), root);

	if (!reader.good())
		BOOST_LOG_TRIVIAL(error) << "HostDir::QueryServer error parsing reply: " << reader.getFormattedErrorMessages();

	#if 0
	cerr << "json root.size() " << root.size() << endl;
	for (unsigned i = 0; i < root.size(); ++i)
		cerr << "json root[" << i << "] = " << root.getMemberNames().at(i) << endl;
	#endif

	ParseNameArray(root, "Relay", Relay);
	ParseNameArray(root, "Block", Blockserve);
}

void HostDir::ParseNameArray(Json::Value &root, const char* label, const HostType type)
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
