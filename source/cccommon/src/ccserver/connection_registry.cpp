/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * connection_registry.cpp
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "connection_registry.hpp"

CCServer::ConnectionRegistry g_connregistry;

namespace CCServer {

ConnectionRegistry::~ConnectionRegistry()
{
	//CCASSERT(m_connections.empty());
}

void ConnectionRegistry::DeInit()
{
	m_connections.clear();
}

void ConnectionRegistry::Init()
{
	CCASSERT(m_connections.empty());

	m_connections.push_back(NULL);	// leave entry zero NULL to guard against bugs

	last_unmapped_index = 0;
}

int ConnectionRegistry::RegisterConn(Connection *conn, bool map)
{
	int index;

	{
		lock_guard<FastSpinLock> lock(m_conn_registry_lock);

		if (map)
		{
			index = m_connections.size();
			m_connections.push_back(conn);
		}
		else
			index = --last_unmapped_index;
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << "ConnectionRegistry::RegisterConn index " << index << " conn " << (uintptr_t)conn;

	return index;
}

Connection* ConnectionRegistry::GetConn(int index)
{
	CCASSERT(index > 0);

	lock_guard<FastSpinLock> lock(m_conn_registry_lock);

	auto conn = m_connections[index];

	CCASSERT(conn);

	return conn;
}

} // namespace CCServer