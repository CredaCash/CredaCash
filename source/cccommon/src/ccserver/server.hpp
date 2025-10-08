/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * server.hpp
*/

#pragma once

#include <SpinLock.hpp>

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>

#include <string>
#include "../ccserver/connection.hpp"
#include "../ccserver/connection_manager.hpp"

using namespace std;

namespace CCServer {

class Server
	: private boost::noncopyable
{
	const string m_name;

public:

	const string& Name() const
	{
		return m_name;
	}

	/// Construct the Server
	explicit Server(const string& name)
	 :	m_name(name),
		m_startup_backlog(0),
		m_stopping(0),
		m_io_service(),
		m_signals(m_io_service),
		m_acceptor(m_io_service),
		m_connection_manager(name, m_io_service),
		m_new_connection()
	{ }

	// Setup server
	void Init(const boost::asio::ip::tcp::endpoint& endpoint, unsigned maxconns, unsigned maxincoming, unsigned backlog, const class ConnectionFactory& connfac);
	void DeInit();

	ConnectionManager& GetConnectionManager()
	{
		return m_connection_manager;
	}

	/// Run the server's io_service loop.
	void Run();

	atomic<int> m_startup_backlog;

	/// Make outgoing connection
	pconnection_t Connect(const string& host, unsigned port, bool use_tor = false);

	/// Respond when a Connection becomes free
	void HandleFreeConnection();

	void AsyncStop();

private:
	/// Initiate an asynchronous accept operation.
	void StartAccept(bool new_connection_used = false);
	void StartAcceptWithLock(bool new_connection_used);

	/// Handle completion of an asynchronous accept operation.
	void HandleAccept(const boost::system::error_code& e);

	/// Handle Signals
	void HandleSignals();

	/// Handle a request to stop the server.
	void HandleStop();
	atomic<int> m_stopping;				// prevent multiple stops

	/// The io_service used to perform asynchronous operations.
	boost::asio::io_service m_io_service;

	/// The signal_set is used to register for process termination notifications.
	boost::asio::signal_set m_signals;

	/// Acceptor used to listen for incoming connections.
	boost::asio::ip::tcp::acceptor m_acceptor;

	/// The Connection manager which owns all live connections.
	ConnectionManager m_connection_manager;

	/// The next Connection to be accepted.
	pconnection_t m_new_connection;
	mutex m_new_connection_lock;
};

} // namespace CCServer
