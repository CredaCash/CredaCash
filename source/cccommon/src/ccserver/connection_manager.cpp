/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * connection_manager.cpp
*/

#include "CCdef.h"
#include "connection_manager.hpp"
#include "server.hpp"

#include <algorithm>
#include <iostream>
#ifdef _WIN32
#include <Winsock2.h>
#else
#include <sys/socket.h>
#endif

using namespace std;

namespace CCServer {

ConnectionManager::~ConnectionManager()
{
	for (auto connection : m_connections)
		delete connection;
}

void ConnectionManager::Init(unsigned maxconns, unsigned maxincoming, const class ConnectionFactory &connfac)
{
	CCASSERT(m_connections.empty());
	CCASSERT(m_free_connections.empty());

	m_connections.reserve(maxconns);
	m_free_connections.reserve(maxconns);

	m_maxincoming = maxincoming;

	for (unsigned i = 0; i < maxconns; ++i)
	{
		pconnection_t connection = connfac.NewConnection(*this, m_io_service);
		m_connections.push_back(connection);
		FreeConnection(connection);
	}
}

void ConnectionManager::FreeConnection(pconnection_t connection)
{
	if (g_shutdown)
		return;

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << connection->m_conn_index << " ConnectionManager::FreeConnection";

	bool was_freed = false;

	{
		lock_guard<FastSpinLock> lock(m_lock);

		if (!connection->m_is_free)
		{
			connection->m_is_free = true;
			was_freed = true;

			m_free_connections.push_back(connection);

			if (connection->m_incoming)
				--m_incoming_count;
		}
	}

	if (was_freed && m_free_callback_obj)
		m_free_callback_obj->HandleFreeConnection();
}

void ConnectionManager::SetFreeConnectionHandler(Server *p)
{
	m_free_callback_obj = p;
}

pconnection_t ConnectionManager::GetFreeConnection(bool incoming)
{
	pconnection_t connection;

	{
		lock_guard<FastSpinLock> lock(m_lock);

		if (!m_free_connections.size())
			return NULL;

		if (incoming)
		{
			if (m_incoming_count >= m_maxincoming)
				return NULL;

			++m_incoming_count;
		}

		connection = m_free_connections.back();
		m_free_connections.pop_back();

		connection->m_is_free = false;
	}

	connection->m_incoming = incoming;	// need to set this now to keep count of incoming and outgoing connections

	return connection;
}

unsigned ConnectionManager::GetOutgoingConnectionCount()
{
	lock_guard<FastSpinLock> lock(m_lock);

	//cerr << Name() << " m_connections.size() " << m_connections.size() << " m_free_connections.size() " << m_free_connections.size() << " m_incoming_count " << m_incoming_count << endl;

	return m_connections.size() - m_free_connections.size() - m_incoming_count;
}

void ConnectionManager::StopAllConnections()
{
	for (auto connection : m_connections)
	{
		connection->Stop();
		connection->WaitForStop();
	}
}

void set_int_opt(int sockfd, int level, int optname, int opt)
{
	socklen_t optlen = sizeof(int);
	setsockopt(sockfd, level, optname, (char*)&opt, optlen);
}

int get_int_opt(int sockfd, int level, int optname)
{
	int opt = 0xff;
	socklen_t optlen = sizeof(int);
	getsockopt(sockfd, level, optname, (char*)&opt, &optlen);

	return opt;
}

void dump_socket_opts(int sockfd)
{
	cout << endl;
	cout << "socket # " << sockfd << endl;

	socklen_t optlen;

	struct linger lopt;
	lopt.l_onoff = -1;
	lopt.l_linger = -1;
	optlen = sizeof(lopt);
	getsockopt(sockfd, SOL_SOCKET, SO_LINGER, (char*)&lopt, &optlen);
	cout << "SO_LINGER " << lopt.l_onoff << " seconds " << lopt.l_linger << endl;

#ifdef _WIN32
	cout << "SO_DONTLINGER " << get_int_opt(sockfd, SOL_SOCKET,		SO_DONTLINGER) << endl;
#endif
	cout << "SO_RCVBUF " << get_int_opt(sockfd, SOL_SOCKET,			SO_RCVBUF) << endl;
	cout << "SO_SNDBUF " << get_int_opt(sockfd, SOL_SOCKET,			SO_SNDBUF) << endl;
	cout << "SO_RCVLOWAT " << get_int_opt(sockfd, SOL_SOCKET,		SO_RCVLOWAT) << endl;
	cout << "SO_SNDLOWAT " << get_int_opt(sockfd, SOL_SOCKET,		SO_SNDLOWAT) << endl;
	cout << "SO_RCVTIMEO " << get_int_opt(sockfd, SOL_SOCKET,		SO_RCVTIMEO) << endl;
	cout << "SO_SNDTIMEO " << get_int_opt(sockfd, SOL_SOCKET,		SO_SNDTIMEO) << endl;
	cout << "SO_REUSEADDR " << get_int_opt(sockfd, SOL_SOCKET,		SO_REUSEADDR) << endl;
#ifdef _WIN32
	cout << "SO_EXCLUSIVEADDRUSE " << get_int_opt(sockfd, SOL_SOCKET,SO_EXCLUSIVEADDRUSE) << endl;
#else
	cout << "SO_REUSEPORT " << get_int_opt(sockfd, SOL_SOCKET,		SO_REUSEPORT) << endl;

	cout << "IP_MTU_DISCOVER " << get_int_opt(sockfd, IPPROTO_IP,	IP_MTU_DISCOVER) << endl;

	cout << "TCP_CORK " << get_int_opt(sockfd, IPPROTO_TCP,			TCP_CORK) << endl;
	cout << "TCP_DEFER_ACCEPT " << get_int_opt(sockfd, IPPROTO_TCP,	TCP_DEFER_ACCEPT) << endl;
	cout << "TCP_LINGER2 " << get_int_opt(sockfd, IPPROTO_TCP,		TCP_LINGER2) << endl;
	cout << "TCP_QUICKACK " << get_int_opt(sockfd, IPPROTO_TCP,		TCP_QUICKACK) << endl;
#endif
	//cout << "TCP_MAXRT " << get_int_opt(sockfd, IPPROTO_TCP,		TCP_MAXRT) << endl;
	cout << "TCP_NODELAY " << get_int_opt(sockfd, IPPROTO_TCP,		TCP_NODELAY) << endl;

	cout << endl;
}


} // namespace CCServer