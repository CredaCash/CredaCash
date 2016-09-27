/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * blockserve.hpp
*/

#pragma once

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#include "service_base.hpp"

#include <CCobjdefs.h>

class BlockServeConnection : public CCServer::Connection
{
public:
	BlockServeConnection(class CCServer::ConnectionManager& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactory& connfac)
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

	bool SetTimer(unsigned sec);
	void HandleTimeout(const boost::system::error_code& e, AutoCount pending_op_counter);
};


class BlockService : public ServiceBase
{
	CCServer::Service m_service;

	thread m_conn_monitor_thread;

	void ConnMonitorProc();

public:
	BlockService(string n, string s)
	 :	ServiceBase(n, s),
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

	void WaitForShutdown();
};

class BlockServeThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
