/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * transact.hpp
*/

#pragma once

#include "process_queue.h"

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#include <boost/bind.hpp>

class TransactConnection : public CCServer::Connection
{

public:
	TransactConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	:	CCServer::Connection(manager, io_service, connfac)
	{ }

	void HandleValidateDone(uint64_t level, uint32_t callback_id, int64_t result);

private:
	void StartConnection();
	void HandleReadComplete();
	void HandleMsgReadComplete(const boost::system::error_code& e, size_t bytes_transferred, SmartBuf smartobj, AutoCount pending_op_counter);
	void HandleTx(Process_Q_Priority priority, SmartBuf smartobj);
	void SendValidateResult(int64_t result);
	bool SetValidationTimer(uint32_t callback_id, unsigned sec);
	void HandleValidationTimeout(uint32_t callback_id, const boost::system::error_code& e, AutoCount pending_op_counter);
	void HandleTxQueryParams(const char *msg, unsigned size);
	void HandleTxQueryAddress(const char *msg, unsigned size);
	void HandleTxQueryInputs(const char *msg, unsigned size);
	void HandleTxQuerySerials(const char *msg, unsigned size);
	void HandleTxQueryXreqs(const char *msg, unsigned size);
	void HandleTxQueryXmatchreq(uint32_t tag, const char *msg, unsigned size);
	void HandleTxQueryXmatch(const char *msg, unsigned size);
	void HandleTxQueryXminingInfo(const char *msg, unsigned size);
	void SendReply(ostringstream& os);
	void SendObjectNotValid();
	void SendBlockchainNumberError();
	void SendTooManyObjectsError();
	void SendNotConnectedError();
	void SendServerError(unsigned line);
	void SendServerUnknown(unsigned line);
	void SendReplyWriteError();
	void SendTimeout();
};


class TransactService : public TorService
{
	CCServer::Service m_service;

public:
	TransactService(const string& n, const wstring& d, const string& s)
	 :	TorService(n, d, s),
		m_service(n),
		max_net_sec(0),
		max_block_sec(0),
		query_work_difficulty(0)
	{ }

	bool info_only;
	bool relay_only;
	int32_t  max_net_sec;
	int32_t  max_block_sec;
	uint64_t query_work_difficulty;

	void ConfigPostset()
	{
		tor_advertise = false;
	}

	void DumpExtraConfigTop() const;
	void DumpExtraConfigBottom() const;

	bool IsConnectedToNet() const;

	void Start();

	void StartShutdown();
	void WaitForShutdown();
};

class TransactThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
