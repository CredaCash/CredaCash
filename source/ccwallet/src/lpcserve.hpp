/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * lpcserve.hpp
*/

#pragma once

#include "txquery.hpp"

#include <ccserver/service.hpp>

class LpcService : public TorService
{
	CCServer::Service m_service;

public:
	LpcService(const string& n, const wstring& d, const string& s)
	 :	TorService(n, d, s),
		m_service(n)
	{ }

	void ConfigPreset()
	{
		enabled = true;
		tor_new_hostname = false;
	}

	void ConfigPostset()
	{
		tor_advertise = false;
	}

	void Start();

	TxQuery* GetConnection(bool autofree = true);

	void StartShutdown();
	void WaitForShutdown();
};

class LpcThread : public CCThread
{
public:
	void ThreadProc(boost::function<void()> threadproc);
};
