/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * connection.cpp
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "connection.hpp"
#include "connection_manager.hpp"
#include "connection_registry.hpp"
#include "socks.hpp"

#define TRACE_CCSERVER_OPSCOUNT		0

//!#define RTEST_CUZZ_TIMER				32
//!#define RTEST_CUZZ_WAIT				(1024-1)
//!#define RTEST_TIMER_TIMEOUT			32

//!#define TEST_VALIDATION_FAIL_NO_STOP	1	// allows bad tx's to be propogated through network

#ifndef RTEST_CUZZ_TIMER
#define RTEST_CUZZ_TIMER				0	// don't test
#endif

#ifndef RTEST_CUZZ_WAIT
#define RTEST_CUZZ_WAIT					0	// don't test
#endif

#ifndef RTEST_TIMER_TIMEOUT
#define RTEST_TIMER_TIMEOUT				0	// don't test
#endif

#ifndef TEST_VALIDATION_FAIL_NO_STOP
#define TEST_VALIDATION_FAIL_NO_STOP	0	// don't test
#endif

#define DIRECT_TIMEOUT	20	// in seconds	// !!! make this a param
#define TOR_TIMEOUT		120	// in seconds	// !!! make this a param

using namespace std;

namespace CCServer {

ConnectionFactoryBase::ConnectionFactoryBase(unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister)
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

	if (0 && TRACE_CCSERVER)	// disabled so it doesn't output during static initialization
		BOOST_LOG_TRIVIAL(trace) << "ConnectionFactoryBase"
		<< " m_conn_nreadbuf " << m_conn_nreadbuf
		<< " m_conn_nwritebuf " << m_conn_nwritebuf
		<< " m_sock_nreadbuf " << m_sock_nreadbuf
		<< " m_sock_nwritebuf " << m_sock_nwritebuf
		<< " m_headersize " << m_headersize
		<< " m_noclose " << m_noclose
		<< " m_register " << m_register;
}

Connection::Connection(ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class ConnectionFactoryBase& connfac)
	:	m_conn_state(CONN_STOPPED),
		m_read_count(0),
		m_autofree(true),
		m_connection_manager(manager),
		m_conn_index(0),
		m_is_free(0),
		m_use_count(0),
		m_headersize(connfac.m_headersize),
		m_terminated(0),
		m_terminator(0),
		m_read_after_write(0),
		m_noclose(connfac.m_noclose),
		m_socket(io_service),
		m_incoming(0),
		m_readbuf(connfac.m_conn_nreadbuf),
		m_writebuf(connfac.m_conn_nwritebuf),
		m_pread(NULL),
		m_maxread(0),
		m_nred(0),
		m_timer(io_service),
		m_stopping(-9999),
		m_ops_pending(0)
{
	CCASSERT(m_headersize <= m_readbuf.size() - 1);

	CCASSERT(m_readbuf.size() == connfac.m_conn_nreadbuf);
	CCASSERT(m_writebuf.size() == connfac.m_conn_nwritebuf);

	CCASSERT(m_readbuf.capacity() == connfac.m_conn_nreadbuf);
	CCASSERT(m_writebuf.capacity() == connfac.m_conn_nwritebuf);

	m_conn_index = g_connregistry.RegisterConn(this, connfac.m_register);

	m_write_in_progress.clear();
}

const string& Connection::Name() const
{
	return m_connection_manager.Name();
}

void Connection::InitNewConnection()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::InitNewConnection use count " << m_use_count.load() << " pending ops " << m_ops_pending.load() << " state " << m_conn_state << " stopping " << m_stopping;

	CCASSERTZ(m_ops_pending.load());
	CCASSERT(m_stopping.load() < 0);
	CCASSERT(m_conn_state == CONN_STOPPED);

	m_conn_state = CONN_STOPPED;
	m_stopping.store(0);
	m_write_in_progress.clear();
	m_pread = m_readbuf.data();
	m_nred = 0;
}

/*
	For Post(), ReadAsync(), WriteAsync() and TimerWaitAsync():
	The caller creates an AutoCount object and passes it to the boost asio handler.
	This object prevents the connection from closing completely until the handler function
	has completed and the AutoCount object is destroyed.
	If the connection was closing when the calling function attempted to create the AutoCount object,
	the AutoCount IncRef will fail and the AutoCount will not hold a reference to the connection.
	The easiest way to test of this is for Post, ReadAsync, WriteAsync and TimerWaitAsync
	to test m_stopping and return true if m_stopping is set.  This indicates to the caller
	that the async operation was not started and it should abort or otherwise handle this condition.
	For comments on what happens when boost asio calls the handler, see the source code for CheckOpCount()
*/

bool Connection::IncRef()
{
	//if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::IncRef";

	// Stop() sets m_stopping, then checks m_ops_pending
	// if m_stopping is set, then m_ops_pending should not be incremented

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	//auto t0 = highres_ticks();

	lock_guard<mutex> lock(m_ref_lock);

	//auto dt = highres_ticks() - t0;
	//if (report_highres_ticks(dt))
	//	cout << "IncRef acquired m_ref_lock in " << dt << endl;

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	if (g_shutdown || m_stopping.load())
	{
		if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::IncRef connection is stopping";

		return true;
	}

	auto ops = m_ops_pending.fetch_add(1) + 1;

	if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::IncRef pending ops increment to " << ops;

	return false;
}

void Connection::DecRef()
{
	//if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::DecRef";

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	//auto t0 = highres_ticks();

	lock_guard<mutex> lock(m_ref_lock);

	//auto dt = highres_ticks() - t0;
	//if (report_highres_ticks(dt))
	//	cout << "DecRef acquired m_ref_lock in " << dt << endl;

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	auto ops = m_ops_pending.fetch_sub(1) - 1;

	if (TRACE_CCSERVER_OPSCOUNT) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::DecRef pending ops decrement to " << ops;

	CCASSERT(ops >= 0);

	if (m_stopping.load())
	{
		if (ops)
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::DecRef postponing stop for " << ops << " pending ops";
		}
		else
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::DecRef posting stop";

			PostDirect(boost::bind(&Connection::HandleStop, this));

			//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::DecRef posting stop done";
		}
	}
}

/*
	boost asio will copy the AutoCount object (and thereby call IncRef) before it calls an async handler function
	The IncRef will fail if the connection is stopping, and therefore pending_op_counter will be false.
	This will not cause the reference counting to fail however, because the AutoCount object passed in the call
	to boost asio will still exist and hold a valid reference, preventing connection close from completing
	until the AutoCount object is destroyed.

	As an optimization--and only as an optimization, a function can return immediately if pending_op_counter is false.
	This is only an optimization however because the connection can still start to close at any time.
	To test function operation when the connection is closing, RTEST_STOPS and/or RTEST_DELAY_CONN_RELEASE can be set
	either of which allows function execution to continue after a stop has been initiated
*/

bool Connection::CheckOpCount(AutoCount& pending_op_counter)
{
	if (RandTest(RTEST_STOPS))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::CheckOpCount simulating random stop";

		Stop();

		if (RandTest(2)) sleep(1);

		return false;	// continue execution
	}

	if (!pending_op_counter && RandTest(RTEST_DELAY_CONN_RELEASE ? 2 : 0))
		return true;

	return false;
}

bool Connection::Post(const char *function, PostHandler handler)
{
	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	lock_guard<mutex> lock(m_conn_lock);

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	if (g_shutdown || m_stopping.load())
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " skipping Post because connection is closing";

		return true;
	}

	PostDirect(handler);	// post is async, and may need a refcount to prevent Stop from running while the post is pending

	return false;
};

void Connection::PostDirect(PostHandler handler)
{
	#if BOOST_VERSION < 107000
	m_socket.get_io_service().post(handler);
	#else
	post(m_socket.get_executor(), handler);
	#endif
}

void Connection::HandleConnectOutgoing(const string& host, unsigned port, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " Connection::HandleConnectOutgoing connection is closing";

		return;
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleConnectOutgoing host " << host << " port " << port;

	// !!! TODO: close unregistered connections on shutdown so they don't hang up trying to connect?

	m_conn_state = CONN_CONNECTING;

	if (SetTimer(DIRECT_TIMEOUT))
		return;

	auto dest = boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>(boost::asio::ip::address::from_string(host), port);

	boost::system::error_code e;

	BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " synchronous connect host " << host << " port " << port << "...";

	m_socket.connect(dest, e);	// TODO: make connect async?

	BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " connected";

	if (e)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " connect failed host " << host << " error " << e << " " << e.message();

		return Stop();
	}

	return StartConnection();
}

void Connection::HandleConnectOutgoingTor(unsigned proxy_port, const string& host, const string& toruser, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " Connection::HandleConnectOutgoingTor connection is closing";

		return;
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleConnectOutgoingTor torproxy port " << proxy_port << " host " << host << " toruser " << toruser;

	m_conn_state = CONN_CONNECTING;

	if (SetTimer(TOR_TIMEOUT))
		return;

	auto dest = Socks::ConnectPoint(proxy_port);

	boost::system::error_code e;

	BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " torproxy synchronous connect localhost port " << proxy_port << "...";

	m_socket.connect(dest, e);	// !!! need to make this connect async?

	BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " torproxy connected";

	if (e)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " torproxy connect failed host " << host << " error " << e << " " << e.message();

		return Stop();
	}

	string proxycmd = Socks::ConnectString(host, toruser);

	//proxycmd += "TEST";	// for testing

	//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " sending " << s2hex(proxycmd);

	//cerr << "proxycmd.size " << proxycmd.size() << " m_readbuf.size " << m_readbuf.size() << endl;

	auto msgbuf = SmartBuf(proxycmd.size() > SOCKS_REPLY_SIZE ? proxycmd.size() : SOCKS_REPLY_SIZE);
	if (!msgbuf)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " Connection::HandleConnectOutgoingTor msgbuf failed";

		return;
	}

	memcpy(msgbuf.data(), proxycmd.data(), proxycmd.size());

	WriteAsync("Connection::HandleConnectOutgoingTor", boost::asio::buffer(msgbuf.data(), proxycmd.size()),
			boost::bind(&Connection::HandleTorProxyWrite, this, host, boost::asio::placeholders::error, msgbuf, AutoCount(this)));
}

void Connection::HandleTorProxyWrite(const string host, const boost::system::error_code& e, SmartBuf msgbuf, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	if (e) return Stop();

	m_write_in_progress.clear();

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleTorProxyWrite ok";

	ReadAsync("Connection::HandleTorProxyWrite", boost::asio::buffer(msgbuf.data(), SOCKS_REPLY_SIZE), boost::asio::transfer_exactly(SOCKS_REPLY_SIZE),
				boost::bind(&Connection::HandleTorProxyRead, this, host, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, msgbuf, AutoCount(this)));

	// timer set in HandleConnectOutgoingTor is still running...
}

void Connection::HandleTorProxyRead(const string host, const boost::system::error_code& e, size_t bytes_transferred, SmartBuf msgbuf, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	CCASSERT(bytes_transferred <= SOCKS_REPLY_SIZE);

	bool sim_err = RandTest(RTEST_READ_ERRORS);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::HandleTorProxyRead simulating read error";

	if (e || !bytes_transferred || sim_err)
	{
		BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " Connection::HandleTorProxyRead host " << host << " error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << SOCKS_REPLY_SIZE << " bytes";

		return Stop();
	}

	if (bytes_transferred < SOCKS_REPLY_SIZE)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " Connection::HandleTorProxyRead host " << host << " torproxy returned " << bytes_transferred << " bytes";

		return Stop();
	}

	if (msgbuf.data()[1] != 90)
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " Connection::HandleTorProxyRead host " << host << " torproxy status " << (unsigned)msgbuf.data()[1];

		return Stop();
	}

	BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleTorProxyRead ok";

	StartConnection();
}

void Connection::HandleStartIncomingConnection(AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " Connection::HandleStartIncomingConnection connection is closing";

		return;
	}

	if (TRACE_CCSERVER &&  m_noclose) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " Connection::HandleStartIncomingConnection";
	if (TRACE_CCSERVER && !m_noclose) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleStartIncomingConnection";

	StartConnection();
}

void Connection::StartConnection()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::StartConnection";

	// the StartConnection function should be overridden if:
	//	if a timeout is needed for the read
	//	the first operation is a write instead of a read

	m_conn_state = CONN_CONNECTED;

	if (CancelTimer())
		return;

	StartRead();
}

bool Connection::SetTimer(unsigned sec)
{
	//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::SetTimer " << sec;

	bool rc = TimerWaitAsync("Connection::SetTimer", sec*1000, boost::bind(&Connection::HandleTimeout, this, boost::asio::placeholders::error, AutoCount(this)));

	if (RandTest(RTEST_STOPS))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::SetTimer simulating random stop";

		Stop();

		if (RandTest(2)) sleep(1);
	}

	return rc;
}

bool Connection::TimerWaitAsync(const char *function, int ms, TimerHandler handler)
{
	// note there is only one timer object m_timer associated with the connection, and all TimerWaitAsync calls wait on this same timer
	// according to boost asio documentation, starting a wait on a timer cancels an already pending wait on the same timer

	if (RandTest(RTEST_TIMER_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " simulating timer error";

		Stop();	// !!! why Stop()?

		return true;
	}

	if (ms > 0 && RandTest(RTEST_CUZZ_TIMER))
	{
		auto newms = rand() % ms;
		newms = rand() % (newms + 1);	// generate random distribution skewed toward smaller values

		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " TimerWaitAsync test changing timer ms from " << ms << " to " << newms;

		ms = newms;
	}

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	lock_guard<mutex> lock(m_conn_lock);

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	if (g_shutdown || m_stopping.load())
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " skipping TimerWaitAsync because connection is closing";

		return true;
	}

	boost::system::error_code e;
	m_timer.expires_from_now(boost::posix_time::milliseconds(ms), e);
	if (e)
	{
		BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " " << function << " boost asio expires_from_now failed error " << e << " " << e.message();

		Stop();

		return true;
	}

	if (RandTest(RTEST_TIMER_TIMEOUT))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " TimerWaitAsync test sleeping until timer has expired";

		ccsleep((ms + 1200) / 1000);

		ms = 0;
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " starting TimerWaitAsync for " << ms << " ms; ops pending " << m_ops_pending.load();

	m_timer.async_wait(handler);

	return false;
}

bool Connection::CancelTimer()
{
	// The "as an optimization..." comment above in CheckOpCount applies here, too

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::CancelTimer " << uintptr_t(this);

	if (RandTest(RTEST_STOPS))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::CancelTimer simulating random stop";

		Stop();

		if (RandTest(2)) sleep(1);

		return false;	// continue execution
	}

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	lock_guard<mutex> lock(m_conn_lock);

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	if ((g_shutdown || m_stopping.load()) && RandTest(RTEST_DELAY_CONN_RELEASE ? 2 : 0))
		return true;

	boost::system::error_code e;
	m_timer.cancel(e);

	return false;
}

void Connection::HandleTimeout(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (e == boost::asio::error::operation_aborted)
	{
		//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleTimeout " << uintptr_t(this) << " e = " << e << " " << e.message();

		return;
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::HandleTimeout " << uintptr_t(this) << " e = " << e << " " << e.message();

	return Stop();
}

void Connection::StartRead()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::StartRead";

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
		m_maxread = m_readbuf.size() - 1;
	}

	QueueRead(m_terminated ? 1 : m_headersize);
}

void Connection::QueueRead(unsigned minbytes)
{
	CCASSERT(minbytes);	// seems to work fine if this is zero, but let's enforce anyway

	CCASSERT(m_nred <= m_maxread);

	if (m_nred >= m_maxread)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::QueueRead buffer full max " << m_maxread;

		return Stop();
	}

	if (m_terminated)
	{
		//if (TRACE_CCSERVER_RW) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::QueueRead m_pread " << (uintptr_t)m_pread << " m_nred " << m_nred << " m_maxread " << m_maxread << " ReadAsync transfer_at_least " << minbytes;

		ReadAsync("Connection::QueueRead", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_at_least(minbytes),
			boost::bind(&Connection::HandleReadCheck, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, AutoCount(this)));
	}
	else
	{
		//if (TRACE_CCSERVER_RW) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::QueueRead m_pread " << (uintptr_t)m_pread << " m_nred " << m_nred << " m_maxread " << m_maxread << " ReadAsync transfer_exactly " << minbytes;

		ReadAsync("Connection::QueueRead", boost::asio::buffer(m_pread + m_nred, m_maxread - m_nred), boost::asio::transfer_exactly(minbytes),
			boost::bind(&Connection::HandleReadCheck, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, AutoCount(this)));
	}
}

void Connection::HandleReadCheck(const boost::system::error_code& e, size_t bytes_transferred, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	m_nred += bytes_transferred;

	CCASSERT(m_nred <= m_maxread);

	bool sim_err = RandTest(RTEST_READ_ERRORS);
	if (sim_err) BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::HandleReadCheck simulating read error";

	if (e || !bytes_transferred || sim_err)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::HandleReadCheck error " << e << " " << e.message() << "; read " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

		return Stop();
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleReadCheck " << bytes_transferred << " of " << m_nred << " of " << m_maxread << " bytes";

	HandleRead(bytes_transferred);

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleReadCheck done";
}

void Connection::HandleRead(size_t bytes_transferred)
{
	// HandleReadComplete is a virtual function that may be overridden.
	// The AutoCount object passed to HandleReadCheck above will still exist during the call the HandleReadComplete,
	// ensuring that the connection will not be completely closed and reused while HandleReadComplete is running.

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
			//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleRead byte index " << i << " of " << m_nred << " buffer value " << (unsigned)m_pread[i] << " terminator " << (unsigned)m_terminator;

			if (m_pread[i] == m_terminator)
			{
				return HandleReadComplete();
			}
		}

		if (m_nred == m_maxread)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::HandleRead buffer overflow max " << m_maxread;

			return Stop();
		}

		if (m_nred >= m_maxread)
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " Conn " << m_conn_index << " Connection::HandleRead buffer overrun max " << m_maxread;

			return Stop();
		}

		QueueRead(1);
	}
}

void Connection::HandleReadComplete()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleReadComplete signalling completion after " << m_nred << " of " << m_maxread << " of maximum bytes read";

	{
		lock_guard<mutex> lock(event_mutex);

		++m_read_count;
	}

	event_condition_variable.notify_all();
}

//@@! add a function parameter that determines if timer is cancelled?
bool Connection::WriteAsync(const char *function, boost::asio::const_buffer buffer, WriteHandler handler, bool already_own_mutex)
{
	{
		// if multiple threads are trying to write, then queuing up on this mutex should help prevent thread starvation
		unique_lock<mutex> next_writer_lock(next_writer_mutex, defer_lock);
		if (!already_own_mutex)
			next_writer_lock.lock();

		unsigned wait_count = 0;

		/* boost asio only allows one async_write to be in progess at a time.  In other words, async_write cannot be called again
			until the completion handler from the last async_write is called.
			In order to enforce this, WriteAsync increments m_write_in_progress, then subsequent calls to WriteAsync
			will block until m_write_in_progress is cleared.  The handler function for async_write should clear m_write_in_progress.
		*/

		while (!g_shutdown && !m_stopping.load() && !already_own_mutex && m_write_in_progress.test_and_set())
		{
			if (!(++wait_count & 4095))	// about 82 seconds
				BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " " << function << " WriteAsync may be hung waiting for prior write to complete";

			//BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " WriteAsync waiting for prior write to complete...";

			usleep(20*1000);
		}

		if (RandTest(RTEST_WRITE_ERRORS))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " simulating WriteAsync error";

			Stop();	// !!! why Stop()?

			return true;
		}

		if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

		lock_guard<mutex> lock(m_conn_lock);

		if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

		if (g_shutdown || m_stopping.load())
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " skipping WriteAsync because connection is closing";

			return true;
		}

		// !!! TODO: fuzz the output

		//BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " " << function << " starting WriteAsync buffer " << buffer.data() << " size " << buffer.size() << " part " << part.data() << " size " << part.size();
		//BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " " << function << " starting WriteAsync data " << buf2hex(part.data(), min(part.size(), size_t(16)));

		//auto part = boost::asio::buffer(buffer.data(), min(buffer.size(), size_t(64*1024)));

		boost::asio::async_write(m_socket, buffer, boost::bind(&Connection::CheckWriteComplete, this, function, buffer, handler, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	}

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	return false;
}

void Connection::CheckWriteComplete(const char *function, boost::asio::const_buffer buffer, WriteHandler handler, const boost::system::error_code& e, size_t bytes_transferred)
{
	if (e)
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " CheckWriteComplete bytes_transferred " << bytes_transferred << " error " << e << " " << e.message();

		return handler(e, bytes_transferred);
	}

	if (RandTest(RTEST_WRITE_ERRORS))
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " simulating write error";

		return handler(boost::system::errc::make_error_code(boost::system::errc::address_family_not_supported), bytes_transferred);
	}

	if (bytes_transferred < buffer.size())
	{
		BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " " << function << " CheckWriteComplete bytes_transferred " << bytes_transferred << " buffer size " << buffer.size();

		auto rc = WriteAsync(function, buffer + bytes_transferred, handler, true);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " CheckWriteComplete requeue WriteAsync failed";

			// note: requeue doesn't currently track and return total bytes_transferred

			return handler(boost::system::errc::make_error_code(boost::system::errc::argument_list_too_long), bytes_transferred);
		}
	}
	else
	{
		if (!buffer.size())
			BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " " << function << " CheckWriteComplete buffer.size = 0";

		return handler(e, bytes_transferred);
	}
}

void Connection::HandleWriteSmartBuf(const boost::system::error_code& e, SmartBuf buf, AutoCount pending_op_counter)
{
	Connection::HandleWrite(e, std::move(pending_op_counter));	// don't need to increment op count
}

void Connection::HandleWriteString(const boost::system::error_code& e, shared_ptr<string> buf, AutoCount pending_op_counter)
{
	//BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleWriteString buf.use_count " << buf.use_count() << " buffer " << buf->data() << " size " << buf->size();
	//BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " RpcConnection::HandleWriteString buffer " << buf->data() << " data " << buf2hex(buf->data(), min(buf->size(), size_t(16)));

	Connection::HandleWrite(e, std::move(pending_op_counter));	// don't need to increment op count
}

void Connection::HandleWrite(const boost::system::error_code& e, AutoCount pending_op_counter)
{
	if (CheckOpCount(pending_op_counter))
		return;

	if (e) return Stop();

	m_write_in_progress.clear();

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleWrite ok";

	if (m_read_after_write)
	{
		StartRead();
	}
	else if (!m_noclose)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleWrite closing connection";

		{
			if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep((rand() & 1) + 1);

			lock_guard<mutex> lock(m_conn_lock);

			if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

			// Initiate graceful Connection closure.
			boost::system::error_code ignored_ec;
			m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
		}

		Stop();
	}
}

void Connection::HandleValidateDone(uint64_t level, uint32_t callback_id, int64_t result)
{
	// calling this function is completely asynchronous and can occur long after a connection is closed and reopened, or while its closing or reopening

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	// need the conn lock here so we don't end up stopping the connection after it has been restarted somewhere else

	lock_guard<mutex> lock(m_conn_lock);

	if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

	if (callback_id != m_use_count.load())
	{
		BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::HandleValidateDone level " << level << " ignoring late or unexpected callback id " << callback_id << " use count " << m_use_count.load();

		return;
	}

	if (TEST_VALIDATION_FAIL_NO_STOP)
		return;

	if (result <= PROCESS_RESULT_STOP_THRESHOLD)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleValidateDone result " << result << " stopping connection";

		return StopWithConnLock();
	}
	else
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleValidateDone result " << result;

		if (RandTest(RTEST_VALIDATION_FAILURES))
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " Connection::HandleValidateDone simulating validation failure";

			return StopWithConnLock();
		}
	}
}

void Connection::Stop()
{
	//if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::Stop";

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep((rand() & 1) + 1);

	lock_guard<mutex> lock(m_conn_lock);

	StopWithConnLock();
}

void Connection::StopWithConnLock()
{
	// call this if the caller already holds m_conn_lock

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	//auto t0 = highres_ticks();

	unique_lock<mutex> lock(m_ref_lock);

	//auto dt = highres_ticks() - t0;
	//if (report_highres_ticks(dt))
	//	cout << "StopWithConnLock acquired m_ref_lock in " << dt << endl;

	if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

	auto already_stopping = m_stopping.fetch_add(1);

	auto ops = m_ops_pending.load();

	lock.unlock();

	if (already_stopping)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::Stop already stopping " << already_stopping;

		return;
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::Stop canceling async ops";

	m_conn_state = CONN_STOPPING;

	boost::system::error_code e;
	m_timer.cancel(e);
	m_socket.cancel(e);

	if (ops)
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::Stop postponing stop for " << ops << " pending ops";
	}
	else
	{
		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::Stop posting stop";

		PostDirect(boost::bind(&Connection::HandleStop, this));
	}
}

void Connection::HandleStop()
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleStop closing connection";

	if (RandTest(RTEST_DELAY_CONN_RELEASE ? 2 : 0)) sleep((rand() & 3) + 1);

	CCASSERT(m_stopping.load() > 0);

	{
		lock_guard<mutex> lock(m_conn_lock);

		if (RTEST_DELAY_CONN_RELEASE) sleep(rand() & 1);

		CCASSERTZ(m_ops_pending.load());

		m_use_count.fetch_add(1);		// ignore any subsequent callbacks with former m_use_count value

		//dump_socket_opts(m_socket.native_handle());

		boost::system::error_code ec;
		m_socket.close(ec);

		if (TRACE_CCSERVER &&  m_noclose) BOOST_LOG_TRIVIAL(debug) << Name() << " Conn " << m_conn_index << " Connection::Stop done";
		if (TRACE_CCSERVER && !m_noclose) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::Stop done";

		FinishConnection();

		CCASSERTZ(m_ops_pending.load());
	}

	{
		lock_guard<mutex> lock(event_mutex);

		m_stopping.store(-9999);			// done stopping

		m_conn_state = CONN_STOPPED;
	}

	event_condition_variable.notify_all();

	if (m_autofree)
		FreeConnection();

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::HandleStop done";
}

void Connection::FreeConnection()
{
	if (!g_shutdown || g_is_dll)
	{
		m_autofree = true;

		m_connection_manager.FreeConnection(this);
	}
}

Connection::~Connection()
{
	if (m_conn_state != CONN_STOPPED)
		BOOST_LOG_TRIVIAL(warning) << Name() << " Conn " << m_conn_index << " Connection::~Connection pending ops " << m_ops_pending.load() << " state " << m_conn_state << " stopping " << m_stopping;

	//can't do this here because there are no threads active to service it:
	//Stop();
	//WaitForStopped();

	//these seem to cause hangs (for unknown reasons, since all handler threads should be terminated before this destructor is called):
	//lock_guard<mutex> lock1(event_mutex);
	//lock_guard<mutex> lock2(m_conn_lock);
	//lock_guard<FastSpinLock> lock3(m_ref_lock);
}

void Connection::WaitForStopped(bool abort_on_shutdown)	// this function should not be used with m_autofree = true when the connection can be automatically reused
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::WaitForStopped abort_on_shutdown " << abort_on_shutdown;

	unique_lock<mutex> lock(event_mutex);

	if (abort_on_shutdown && !g_is_dll)
	{
		while (m_conn_state != CONN_STOPPED && !g_shutdown)
		{
			if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

			event_condition_variable.wait_for(lock, chrono::milliseconds(600));
		}
	}
	else
	{
		while (m_conn_state != CONN_STOPPED)
		{
			if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

			event_condition_variable.wait(lock);
		}
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::WaitForStopped done";
}

void Connection::WaitForReadComplete(unsigned last_read_count, bool abort_on_shutdown)
{
	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::WaitForReadComplete last_read_count " << last_read_count << " abort_on_shutdown " << abort_on_shutdown;

	unique_lock<mutex> lock(event_mutex);

	if (abort_on_shutdown)
	{
		while (m_read_count == last_read_count && !m_stopping.load() && !g_shutdown)
		{
			if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

			event_condition_variable.wait_for(lock, chrono::milliseconds(600));
		}
	}
	else
	{
		while (m_read_count == last_read_count && !m_stopping.load())
		{
			if (RTEST_CUZZ_WAIT) usleep(rand() & RTEST_CUZZ_WAIT);

			event_condition_variable.wait(lock);
		}
	}

	if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " Connection::WaitForReadComplete done";
}

} // namespace CCServer
