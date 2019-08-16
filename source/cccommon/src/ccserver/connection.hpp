/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * connection.hpp
*/

#pragma once

#include <vector>
#include <boost/asio.hpp>
#include <unistd.h>

#include <osutil.h>
#include <AutoCount.hpp>
#include <SmartBuf.hpp>
#include <SpinLock.hpp>

#define TRACE_CCSERVER		1		// !!! TODO: make this a property of the connection
#define TRACE_CCSERVER_RW	0		// !!! TODO: make this a property of the connection

//!#define RTEST_READ_ERRORS			128
//!#define RTEST_WRITE_ERRORS			128
//!#define RTEST_TIMER_ERRORS			128
//!#define RTEST_STOPS					64

//!#define RTEST_VALIDATION_FAILURES	64

//!#define RTEST_CUZZ_TIMER				32
//!#define RTEST_TIMER_TIMEOUT			32

//!#define RTEST_DELAY_CONN_RELEASE		32

#ifndef RTEST_READ_ERRORS
#define RTEST_READ_ERRORS				0	// don't test
#endif

#ifndef RTEST_WRITE_ERRORS
#define RTEST_WRITE_ERRORS				0	// don't test
#endif

#ifndef RTEST_TIMER_ERRORS
#define RTEST_TIMER_ERRORS				0	// don't test
#endif

#ifndef RTEST_STOPS
#define RTEST_STOPS						0	// don't test
#endif

#ifndef RTEST_VALIDATION_FAILURES
#define RTEST_VALIDATION_FAILURES		0	// don't test
#endif

#ifndef RTEST_CUZZ_TIMER
#define RTEST_CUZZ_TIMER				0	// don't test
#endif

#ifndef RTEST_TIMER_TIMEOUT
#define RTEST_TIMER_TIMEOUT				0	// don't test
#endif

#ifndef RTEST_DELAY_CONN_RELEASE
#define RTEST_DELAY_CONN_RELEASE		0	// don't test
#endif

#define PROCESS_RESULT_STOP_THRESHOLD	-10

using namespace std;

namespace CCServer {

class ConnectionManagerBase;
typedef class Connection *pconnection_t;

/// Represents a single Connection from a client.
class Connection
	: public RefCounted
{
public:

	volatile enum state_t
	{
		CONN_STOPPED,
		CONN_CONNECTING,
		CONN_CONNECTED,
		CONN_STOPPING
	} m_conn_state;

	/// Counter increments each time a read operation completes
	unsigned m_read_count;

	bool m_autofree;

	const string& Name() const;

	/// Construct a Connection with the given ConnectionManager and io_service.
	explicit Connection(ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class ConnectionFactoryBase& connfac);

	virtual ~Connection();

	/// Initialize member values for a new connection
	virtual void InitNewConnection();

	/// Start incoming connection
	virtual void HandleStartIncomingConnection(AutoCount pending_op_counter);

	/// Make outgoing connection
	void HandleConnectOutgoing(const string& host, unsigned port, AutoCount pending_op_counter);
	void HandleConnectOutgoingTor(unsigned proxy_port, const string& host, const string& toruser, AutoCount pending_op_counter);
	void HandleTorProxyWrite(const string host, const boost::system::error_code& e, SmartBuf msgbuf, AutoCount pending_op_counter);
	void HandleTorProxyRead(const string host, const boost::system::error_code& e, size_t bytes_transferred, SmartBuf msgbuf, AutoCount pending_op_counter);

	// Waits for a read operation to complete
	void WaitForReadComplete(unsigned last_read_count, bool abort_on_shutdown = false);

	/// Initiates close of the connection
	void Stop();
	void StopWithConnLock();

	/// Handles final close
	void HandleStop();

	/// Clean up connection
	virtual void FinishConnection() {}

	/// Wait for connection close
	void WaitForStopped(bool abort_on_shutdown = false);

	/// Free connection
	void FreeConnection();

	/// Test Op Counter for connection close
	bool CheckOpCount(AutoCount& pending_op_counter);

	/// Timeout functions
	virtual bool SetTimer(unsigned sec);
	bool CancelTimer();
	virtual void HandleTimeout(const boost::system::error_code& e, AutoCount pending_op_counter);

	/// Start a new read
	virtual void StartRead();

	/// Queue a read
	virtual void QueueRead(unsigned minbytes);

	/// Handle completion of a read operation.
	virtual void HandleReadCheck(const boost::system::error_code& e, size_t bytes_transferred, AutoCount pending_op_counter);

	/// Handle completion of a successful read operation.
	virtual void HandleRead(size_t bytes_transferred);

	/// Handle completion of reading a full message
	virtual void HandleReadComplete();

	/// Handle completion of a write operation.
	virtual void HandleWrite(const boost::system::error_code& e, AutoCount pending_op_counter);
	void HandleWriteSmartBuf(const boost::system::error_code& e, SmartBuf buf, AutoCount pending_op_counter);
	void HandleWriteString(const boost::system::error_code& e, shared_ptr<string> buf, AutoCount pending_op_counter);
	void HandleWriteOstream(const boost::system::error_code& e, shared_ptr<ostringstream> buf, AutoCount pending_op_counter);

	/// Processing function callback
	virtual void HandleValidateDone(uint32_t callback_id, int64_t result);

	// Templated asynchronous functions

	bool IncRef();
	void DecRef();

	/* for Post, ReadAsync, WriteAsync and AsyncTimerWait:
		The caller creates an AutoCount object and passes it to the boost asio handler.
		This object prevents the connection from closing completely until the handler function
		has completed and the AutoCount object is destroyed.
		If the connection was closing when the calling function attempted to create the AutoCount object,
		the AutoCount IncRef will fail and the AutoCount will not hold a reference to the connection.
		The easiest way to test of this is for Post, ReadAsync, WriteAsync and AsyncTimerWait
		to test m_stopping and return true if m_stopping is set.  This indicates to the caller
		that the async operation was not started and it should abort or otherwise handle this condition.
		For comments on what happens when boost asio calls the handler, see the source code for CheckOpCount()
	*/

	template <typename Handler>
	bool Post(const char *function, Handler handler)
	{
		if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

		lock_guard<FastSpinLock> lock(m_conn_lock);

		if (g_shutdown || m_stopping.load())
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " skipping Post because connection is closing";

			return true;
		}

		PostDirect(handler);	// post is async, and may need a refcount to prevent Stop from running while the post is pending

		return false;
	};

	template <typename Handler>
	void PostDirect(Handler handler)
	{
		#if BOOST_VERSION < 107000
		m_socket.get_io_service().post(handler);
		#else
		post(m_socket.get_executor(), handler);
		#endif
	}

	template <typename Buffer, typename CompletionCondition, typename Handler>
	bool ReadAsync(const char *function, Buffer buffer, CompletionCondition completion_condition, Handler handler)
	{
		if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

		lock_guard<FastSpinLock> lock(m_conn_lock);

		if (g_shutdown || m_stopping.load())
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " skipping ReadAsync because connection is closing";

			return true;
		}

		if (TRACE_CCSERVER_RW) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " starting ReadAsync buffer " << (uintptr_t)boost::asio::buffer_cast<const void*>(buffer) << " size " << boost::asio::buffer_size(buffer);

		//memset((void*)boost::asio::buffer_cast<const void*>(buffer), 0xA5, boost::asio::buffer_size(buffer));	// for testing

		// !!! TODO: add a stub handler that simulates errors and fuzzes input buffer if allowed by flag in connection

		boost::asio::async_read(m_socket, buffer, completion_condition, handler);

		return false;
	};

	//@@! add a function parameter that determines if timer is cancelled?

	template <typename Buffer, typename Handler>
	bool WriteAsync(const char *function, Buffer buffer, Handler handler, bool already_own_mutex = false)
	{
		// if multiple threads are trying to write, then queuing up on this mutex should help prevent thread starvation
		unique_lock<mutex> next_writer_lock(next_writer_mutex, defer_lock);
		if (!already_own_mutex)
			next_writer_lock.lock();

		unsigned wait_count = 0;

		/* boost asio only allows one async_write to be in progess at a time.  In other words, async_write cannot be called again
			until the completion handler from the last async_write is called.
			In order to enforce this, WriteAsync increments m_write_in_progress and subsequently calls to WriteAsync
			will block until m_write_in_progress is cleared.  The handler function for WriteAsync should clear m_write_in_progress.
			At some point, this might be changed to add a chained handler in the middle that clears m_write_in_progress
			so the ultimate handler doesn't have to worry about this.
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
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " simulating write error";

			Stop();	// !!! why Stop()?

			return true;
		}

		if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

		lock_guard<FastSpinLock> lock(m_conn_lock);

		if (g_shutdown || m_stopping.load())
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " skipping WriteAsync because connection is closing";

			return true;
		}

		if (TRACE_CCSERVER_RW) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " starting WriteAsync buffer " << (uintptr_t)boost::asio::buffer_cast<const void*>(buffer) << " size " << boost::asio::buffer_size(buffer);

		// !!! TODO: fuzz the output buffer if allowed by a flag in connection
		// !!! TODO: add a chained handler that clears m_write_in_progress, and can also simulate errors

		boost::asio::async_write(m_socket, buffer, handler);

		return false;
	};

	template <typename Handler>
	bool AsyncTimerWait(const char *function, int ms, Handler handler)
	{
		// note there is only one timer object m_timer associated with the connection, and all AsyncTimerWait calls wait on this same timer
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

			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " AsyncTimerWait test changing timer ms from " << ms << " to " << newms;

			ms = newms;
		}

		if (RandTest(RTEST_DELAY_CONN_RELEASE)) sleep(1);

		lock_guard<FastSpinLock> lock(m_conn_lock);

		if (g_shutdown || m_stopping.load())
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " skipping AsyncTimerWait because connection is closing";

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
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn " << m_conn_index << " " << function << " AsyncTimerWait test sleeping until timer has expired";

			ccsleep((ms + 1200) / 1000);

			ms = 0;
		}

		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " " << function << " starting AsyncTimerWait for " << ms << " ms; ops pending " << m_ops_pending.load();

		m_timer.async_wait(handler);

		return false;
	};

friend class Server;
friend class ConnectionManager;

protected:

	virtual void StartConnection();

	/// The manager for this Connection.
	ConnectionManagerBase& m_connection_manager;

	/// Global index in g_connregistry
	int m_conn_index;

	/// Flags if the connection is in the free list
	bool m_is_free;

	/// Counter increments each time the connection is used
	atomic<unsigned> m_use_count;

	/// Operational options
	unsigned m_headersize;
	bool m_terminated;
	char m_terminator;
	bool m_read_after_write;
	bool m_noclose;

	/// Socket for the Connection.
	boost::asio::ip::tcp::socket m_socket;
	bool m_incoming;					// flags incoming connection

	/// Local data buffers
	vector<char> m_readbuf;
	vector<char> m_writebuf;

	/// Buffer tracking
	char *m_pread;
	unsigned m_maxread;
	unsigned m_nred;

	/// For general timeouts
	boost::asio::deadline_timer m_timer;

	// lock for m_socket and m_timer -- boost docs say socket is not thread safe (at minimum, close must be serialized)
	FastSpinLock m_conn_lock;

	// lock for reference counter
	FastSpinLock m_ref_lock;

	atomic<int> m_stopping;				// don't queue more reads or writes if connection stopping
	atomic<int> m_ops_pending;			// don't stop until all pending aync ops are done
	atomic_flag m_write_in_progress;	// a write on the socket is in progress
	mutex next_writer_mutex;			// mutex to be next to write

	// event wait
	mutex event_mutex;
	condition_variable event_condition_variable;
};

class ConnectionFactoryBase
{
public:
	ConnectionFactoryBase(unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister);

	virtual ~ConnectionFactoryBase() = default;

	unsigned m_conn_nreadbuf;
	unsigned m_conn_nwritebuf;
	unsigned m_sock_nreadbuf;	// 0 = same as conn_nreadbuf; -1 = max size
	unsigned m_sock_nwritebuf;	// 0 = same as conn_nwritebuf; -1 = max_size
	unsigned m_headersize;		// zero -> terminated
	bool m_noclose;				// don't close connection after write
	bool m_register;			// register connection with g_connregistry for integer index to connection pointer lookup's
};

class ConnectionFactory : public ConnectionFactoryBase
{
public:
	ConnectionFactory(unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister)
	:	ConnectionFactoryBase(conn_nreadbuf, conn_nwritebuf, sock_nreadbuf, sock_nwritebuf, headersize, noclose, bregister)
	{ }

	virtual ~ConnectionFactory() = default;

	virtual pconnection_t NewConnection(ConnectionManagerBase& manager, boost::asio::io_service& io_service) const = 0;
};

template <typename CT>
class ConnectionFactoryInstantiation : public ConnectionFactory
{
public:
	ConnectionFactoryInstantiation(unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister)
		: ConnectionFactory(conn_nreadbuf, conn_nwritebuf, sock_nreadbuf, sock_nwritebuf, headersize, noclose, bregister)
	{ }

	pconnection_t NewConnection(ConnectionManagerBase& manager, boost::asio::io_service& io_service) const
	{
		return new CT(manager, io_service, *this);
	}
};

} // namespace CCServer
