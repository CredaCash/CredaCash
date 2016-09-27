/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * transact.hpp
*/

#pragma once

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#include "service_base.hpp"

#include <boost/bind.hpp>

class TransactConnection : public CCServer::Connection
{

public:
	TransactConnection(class CCServer::ConnectionManager& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactory& connfac)
	:	CCServer::Connection(manager, io_service, connfac),
		expected_callback_id(0)
	{ }

	void HandleValidateDone(unsigned callback_id, int64_t result);

private:
	atomic<uint32_t> expected_callback_id;

	void StartConnection();
	void HandleReadComplete();
	void HandleMsgReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter);
	void HandleTx(SmartBuf smartobj);
	bool SetTimer(unsigned sec);
	void HandleTimeout(unsigned callback_id, const boost::system::error_code& e, AutoCount pending_op_counter);
	void HandleTxQueryParams(const uint8_t *msg, unsigned size);
	void HandleTxQueryAddress(const uint8_t *msg, unsigned size);
	void HandleTxQueryInputs(const uint8_t *msg, unsigned size);
	void HandleTxQuerySerial(const uint8_t *msg, unsigned size);
	void SendReply(ostringstream& os);
	void SendServerError(unsigned line);
	void SendReplyWriteError();
	void SendTimeout();
};


class TransactService : public ServiceBase
{
	CCServer::Service m_service;

public:
	TransactService(string n, string s)
	 :	ServiceBase(n, s),
		m_service(n)
	{ }

	void ConfigPostset()
	{
		tor_advertise = false;
	}

	void Start();

	void WaitForShutdown();
};

class TransactThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
