/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * rpcserve.hpp
*/

#pragma once

#include "txquery.hpp"

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

class RpcConnection : public CCServer::Connection
{
public:
	RpcConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	CCServer::Connection(manager, io_service, connfac),
		m_txquery(TxQuery::nullconnmgr, io_service, TxQuery::txconnfac)
	{
		m_terminator = 10;
		m_read_after_write = false;	// note: if m_read_after_write == false && m_noclose == true, the connection will hang after the first write
	}

private:

	TxQuery m_txquery;

	enum
	{
		POST,
		OPTIONS
	} m_method;

	unsigned m_parse_point;
	unsigned m_content_length;
	unsigned m_content_start;
	bool m_has_auth;

	bool m_keepalive;

	void StartRead();
	void HandleReadComplete();
	void HandleContentReadComplete(const boost::system::error_code& e, size_t bytes_transferred, AutoCount pending_op_counter);
	void HandleWriteHeader(const boost::system::error_code& e, shared_ptr<string> rbuf, AutoCount pending_op_counter);

	void SendServerError();
	void SendBadRequest();
	void SendMethodNotImplemented();
};


class RpcService : public TorService
{
	CCServer::Service m_service;

public:
	RpcService(const string& n, const wstring& d, const string& s)
	 :	TorService(n, d, s),
		m_service(n)
	{ }

	string user_string;
	string pass_string;
	string auth_string;

	void ConfigPreset()
	{
		tor_new_hostname = false;
	}

	void ConfigPostset()
	{
		tor_advertise = false;
	}

	void Start();

	void StartShutdown();
	void WaitForShutdown();
};

class RpcThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
