/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * service.hpp
*/

#pragma once

#include <CCthread.hpp>

#include <vector>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>

using namespace std;

namespace CCServer {

class Service
	: private boost::noncopyable
{
	const string m_name;

public:

	const string& Name() const
	{
		return m_name;
	}

	Service(const string& name)
	 :	m_name(name)
	{ }

	// Start service
	void Start(const boost::asio::ip::tcp::endpoint& endpoint, unsigned nthreads, unsigned maxconns, unsigned maxincoming, unsigned backlog, const class ConnectionFactory& connfac, const class CCThreadFactory& threadfac = CCDefaultThreadFac);

	unsigned GetNServers()
	{
		return m_servers.size();
	}

	class Server& GetServer(unsigned i)
	{
		return *m_servers[i];
	}

	// Commence service shutdown
	void StartShutdown();

	// Wait for service to shutdown
	void WaitForShutdown();

protected:
	vector<class Server *> m_servers;
	vector<class CCThread *> m_threads;
};

} // namespace CCServer
