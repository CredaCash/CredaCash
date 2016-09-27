/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * connection.cpp
*/

#include "CCdef.h"
#include "connection.hpp"
#include "connection_manager.hpp"
#include "connection_registry.hpp"
#include "socks.hpp"
#include "CCutil.h"

#include <iostream>
#include <vector>
#include <boost/bind.hpp>

#include <unistd.h>

#define TRACE_CCSERVER_OPSCOUNT		0

#define TOR_TIMEOUT	120	// in seconds

//#define TEST_DELAY_CONN_RELEASE			15	// for testing
//#define TEST_RANDOM_VALIDATION_FAILURES		7	// for testing

#ifndef TEST_RANDOM_VALIDATION_FAILURES
#define TEST_RANDOM_VALIDATION_FAILURES		0	// don't test
#endif

#ifndef TEST_DELAY_CONN_RELEASE
#define TEST_DELAY_CONN_RELEASE				0	// don't test
#endif

using namespace std;

namespace CCServer {

ConnectionFactory::ConnectionFactory(unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister)
	:	m_conn_nreadbuf(conn_nreadbuf),
		m_conn_nwritebuf(conn_nwritebuf),
		m_sock_nreadbuf(sock_nreadbuf),
		m_sock_nwritebuf(sock_nwritebuf),
		m_headersize(headersize),
		m_noclose(noclose),
		m_register(bregister)
{
	if (m_sock_nreadbuf == 0)
		m_sock_nreadbuf = conn_nreadbuf;
	if (m_sock_nreadbuf > 8*4096)
		m_sock_nreadbuf = 8*4096;

	if (m_sock_nwritebuf == 0)
		m_sock_nwritebuf = conn_nwritebuf;
	if (m_sock_nwritebuf == 0)
		m_sock_nwritebuf = conn_nreadbuf;
	if (m_sock_nwritebuf > 8*4096)
		m_sock_nwritebuf = 8*4096;

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << "ConnectionFactory"
		<< " m_conn_nreadbuf " << m_conn_nreadbuf
		<< " m_conn_nwritebuf " << m_conn_nwritebuf
		<< " m_sock_nreadbuf " << m_sock_nreadbuf
		<< " m_sock_nwritebuf " << m_sock_nwritebuf
		<< " m_headersize " << m_headersize
		<< " m_noclose " << m_noclose
		<< " m_register " << m_register;
}

Connection::Connection(ConnectionManager& manager, boost::asio::io_service& io_service, const class ConnectionFactory& connfac)
	:	m_connection_manager(manager),
		m_conn_index(0),
		m_is_free(0),
		m_use_count(0),
		m_headersize(connfac.m_headersize),
		m_noclose(connfac.m_noclose),
		m_socket(io_service),
		m_incoming(0),
		m_readbuf(connfac.m_conn_nreadbuf),
		m_writebuf(connfac.m_conn_nwritebuf),
		m_pread(NULL),
		m_maxread(0),
		m_nred(0),
		m_terminated(0),
		m_timer(io_service),
		m_stopping(0),
		m_ops_pending(0)
{
	CCASSERT(m_headersize <= m_readbuf.size());

	CCASSERT(m_readbuf.size() == connfac.m_conn_nreadbuf);
	CCASSERT(m_writebuf.size() == connfac.m_conn_nwritebuf);

	CCASSERT(m_readbuf.capacity() == connfac.m_conn_nreadbuf);
	CCASSERT(m_writebuf.capacity() == connfac.m_conn_nwritebuf);

	m_write_in_progress.clear();

	if (connfac.m_register)
		m_conn_index = g_connregistry.RegisterConn(this);
}

const string& Connection::Name() const
{
	return m_connection_manager.Name();
}

void Connection::InitNewConnection()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::InitNewConnection use count " << m_use_count.load() << " pending ops " << m_ops_pending.load();

	CCASSERTZ(m_ops_pending.load());

	m_stopping.store(g_shutdown);
	m_write_in_progress.clear();
}

void Connection::ConnectOutgoing(const string& host, unsigned port)
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::ConnectOutgoing host " << host << " port " << port;

	InitNewConnection();

	auto op_pending = AcquireRef();
	if (!op_pending)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::ConnectOutgoing connection is closing";

		return;
	}

	auto dest = boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>(boost::asio::ip::address::from_string(host), port);

	boost::system::error_code e;

	m_socket.connect(dest, e);
	if (e)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Conn-" << m_conn_index << " connect failed error " << e << " " << e.message();

		return Stop();
	}

	return StartConnection();
}

void Connection::ConnectOutgoingTor(const string& host, unsigned proxy_port)
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::ConnectOutgoingTor host " << host << " torproxy port " << proxy_port;

	InitNewConnection();

	auto op_pending = AcquireRef();
	if (!op_pending)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::ConnectOutgoingTor connection is closing";

		return;
	}

	auto dest = Socks::ConnectPoint(proxy_port);

	boost::system::error_code e;

	m_socket.connect(dest, e);
	if (e)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Conn-" << m_conn_index << " torproxy connect failed error " << e << " " << e.message();

		return Stop();
	}

	string proxycmd = Socks::ConnectString(host);

	//proxycmd += "TEST";	// for testing

	//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " sending " << s2hex(proxycmd);

	//cerr << "proxycmd.size " << proxycmd.size() << " m_readbuf.size " << m_readbuf.size() << endl;

	auto msgbuf = SmartBuf(proxycmd.size() > SOCK_REPLY_SIZE ? proxycmd.size() : SOCK_REPLY_SIZE);
	if (!msgbuf)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " Connection::ConnectOutgoingTor msgbuf failed";

		return;
	}

	memcpy(msgbuf.data(), proxycmd.data(), proxycmd.size());

	WriteAsync("Connection::ConnectOutgoingTor", boost::asio::buffer(msgbuf.data(), proxycmd.size()),
			boost::bind(&Connection::HandleTorProxyWrite, this, boost::asio::placeholders::error, msgbuf, AutoCount(this)));

	auto op_counter = AutoCount();
	AsyncTimerWait("Connection::ConnectOutgoingTor", TOR_TIMEOUT * 1000, boost::bind(&Connection::HandleTorTimeout, this, boost::asio::placeholders::error, op_counter), op_counter);
}

void Connection::HandleTorTimeout(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorTimeout e = " << e << " " << e.message();

	if (e == boost::asio::error::operation_aborted)
		return;

	BOOST_LOG_TRIVIAL(warning) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorTimeout timeout e = " << e << " " << e.message();

	return Stop();
}

void Connection::HandleTorProxyWrite(const boost::system::error_code& e, SmartBuf msgbuf, AutoCount pending_op_counter)
{
	m_write_in_progress.clear();

	bool sim_err = ((TEST_RANDOM_WRITE_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorProxyWrite simulating write error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorProxyWrite after error " << e << " " << e.message();

		return Stop();
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorProxyWrite ok";

	ReadAsync("Connection::HandleTorProxyWrite", boost::asio::buffer(msgbuf.data(), SOCK_REPLY_SIZE), boost::asio::transfer_exactly(SOCK_REPLY_SIZE),
				boost::bind(&Connection::HandleTorProxyRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, msgbuf, AutoCount(this)));
}

void Connection::HandleTorProxyRead(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf msgbuf, AutoCount pending_op_counter)
{
	CancelTimer();

	CCASSERT(bytes_transferred <= SOCK_REPLY_SIZE);

	bool sim_err = ((TEST_RANDOM_READ_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorProxyRead simulating read error";

	if (e || !bytes_transferred || sim_err)
	{
		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorProxyRead error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << SOCK_REPLY_SIZE << " bytes";

		return Stop();
	}

	if (bytes_transferred < SOCK_REPLY_SIZE)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorProxyRead torproxy returned " << bytes_transferred << " bytes";

		return Stop();
	}

	if (msgbuf.data()[1] != 90)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorProxyRead torproxy status " << (unsigned)msgbuf.data()[1];

		return Stop();
	}

	BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleTorProxyRead ok";

	StartConnection();
}

void Connection::StartIncomingConnection()
{
	if (m_noclose) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::StartIncomingConnection";
	if (TRACE_CCSERVER && !m_noclose) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::StartIncomingConnection";

	InitNewConnection();

	auto op_pending = AcquireRef();
	if (!op_pending)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::StartIncomingConnection connection is closing";

		return;
	}

	StartConnection();
}

void Connection::StartConnection()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::StartConnection";

	StartRead();
}

void Connection::StartRead()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::StartRead";

	m_pread = m_readbuf.data();
	m_nred = 0;

	if (m_headersize)
	{
		m_terminated = false;
		m_maxread = m_headersize;
	}
	else
	{
		m_terminated = true;
		m_maxread = m_readbuf.size();
	}

	QueueRead(m_terminated ? 1 : m_headersize);
}

void Connection::QueueRead(unsigned minbytes)
{
	CCASSERT(minbytes);	// seems to work fine if this is zero, but let's enforce anyway

	CCASSERT(m_nred < m_maxread);

	if (m_terminated)
	{
		//if (TRACE_CCSERVER_RW) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::QueueRead m_pread " << (uintptr_t)m_pread << " m_nred " << m_nred << " m_maxread " << m_maxread << " ReadAsync transfer_at_least " << minbytes;

		ReadAsync("Connection::QueueRead", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_at_least(minbytes),
			boost::bind(&Connection::HandleReadCheck, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, AutoCount(this)));
	}
	else
	{
		//if (TRACE_CCSERVER_RW) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::QueueRead m_pread " << (uintptr_t)m_pread << " m_nred " << m_nred << " m_maxread " << m_maxread << " ReadAsync transfer_exactly " << minbytes;

		ReadAsync("Connection::QueueRead", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(minbytes),
			boost::bind(&Connection::HandleReadCheck, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, AutoCount(this)));
	}
}

void Connection::HandleReadCheck(const boost::system::error_code& e, size_t bytes_transferred, AutoCount pending_op_counter)
{
	m_nred += bytes_transferred;

	CCASSERT(m_nred <= m_maxread);

	bool sim_err = ((TEST_RANDOM_READ_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " Connection::HandleReadCheck simulating read error";

	if (e || !bytes_transferred || sim_err)
	{
		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::HandleReadCheck error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

		return Stop();
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleReadCheck " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

	HandleRead(bytes_transferred);
}

void Connection::HandleRead(size_t bytes_transferred)
{
	if (!m_terminated)
	{
		if (m_nred >= m_maxread)
			HandleReadComplete();
		else
			QueueRead(m_maxread - m_nred);
	}
	else
	{
		for (unsigned i = m_nred - bytes_transferred; i < m_nred; ++i)
		{
			if (!m_pread[i])
			{
				return HandleReadComplete();
			}
		}

		if (m_nred >= m_maxread)
		{
			BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::HandleRead buffer overflow";

			return Stop();
		}

		QueueRead(1);
	}
}

void Connection::HandleWriteSmartBuf(const boost::system::error_code& e, SmartBuf buf, AutoCount pending_op_counter)
{
	Connection::HandleWrite(e, AutoCount());	// don't need to increment op count
}

void Connection::HandleWrite(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	m_write_in_progress.clear();

	bool sim_err = ((TEST_RANDOM_WRITE_ERRORS & rand()) == 1);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " Connection::HandleWrite simulating write error";

	if (e || sim_err)
	{
		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::HandleWrite after error " << e << " " << e.message();

		return Stop();
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleWrite ok";

	if (!m_noclose)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleWrite closing connection";

		{
			lock_guard<FastSpinLock> lock(m_conn_lock);

			// Initiate graceful Connection closure.
			boost::system::error_code ignored_ec;
			m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
		}

		Stop();
	}
}

void Connection::HandleValidateDone(unsigned callback_id, int64_t result)
{
	// calling this function is completely asynchronous and can occur long after a connection is closed and reopened, or while its closing or reopening

	lock_guard<FastSpinLock> lock(m_conn_lock);

	if (callback_id != m_use_count.load())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " Connection::HandleValidateDone ignoring late or unexpected callback id " << callback_id << " use count " << m_use_count.load();

		return;
	}

	if (result > PROCESS_RESULT_STOP_THRESHOLD)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleValidateDone result " << result;

		if ((TEST_RANDOM_VALIDATION_FAILURES & rand()) == 1)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " Connection::HandleValidateDone simulating validation failure";

			StopWithConnLock();
		}
	}
	else
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleValidateDone result " << result << " stopping connection";

		StopWithConnLock();
	}
}

void Connection::CancelTimer()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::CancelTimer " << uintptr_t(this);

	lock_guard<FastSpinLock> lock(m_conn_lock);

	boost::system::error_code e;
	m_timer.cancel(e);
}

AutoCount Connection::AcquireRef()
{
	if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::AcquireRef";

	return AutoCount(this);
}

bool Connection::IncRef()
{
	lock_guard<FastSpinLock> lock(m_conn_lock);

	if (g_shutdown || m_stopping.load())
	{
		if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::IncRef connection is stopping";

		return true;
	}

	auto ops = m_ops_pending.fetch_add(1);

	if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::IncRef pending ops increment to " << ops + 1;

	return false;
}

void Connection::DecRef()
{
	lock_guard<FastSpinLock> lock(m_conn_lock);

	auto ops = m_ops_pending.fetch_sub(1);

	if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::DecRef pending ops decrement to " << ops - 1;

	CCASSERT(ops);

	if (ops == 1 && m_stopping.load())
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::DecRef posting stop";

		m_socket.get_io_service().post(boost::bind(&Connection::HandleStop, this));
	}
}

void Connection::Stop()
{
	lock_guard<FastSpinLock> lock(m_conn_lock);

	StopWithConnLock();
}

void Connection::StopWithConnLock()
{
	// call this if the caller already holds m_conn_lock

	auto already_stopping = m_stopping.fetch_add(1);

	if (!already_stopping)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::Stop canceling async ops";

		boost::system::error_code e;
		m_timer.cancel(e);
		m_socket.cancel(e);
	}

	auto pending_ops = m_ops_pending.load();

	if (pending_ops)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::Stop postponing stop for " << pending_ops << " pending ops";
	}
	else
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::Stop posting stop";

		m_socket.get_io_service().post(boost::bind(&Connection::HandleStop, this));
	}
}

void Connection::HandleStop()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::HandleStop closing connection";

	CCASSERT(m_stopping.load());

	m_stopping.store(-77);

	if ((TEST_DELAY_CONN_RELEASE & rand()) == 1) sleep(1);

	lock_guard<FastSpinLock> lock(m_conn_lock);

	if ((TEST_DELAY_CONN_RELEASE & rand()) == 1) sleep(1);	// test delay both before and after acquiring m_conn_lock

	CCASSERTZ(m_ops_pending.load());

	//dump_socket_opts(m_socket.native_handle());

	if (m_socket.is_open())
		m_socket.close();

	m_use_count.fetch_add(1);		// ignore anymore callbacks with former m_use_count value

	if (m_noclose) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn-" << m_conn_index << " Connection::Stop done";
	if (TRACE_CCSERVER && !m_noclose) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::Stop done";

	FinishConnection();

	CCASSERTZ(m_ops_pending.load());

	m_stopping.store(-99);			// done stopping

	m_connection_manager.FreeConnection(this);
}

void Connection::WaitForStop()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " Connection::WaitForStop";

	CCASSERT(m_stopping.load());

	while (m_stopping.load() != -99)
		usleep(100);
}

} // namespace CCServer
