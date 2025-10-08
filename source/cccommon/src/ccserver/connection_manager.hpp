/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * connection_manager.hpp
*/

#pragma once

#include <vector>
#include <boost/noncopyable.hpp>

#include <SpinLock.hpp>
#include "../ccserver/connection.hpp"

using namespace std;

namespace CCServer {

class Server;

/// Manages a pool of connections

class ConnectionManagerBase
	: private boost::noncopyable
{
	const string m_name;

public:

	const string& Name() const
	{
		return m_name;
	}

	ConnectionManagerBase(const string& name)
	 :	m_name(name)
	{ }

	virtual ~ConnectionManagerBase() = default;

	virtual void FreeConnection(pconnection_t connection)
	{ }
};

class ConnectionManager : public ConnectionManagerBase
{
public:
	ConnectionManager(const string& name, boost::asio::io_service& io_service)
	 :	ConnectionManagerBase(name),
		m_io_service(io_service),
		m_free_callback_obj(NULL),
		m_maxincoming(0),
		m_incoming_count(0),
		m_conn_mgr_lock(__FILE__, __LINE__)
	{ }

	~ConnectionManager();

	void Init(unsigned maxconns, unsigned maxincoming, const class ConnectionFactory& connfac);
	void DeInit();

	/// Register Server object to receive notification when a Connection becomes free
	void SetFreeConnectionHandler(Server *p);

	pconnection_t GetFreeConnection(bool incoming = false);

	unsigned GetOutgoingConnectionCount();

	/// Return Connection to free pool
	void FreeConnection(pconnection_t connection);

	/// Stop all connections
	void StopAllConnections();

protected:
	/// The io_service used to perform asynchronous operations.
	boost::asio::io_service& m_io_service;

	// Server object to notify on free connection
	Server *m_free_callback_obj;

	/// The managed connections.
	vector<Connection *> m_connections;

	/// Free connections
	vector<Connection *> m_free_connections;

	unsigned m_maxincoming;
	unsigned m_incoming_count;
	FastSpinLock m_conn_mgr_lock;
};

void set_int_opt(int sockfd, int level, int optname, int opt);
int get_int_opt(int sockfd, int level, int optname);
void dump_socket_opts(int sockfd);

} // namespace CCServer
