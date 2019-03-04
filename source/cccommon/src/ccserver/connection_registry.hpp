/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
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
		last_unmapped_index = 0;
		m_connections.push_back(NULL);	// leave entry zero NULL to guard against bugs
	}

	int RegisterConn(Connection *conn, bool map);

	Connection* GetConn(int index);

protected:
	vector<Connection *> m_connections;
	int last_unmapped_index;

	FastSpinLock m_conn_registry_lock;
};

} // namespace CCServer

extern CCServer::ConnectionRegistry g_connregistry;
