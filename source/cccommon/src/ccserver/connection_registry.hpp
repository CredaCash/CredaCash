/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * connection_registry.hpp
*/

#pragma once

#include <vector>
#include <string>
#include <boost/noncopyable.hpp>

#include <SpinLock.hpp>
#include "../ccserver/connection.hpp"

using namespace std;

namespace CCServer {

class ConnectionRegistry
	: private boost::noncopyable
{

public:
	ConnectionRegistry()
	{
		m_connections.push_back(NULL);	// leave entry zero NULL to guard against bugs
	}

	unsigned RegisterConn(Connection *conn);

	Connection* GetConn(unsigned index);

protected:
	vector<Connection *> m_connections;

	FastSpinLock m_lock;
};

} // namespace CCServer

extern CCServer::ConnectionRegistry g_connregistry;
