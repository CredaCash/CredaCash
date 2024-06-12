/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
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

extern bool g_trace_ccserver;

#define TRACE_CCSERVER		g_trace_ccserver		// !!! TODO: make this a property of the connection
#define TRACE_CCSERVER_RW	0		// !!! TODO: make this a property of the connection

//!#define RTEST_CUZZ_CONN				32
//!#define RTEST_READ_ERRORS			128
//!#define RTEST_WRITE_ERRORS			128
//!#define RTEST_TIMER_ERRORS			128
//!#define RTEST_STOPS					64

//!#define RTEST_VALIDATION_FAILURES	64

//!#define RTEST_DELAY_CONN_RELEASE		32

#ifndef RTEST_CUZZ_CONN
#define RTEST_CUZZ_CONN				0	// don't test
#endif

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

#ifndef RTEST_DELAY_CONN_RELEASE
#define RTEST_DELAY_CONN_RELEASE		0	// don't test
#endif

#define PROCESS_RESULT_STOP_THRESHOLD	-1000

using namespace std;

namespace CCServer {

class ConnectionManagerBase;
typedef class Connection *pconnection_t;

typedef function<void(const boost::system::error_code& e, size_t bytes)> ReadHandler;
typedef function<void(const boost::system::error_code& e, size_t bytes)> WriteHandler;
typedef function<void(const boost::system::error_code& e)> TimerHandler;
typedef function<void()> PostHandler;

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

	char * ReadBufDebug(unsigned min_chars = 1, char* defval = NULL) const
	{
		return (char*)(m_readbuf.size() >= min_chars ? m_readbuf.data() : defval);
	}

	/// Counter increments each time a read operation completes
	unsigned m_read_count;

	bool m_autofree;

	const string& Name() const;

	/// Construct a Connection with the given ConnectionManager and io_service.
	explicit Connection(ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class ConnectionFactoryBase& connfac);

	virtual ~Connection();

	/// Initialize member values for a new connection
	virtual void InitNewConnection();

	/// Reference Counting
	bool IncRef();
	void DecRef();
	bool CheckOpCount(AutoCount& pending_op_counter);

	/// Queue asynchronous function
	bool Post(const char *function, PostHandler handler);
	void PostDirect(PostHandler handler);

	/// Start incoming connection
	virtual void HandleStartIncomingConnection(AutoCount pending_op_counter);

	/// Make outgoing connection
	void HandleConnectOutgoing(const string& host, unsigned port, AutoCount pending_op_counter);
	void HandleConnectOutgoingTor(unsigned proxy_port, const string& host, const string& toruser, AutoCount pending_op_counter);
	void HandleTorProxyWrite(const string host, const boost::system::error_code& e, SmartBuf msgbuf, AutoCount pending_op_counter);
	void HandleTorProxyRead(const string host, const boost::system::error_code& e, size_t bytes_transferred, SmartBuf msgbuf, AutoCount pending_op_counter);

	// Waits for a read operation to complete
	void WaitForReadComplete(unsigned last_read_count, bool abort_on_shutdown = true);

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

	/// Timeout functions
	virtual bool SetTimer(unsigned sec);
	bool CancelTimer();
	bool TimerWaitAsync(const char *function, int ms, TimerHandler handler);
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

	/// Write data
	bool WriteAsync(const char *function, boost::asio::const_buffer buffer, WriteHandler handler, bool already_own_mutex = false);
	void CheckWriteComplete(const char *function, boost::asio::const_buffer buffer, WriteHandler handler, const boost::system::error_code& e, size_t bytes_transferred);

	/// Handle completion of a write operation.
	virtual void HandleWrite(const boost::system::error_code& e, AutoCount pending_op_counter);
	void HandleWriteSmartBuf(const boost::system::error_code& e, SmartBuf buf, AutoCount pending_op_counter);
	void HandleWriteString(const boost::system::error_code& e, shared_ptr<string> buf, AutoCount pending_op_counter);

	/// Processing function callback
	virtual void HandleValidateDone(uint64_t level, uint32_t callback_id, int64_t result);

	/// Asynchronous read (templated for CompletionCondition)
	template <typename CompletionCondition>
	bool ReadAsync(const char *function, boost::asio::mutable_buffer buffer, CompletionCondition completion_condition, ReadHandler handler)
	{
		if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

		lock_guard<mutex> lock(m_conn_lock);

		if (RandTest(RTEST_CUZZ_CONN)) sleep(1);

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
	mutex m_conn_lock;

	// lock for reference counter
	mutex m_ref_lock;

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
