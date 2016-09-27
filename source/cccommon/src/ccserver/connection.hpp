/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * connection.hpp
*/

#pragma once

#include <vector>
#include <boost/asio.hpp>
#include <unistd.h>

#include <AutoCount.hpp>
#include <SmartBuf.hpp>
#include <SpinLock.hpp>

#define TRACE_CCSERVER		1
#define TRACE_CCSERVER_RW	1

//#define TEST_RANDOM_READ_ERRORS		127
//#define TEST_RANDOM_WRITE_ERRORS	127

#ifndef TEST_RANDOM_READ_ERRORS
#define TEST_RANDOM_READ_ERRORS 0	// don't test
#endif

#ifndef TEST_RANDOM_WRITE_ERRORS
#define TEST_RANDOM_WRITE_ERRORS 0	// don't test
#endif

#define PROCESS_RESULT_STOP_THRESHOLD	-10

using namespace std;

namespace CCServer {

class ConnectionManager;

/// Represents a single Connection from a client.
class Connection
	: public RefCounted
{
public:

	const string& Name() const;

	/// Construct a Connection with the given ConnectionManager and io_service.
	explicit Connection(ConnectionManager& manager, boost::asio::io_service& io_service, const class ConnectionFactory& connfac);

	virtual ~Connection() = default;

	/// Initialize member values for a new connection
	virtual void InitNewConnection();

	/// Prepare to start the first asynchronous operation for an incoming connection
	virtual void StartIncomingConnection();

	/// Prepare to start the first asynchronous operation for any connection
	virtual void StartConnection();

	/// Make outgoing connection
	void ConnectOutgoing(const string& host, unsigned port);
	void ConnectOutgoingTor(const string& host, unsigned proxy_port);
	void HandleTorProxyWrite(const boost::system::error_code& e, SmartBuf msgbuf, AutoCount pending_op_counter);
	void HandleTorProxyRead(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf msgbuf, AutoCount pending_op_counter);
	void HandleTorTimeout(const boost::system::error_code& e, AutoCount pending_op_counter);

	/// Initiates close of the connection
	void Stop();
	void StopWithConnLock();

	/// Closes the connection
	void HandleStop();

	/// Close out connection
	virtual void FinishConnection() {}

	/// Closes the connection
	void WaitForStop();

	/// Start a new read
	virtual void StartRead();

	/// Queue a read
	virtual void QueueRead(unsigned minbytes);

	/// Handle completion of a read operation.
	virtual void HandleReadCheck(const boost::system::error_code& e, size_t bytes_transferred, AutoCount pending_op_counter);

	/// Handle completion of a successful read operation.
	virtual void HandleRead(size_t bytes_transferred);

	/// Handle completion of reading a full message
	virtual void HandleReadComplete() = 0;

	/// Handle completion of a write operation.
	virtual void HandleWrite(const boost::system::error_code& e, AutoCount pending_op_counter);
	virtual void HandleWriteSmartBuf(const boost::system::error_code& e, SmartBuf buf, AutoCount pending_op_counter);

	/// Processing function callback
	virtual void HandleValidateDone(unsigned callback_id, int64_t result);

	// Templated asynchronous functions

	AutoCount AcquireRef();
	bool IncRef();
	void DecRef();

	template <typename Handler>
	void Post(Handler handler)
	{
		lock_guard<FastSpinLock> lock(m_conn_lock);

		m_socket.get_io_service().post(handler);
	};

	template <typename Buffer, typename CompletionCondition, typename Handler>
	bool ReadAsync(const char *function, Buffer buffer, CompletionCondition completion_condition, Handler handler)
	{
		if (g_shutdown || m_stopping.load())
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " " << function << " skipping ReadAsync because connection is closing";

			return true;
		}

		if (TRACE_CCSERVER_RW) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " " << function << " starting ReadAsync buffer " << (uintptr_t)boost::asio::buffer_cast<const void*>(buffer) << " size " << boost::asio::buffer_size(buffer);

		//memset((void*)boost::asio::buffer_cast<const void*>(buffer), 0xA5, boost::asio::buffer_size(buffer));	// for testing

		boost::asio::async_read(m_socket, buffer, completion_condition, handler);

		return false;
	};

	template <typename Buffer, typename Handler>
	bool WriteAsync(const char *function, Buffer buffer, Handler handler, bool already_own_mutex = false)
	{
		// if multiple threads are trying to write, then queuing up on this mutex should help prevent thread starvation
		unique_lock<mutex> next_writer_lock(next_writer_mutex, defer_lock);
		if (!already_own_mutex)
			next_writer_lock.lock();

		unsigned wait_count = 0;

		while (!g_shutdown && !m_stopping.load() && m_write_in_progress.test_and_set())
		{
			if (!(++wait_count & 1023))	// about 102 seconds
				BOOST_LOG_TRIVIAL(warning) << Name() << " Conn-" << m_conn_index << " " << function << " WriteAsync may be hung waiting for prior write to complete";

			//BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " " << function << " WriteAsync waiting for prior write to complete...";

			usleep(100*1000);
		}

		if (g_shutdown || m_stopping.load())
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " " << function << " skipping WriteAsync because connection is closing";

			return true;
		}

		if ((TEST_RANDOM_WRITE_ERRORS & rand()) == 1)
		{
			BOOST_LOG_TRIVIAL(info) << Name() << " Conn-" << m_conn_index << " " << function << " simulating write error";

			Stop();

			return true;
		}

		// !!! add a fuzz test, but only if the buffer is writable

		if (TRACE_CCSERVER_RW) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " " << function << " starting WriteAsync buffer " << (uintptr_t)boost::asio::buffer_cast<const void*>(buffer) << " size " << boost::asio::buffer_size(buffer);

		boost::asio::async_write(m_socket, buffer, handler);

		return false;
	};

	template <typename Handler>
	bool AsyncTimerWait(const char *function, int ms, Handler handler, AutoCount& op_counter)
	{
		if (op_counter.AcquireRef(this))
		{
			if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " " << function << " skipping SetTimer because connection is closing";

			return true;
		}

		boost::system::error_code e;
		m_timer.expires_from_now(boost::posix_time::milliseconds(ms), e);
		if (e)
		{
			BOOST_LOG_TRIVIAL(error) << Name() << " Conn-" << m_conn_index << " " << function << " expires_from_now failed error " << e << " " << e.message();

			Stop();

			return true;
		}

		if (TRACE_CCSERVER) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn-" << m_conn_index << " " << function << " starting async wait for " << ms << " ms";

		m_timer.async_wait(handler);

		return false;
	};

	/// Cancels the timer associated with the Connection
	void CancelTimer();

friend class Server;
friend class ConnectionManager;

protected:
	/// The manager for this Connection.
	ConnectionManager& m_connection_manager;

	/// Global index in g_connregistry
	unsigned m_conn_index;

	/// Flags if the connection is in the free list
	bool m_is_free;

	/// Counter increments each time the connection is used
	atomic<unsigned> m_use_count;

	/// Operational options
	unsigned m_headersize;
	bool m_noclose;

	/// Socket for the Connection.
	boost::asio::ip::tcp::socket m_socket;
	bool m_incoming;					// flags incoming connection

	/// Local data buffers
	vector<uint8_t> m_readbuf;
	vector<uint8_t> m_writebuf;

	/// Buffer tracking
	uint8_t *m_pread;
	unsigned m_maxread;
	unsigned m_nred;
	bool m_terminated;

	/// For general timeouts
	boost::asio::deadline_timer m_timer;

	// lock for m_socket and m_timer -- boost docs say socket is not thread safe (at minimum, close must be serialized)
	FastSpinLock m_conn_lock;

	atomic<int> m_stopping;				// don't queue more reads or writes if connection stopping
	atomic<int> m_ops_pending;			// don't stop until all pending aync ops are done
	atomic_flag m_write_in_progress;	// a write on the socket is in progress
	mutex next_writer_mutex;			// mutex to be next to write
};

typedef Connection *pconnection_t;

class ConnectionFactory
{
public:
	ConnectionFactory(unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister);

	virtual ~ConnectionFactory() = default;

	virtual pconnection_t NewConnection(ConnectionManager& manager, boost::asio::io_service& io_service) const = 0;

	unsigned m_conn_nreadbuf;
	unsigned m_conn_nwritebuf;
	unsigned m_sock_nreadbuf;	// 0 = same as conn_nreadbuf; -1 = max size
	unsigned m_sock_nwritebuf;	// 0 = same as conn_nwritebuf; -1 = max_size
	unsigned m_headersize;		// zero = nul terminated
	bool m_noclose;				// don't close connection after write
	bool m_register;			// register connection with g_connregistry for integer index -> connection pointer lookup's
};

template <typename CT>
class ConnectionFactoryInstantiation : public ConnectionFactory
{
public:
	ConnectionFactoryInstantiation(unsigned conn_nreadbuf, unsigned conn_nwritebuf, unsigned sock_nreadbuf, unsigned sock_nwritebuf, unsigned headersize, bool noclose, bool bregister)
		: ConnectionFactory(conn_nreadbuf, conn_nwritebuf, sock_nreadbuf, sock_nwritebuf, headersize, noclose, bregister)
	{ }

	pconnection_t NewConnection(ConnectionManager& manager, boost::asio::io_service& io_service) const
	{
		return new CT(manager, io_service, *this);
	}
};

} // namespace CCServer
