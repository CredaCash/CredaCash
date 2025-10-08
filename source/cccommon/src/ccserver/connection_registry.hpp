/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
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
	 :	m_conn_registry_lock(__FILE__, __LINE__)
	{ }

	~ConnectionRegistry();

	void Init();
	void DeInit();

	int RegisterConn(Connection *conn, bool map);

	Connection* GetConn(int index);

protected:
	vector<Connection *> m_connections;
	int last_unmapped_index;

	FastSpinLock m_conn_registry_lock;
};

} // namespace CCServer

extern CCServer::ConnectionRegistry g_connregistry;
