/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * foreign-rpc.hpp
*/

#pragma once

#include "foreign-conn.hpp"

#include <xtransaction-xreq.hpp>
#include <jsoncpp/json/json.h>

class ForeignRpc : public ForeignConnection
{
	int TryQuery(unsigned port, const string& query);

public:
	ForeignRpc(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	ForeignConnection(manager, io_service, connfac)
	{ }

	int SubmitQuery(unsigned port, const string& auth, const string& query, Json::Value *root, const char* *content = NULL, bool debug = false);
};

class ForeignRpcClient : public TorService
{
	CCServer::Service m_service;

public:
	enum VerifyLevel
	{
		None = 0,
		If_Possible,
		Strict_Relay,	// and blocks if possible
		Strict_Blocks	// and strict relay
	};

	VerifyLevel GetVerifyLevel(unsigned blockchain) const
	{
		CCASSERT(blockchain <= XREQ_BLOCKCHAIN_MAX);

		if (blockchain <= XREQ_BLOCKCHAIN_MAX)
			return (VerifyLevel)verify_level[blockchain];

		return Strict_Blocks;
	}

	bool NoQuery(unsigned blockchain) const
	{
		CCASSERT(blockchain <= XREQ_BLOCKCHAIN_MAX);

		auto level = GetVerifyLevel(blockchain);

		return (level < If_Possible);
	}

	bool IgnoreError(unsigned blockchain, bool in_block) const
	{
		CCASSERT(blockchain <= XREQ_BLOCKCHAIN_MAX);

		auto level = GetVerifyLevel(blockchain);

		if (level >= Strict_Blocks)
			return false;

		if (level == Strict_Relay)
			return in_block;

		return true;
	}

	int verify_level[XREQ_BLOCKCHAIN_MAX+1];
	int rpc_port[XREQ_BLOCKCHAIN_MAX+1];
	string rpc_username[XREQ_BLOCKCHAIN_MAX+1];
	string rpc_password[XREQ_BLOCKCHAIN_MAX+1];
	string rpc_auth[XREQ_BLOCKCHAIN_MAX+1];

	ForeignRpcClient(const string& n, const wstring& d, const string& s)
	 :	TorService(n, d, s),
		m_service(n)
	{
		memset(verify_level, 0, sizeof(verify_level));
		memset(rpc_port, 0, sizeof(rpc_port));
	}

	void ConfigPreset()
	{
		enabled = true;
	}

	void DumpExtraConfigBottom() const;

	void ConfigPostset();

	void Start();

	ForeignRpc* GetConnection(bool autofree = true);

	void StartShutdown();
	void WaitForShutdown();
};

class ForeignRpcThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
