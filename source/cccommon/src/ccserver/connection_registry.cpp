/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * connection_registry.cpp
*/

#include "CCdef.h"
#include "connection_registry.hpp"

#include <cstdint>

using namespace std;

CCServer::ConnectionRegistry g_connregistry;

namespace CCServer {

unsigned ConnectionRegistry::RegisterConn(Connection *conn)
{
	unsigned index;

	{
		lock_guard<FastSpinLock> lock(m_lock);

		index = m_connections.size();
		m_connections.push_back(conn);
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << "ConnectionRegistry::RegisterConn index " << index << " conn " << (uintptr_t)conn;

	return index;
}

Connection* ConnectionRegistry::GetConn(unsigned index)
{
	lock_guard<FastSpinLock> lock(m_lock);

	auto conn = m_connections[index];

	CCASSERT(conn);

	return conn;
}

} // namespace CCServer