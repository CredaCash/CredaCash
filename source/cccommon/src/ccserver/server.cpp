/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * server.cpp
*/

#include "CCdef.h"
#include "server.hpp"

#include <iostream>
#include <boost/bind.hpp>
#include <signal.h>

using namespace std;

namespace CCServer {

void Server::Init(const boost::asio::ip::tcp::endpoint& endpoint, unsigned maxconns, unsigned maxincoming, unsigned backlog, const class ConnectionFactory &connfac)
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::Init maxconns " << maxconns << " maxincoming " << maxincoming << " port " << endpoint.port() << " backlog " << backlog;

	// Register to handle the signals that indicate when the CCServer should exit.
	// It is safe to register for the same signal multiple times in a program,
	// provided all registration for the specified signal is made through Asio.
	m_signals.add(SIGINT);
	m_signals.add(SIGTERM);
	#if defined(SIGQUIT)
	m_signals.add(SIGQUIT);
	#endif
	m_signals.async_wait(boost::bind(&Server::HandleStop, this));

	m_connection_manager.Init(maxconns, maxincoming, connfac);
	m_connection_manager.SetFreeConnectionHandler(this);

	if (!endpoint.port() || !maxincoming)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Server::Init server not listening since port = " << endpoint.port() << " and maxincoming = " << maxincoming;

		return;
	}

	m_acceptor.open(endpoint.protocol());

	// !!! note: also need to check /proc/sys/net/ipv4/tcp_rmem and /proc/sys/net/ipv4/tcp_wmem

	m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

#ifndef _WIN32
	set_int_opt(m_acceptor.native_handle(), SOL_SOCKET, SO_REUSEPORT, 1);
	set_int_opt(m_acceptor.native_handle(), IPPROTO_IP, IP_MTU_DISCOVER, 0);
	set_int_opt(m_acceptor.native_handle(), IPPROTO_TCP, TCP_DEFER_ACCEPT, 1);
	if (!connfac.m_noclose)
		set_int_opt(m_acceptor.native_handle(), IPPROTO_TCP, TCP_CORK, 1);
	set_int_opt(m_acceptor.native_handle(), IPPROTO_TCP, TCP_LINGER2, 1);
#endif

	//set_int_opt(m_acceptor.native_handle(), IPPROTO_TCP, TCP_MAXRT, 40);

	if (connfac.m_noclose)
		set_int_opt(m_acceptor.native_handle(), IPPROTO_TCP, TCP_NODELAY, 1);

	m_acceptor.set_option(boost::asio::socket_base::receive_buffer_size(connfac.m_sock_nreadbuf));
	m_acceptor.set_option(boost::asio::socket_base::send_buffer_size(connfac.m_sock_nwritebuf));

	//dump_socket_opts(m_acceptor.native_handle());

	m_acceptor.bind(endpoint);

	if (backlog < 1)
		backlog = 0x7fff;

	m_acceptor.listen(backlog);

	StartAccept();
}

void Server::Run()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::Run";

	// The io_service::run() call will block until all asynchronous operations
	// have finished. While the server is running, there is always at least one
	// asynchronous operation outstanding: the asynchronous accept call waiting
	// for new incoming connections.

	m_io_service.run();
}

void Server::StartAccept(bool m_new_connection_has_been_used)
{
	lock_guard<FastSpinLock> lock(m_new_connection_lock);

	if (!m_acceptor.is_open())
	{
		//BOOST_LOG_TRIVIAL(info) << Name() << " Server::StartAccept acceptor is closed";

		return;
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept last new connection " << m_new_connection << ", used = " << m_new_connection_has_been_used;

	if (m_new_connection && !m_new_connection_has_been_used)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept already accepting";

		return;
	}

	if (!m_new_connection || m_new_connection_has_been_used)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept fetching new connection";

		m_new_connection = m_connection_manager.GetFreeConnection(true);
	}

	if (!m_new_connection)
	{
		#define USE_TEMP_HACK_FOR_WIN32_SOCKETS 0
		#if USE_TEMP_HACK_FOR_WIN32_SOCKETS
		// this hack allows other threads to accept connections without SO_REUSEPORT (i.e., Windows), but when the threads run out, no more connections will be accepted
		m_acceptor.close();
		BOOST_LOG_TRIVIAL(warning) << Name() << " Server::StartAccept WARNING: temporary hack--listening socket closed--when all listening sockets closed, the server will stop working...";
		#endif

		BOOST_LOG_TRIVIAL(debug) << Name() << " Server::StartAccept no connection available";

		return;
	}

	m_acceptor.async_accept(m_new_connection->m_socket, boost::bind(&Server::HandleAccept, this, boost::asio::placeholders::error));

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_new_connection->m_conn_index << " Server::StartAccept now accepting";
}

void Server::HandleAccept(const boost::system::error_code& e)
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::HandleAccept";

	{
		lock_guard<FastSpinLock> lock(m_new_connection_lock);

		// Check whether the server was stopped by a signal before this completion
		// handler had a chance to run.
		if (!m_acceptor.is_open())
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Server::HandleAccept acceptor is closed";

			return;
		}
	}

	if (e)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_new_connection->m_conn_index << " Server::HandleAccept after error " << e << " " << e.message();

		StartAccept();
	}
	else
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_new_connection->m_conn_index << " Server::HandleAccept starting connection";

		m_new_connection->Post(boost::bind(&Connection::StartIncomingConnection, m_new_connection));

		StartAccept(true);
	}
}

void Server::HandleFreeConnection()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::HandleFreeConnection";

	StartAccept();
}

pconnection_t Server::Connect(const string& host, unsigned port)
{
	auto connection = m_connection_manager.GetFreeConnection(false);

	if (!connection)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Server::Connect no connection available";

		return NULL;
	}

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << connection->m_conn_index << " Server::Connect " << host;

	connection->Post(boost::bind(&Connection::ConnectOutgoing, connection, host, port));

	return connection;
}

pconnection_t Server::ConnectThruTor(const string& host, unsigned proxy_port)
{
	auto connection = m_connection_manager.GetFreeConnection(false);

	if (!connection)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Server::ConnectThruTor no connection available";

		return NULL;
	}

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << connection->m_conn_index << " Server::ConnectThruTor " << host;

	connection->Post(boost::bind(&Connection::ConnectOutgoingTor, connection, host, proxy_port));

	return connection;
}

void Server::HandleStop()
{
	BOOST_LOG_TRIVIAL(info) << Name() << " Server::HandleStop shutting down...";

	g_shutdown = true;

	{
		lock_guard<FastSpinLock> lock(m_new_connection_lock);

		// The server is stopped by cancelling all outstanding asynchronous
		// operations. Once all operations have finished the io_service::run() call
		// will exit.
		m_acceptor.close();
	}

	m_connection_manager.StopAllConnections();
}

} // namespace CCServer
