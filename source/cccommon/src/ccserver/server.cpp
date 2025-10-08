/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * server.cpp
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "server.hpp"

bool g_trace_ccserver = false;

namespace CCServer {

void Server::DeInit()
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " Server::DeInit";

	m_connection_manager.DeInit();

	BOOST_LOG_TRIVIAL(trace) << Name() << " Server::DeInit done";
}

void Server::Init(const boost::asio::ip::tcp::endpoint& endpoint, unsigned maxconns, unsigned maxincoming, unsigned backlog, const class ConnectionFactory& connfac)
{
	BOOST_LOG_TRIVIAL(trace) << Name() << " Server::Init maxconns " << maxconns << " maxincoming " << maxincoming << " port " << endpoint.port() << " backlog " << backlog;

	lock_guard<mutex> lock(m_new_connection_lock);	// not really needed, but doesn't hurt

	m_stopping = false;

	// Register to handle the signals that indicate when the CCServer should exit.
	// It is safe to register for the same signal multiple times in a program,
	// provided all registration for the specified signal is made through Asio.
	#if 0 // no longer using asio to trap signals for shutdown
	m_signals.add(SIGINT);
	m_signals.add(SIGTERM);
	#if defined(SIGQUIT)
	m_signals.add(SIGQUIT);
	#endif
	#endif

	// this async_wait prevents io_service::run() from terminating early
	m_signals.async_wait(boost::bind(&Server::HandleSignals, this));

	m_connection_manager.Init(maxconns, maxincoming, connfac);
	m_connection_manager.SetFreeConnectionHandler(this);

	if (!endpoint.port() || !maxincoming)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Server::Init server not listening since port = " << endpoint.port() << " and maxincoming = " << maxincoming;

		return;
	}

	m_acceptor.open(endpoint.protocol());

	// !!! note: also need to check /proc/sys/net/ipv4/tcp_rmem and /proc/sys/net/ipv4/tcp_wmem

	// SO_REUSEADDR allows the server to be restarted if a prior instance was killed and the port state isn't clean
	// but it is not used because it allows two instances of the server to run at the same time
	//set_int_opt(m_acceptor.native_handle(), SOL_SOCKET, SO_REUSEADDR, 1);

#ifdef _WIN32
	// SO_EXCLUSIVEADDRUSE prevents another application from using the same port
	set_int_opt(m_acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
	// SO_REUSEPORT isn't used because it allows two instances of the server to run at the same time
	//set_int_opt(m_acceptor.native_handle(), SOL_SOCKET, SO_REUSEPORT, 1);
	set_int_opt(m_acceptor.native_handle(), IPPROTO_IP, IP_MTU_DISCOVER, 0);
	set_int_opt(m_acceptor.native_handle(), IPPROTO_TCP, TCP_DEFER_ACCEPT, 1);
	//if (!connfac.m_noclose)
	//	set_int_opt(m_acceptor.native_handle(), IPPROTO_TCP, TCP_CORK, 1);
	set_int_opt(m_acceptor.native_handle(), IPPROTO_TCP, TCP_LINGER2, 1);
#endif

	struct linger lopt;
	memset(&lopt, 0, sizeof(lopt));
	lopt.l_onoff = 1;
	lopt.l_linger = 15;
	setsockopt(m_acceptor.native_handle(), SOL_SOCKET, SO_LINGER, (char*)&lopt, sizeof(lopt));

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
}

void Server::Run()
{
	m_startup_backlog--;

	BOOST_LOG_TRIVIAL(trace) << Name() << " Server::Run startup backlog " << m_startup_backlog.load();

	// The io_service::run() call will block until all asynchronous operations
	// have finished.

	m_io_service.run();

	BOOST_LOG_TRIVIAL(trace) << Name() << " Server::Run done";
}

void Server::StartAccept(bool new_connection_used)
{
	if (!m_acceptor.is_open())
	{
		//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept acceptor is closed";

		return;
	}

	//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept acquiring m_new_connection_lock...";

	lock_guard<mutex> lock(m_new_connection_lock);

	StartAcceptWithLock(new_connection_used);
}

void Server::StartAcceptWithLock(bool new_connection_used)
{
	if (!m_acceptor.is_open())
	{
		//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept acceptor is closed";

		return;
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept last new connection Conn " << (m_new_connection ? m_new_connection->m_conn_index : 0) << ", used = " << new_connection_used;

	if (m_new_connection && !new_connection_used)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept already accepting";

		return;
	}

	if (!m_new_connection || new_connection_used)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::StartAccept fetching new connection";

		m_new_connection = m_connection_manager.GetFreeConnection(true);
	}

	if (!m_new_connection)
	{
		#define USE_TEMP_HACK_FOR_WIN32_SOCKETS 0
		#if USE_TEMP_HACK_FOR_WIN32_SOCKETS
		// this hack allows other threads to accept connections without SO_REUSEPORT (i.e., Windows), but when the threads run out, no more connections will be accepted
		boost::system::error_code ec;
		m_acceptor.close(ec);
		BOOST_LOG_TRIVIAL(warning) << Name() << " Server::StartAccept WARNING: temporary hack--listening socket closed--when all listening sockets closed, the server will stop working...";
		#endif

		BOOST_LOG_TRIVIAL(debug) << Name() << " Server::StartAccept no connection available";

		return;
	}

	m_acceptor.async_accept(m_new_connection->m_socket, boost::bind(&Server::HandleAccept, this, boost::asio::placeholders::error));

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_new_connection->m_conn_index << " Server::StartAccept now accepting";
}

void Server::HandleAccept(const boost::system::error_code& e)
{
	if (e) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_new_connection->m_conn_index << " Server::HandleAccept after error " << e << " " << e.message();
	else if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::HandleAccept";

	lock_guard<mutex> lock(m_new_connection_lock);

	// Check whether the server was stopped by a signal before this completion
	// handler had a chance to run.
	if (!m_acceptor.is_open())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Server::HandleAccept acceptor is closed";

		return;
	}

	if (e)
		StartAcceptWithLock(false);
	else
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_new_connection->m_conn_index << " Server::HandleAccept starting connection";

		m_new_connection->InitNewConnection();

		m_new_connection->Post("Server::HandleAccept", boost::bind(&Connection::HandleStartIncomingConnection, m_new_connection, AutoCount(m_new_connection)));

		StartAcceptWithLock(true);
	}
}

void Server::HandleFreeConnection()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Server::HandleFreeConnection";

	StartAccept();
}

pconnection_t Server::Connect(const string& host, unsigned port, bool use_tor)
{
	auto connection = m_connection_manager.GetFreeConnection();

	if (!connection)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Server::Connect no connection available";

		return NULL;
	}

	BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << connection->m_conn_index << " Server::Connect " << host;

	connection->InitNewConnection();

	int rc;
	static const string null;
	if (use_tor)
		rc = connection->Post("Server::Connect", boost::bind(&Connection::HandleConnectOutgoingTor, connection, port, host, ref(null), AutoCount(connection)));
	else
		rc = connection->Post("Server::Connect", boost::bind(&Connection::HandleConnectOutgoing, connection, host, port, AutoCount(connection)));
	if (rc)
	{
		BOOST_LOG_TRIVIAL(trace) << Name() << " Server::Connect post failed; m_stopping " << m_stopping.load();

		connection->FreeConnection();

		return NULL;
	}

	return connection;
}

void Server::HandleSignals()
{
	BOOST_LOG_TRIVIAL(info) << Name() << " Server::HandleSignals";

	start_shutdown();
}

void Server::AsyncStop()
{
	BOOST_LOG_TRIVIAL(info) << Name() << " Server::AsyncStop posting stop";

	m_io_service.post(boost::bind(&Server::HandleStop, this));
}

void Server::HandleStop()
{
	auto already_stopping = m_stopping.fetch_add(1);

	if (already_stopping)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Server::HandleStop already stopping " << already_stopping;

		return;
	}

	BOOST_LOG_TRIVIAL(info) << Name() << " Server::HandleStop shutting down...";

	{
		lock_guard<mutex> lock(m_new_connection_lock);

		// The server is stopped by cancelling all outstanding asynchronous
		// operations. Once all operations have finished the io_service::run() call
		// will exit.
		boost::system::error_code ec;
		m_signals.cancel(ec);
		m_acceptor.close(ec);
	}

	BOOST_LOG_TRIVIAL(info) << Name() << " Server::HandleStop closed acceptor";

	m_connection_manager.StopAllConnections();

	BOOST_LOG_TRIVIAL(info) << Name() << " Server::HandleStop done";
}

} // namespace CCServer
