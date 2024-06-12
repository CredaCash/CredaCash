/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * blockserve.hpp
*/

#pragma once

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#include <CCobjdefs.h>

class BlockServeConnection : public CCServer::Connection
{
public:
	BlockServeConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	CCServer::Connection(manager, io_service, connfac),
		m_reqlevel(0),
		m_nreqlevels(0)
	{ }

private:

	atomic<uint64_t> m_reqlevel;
	atomic<uint16_t> m_nreqlevels;

	void StartConnection();

	void HandleReadComplete();

	void DoSend();
	void HandleBlockWrite(const boost::system::error_code& e, SmartBuf smartobj, AutoCount pending_op_counter);
};


class BlockService : public TorService
{
	CCServer::Service m_service;

	thread m_conn_monitor_thread;

	void ConnMonitorProc();

public:
	BlockService(const string& n, const wstring& d, const string& s)
	 :	TorService(n, d, s),
		m_service(n)
	{ }

	void ConfigPreset()
	{
		//if (!g_store_blocks)
		//	enabled = false;

		tor_service = true;
		tor_new_hostname = true;
	}

	void Start();

	void StartShutdown();
	void WaitForShutdown();
};

class BlockServeThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
