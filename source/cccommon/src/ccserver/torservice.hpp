/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * torservice.hpp
*/

#pragma once

#include <boost/asio.hpp>

class TorService
{
	string tor_hostname;

public:
	const string name;
	const wstring& app_data_dir;
	const string tor_hostname_subdir;
	bool no_port;
	bool enabled;
	int port;
	string address_string;
	boost::asio::ip::address address;
	string password_string;
	bool tor_service;
	bool tor_new_hostname;
	string tor_auth_string;
	int tor_auth;
	bool tor_advertise;

	int max_outconns;
	int max_inconns;
	float threads_per_conn;

	static void SetPorts(vector<TorService*>& services, unsigned base_port);

	const string& Name() const
	{
		return name;
	}

	TorService(const string& n, const wstring& d, const string& s, bool np = false) :
		name(n),
		app_data_dir(d),
		tor_hostname_subdir(s),
		no_port(np),
		enabled(false),
		port(0),
		tor_service(false),
		tor_new_hostname(false),
		tor_auth(0),
		tor_advertise(false),
		max_outconns(0),
		max_inconns(0),
		threads_per_conn(1)
	{ }

	virtual ~TorService() = default;

	static int TorAuthInt(const string& str);
	static const char* TorAuthString(int val);

	virtual void ConfigPreset() {}
	virtual void ConfigPostset() {}

	virtual int SetConfig();
	virtual void DumpConfig() const;
	virtual void DumpExtraConfigTop() const {}
	virtual void DumpExtraConfigBottom() const {}

	const string& TorHostname();
};


class ControlService : public TorService
{
public:
	ControlService(const string& n, const wstring& d, const string& s)
		: TorService(n, d, s)
	{ }

	void ConfigPostset()
	{
		tor_advertise = false;
	}
};

class TorControlService : public TorService
{
public:
	TorControlService(const string& n, const wstring& d, const string& s)
		: TorService(n, d, s)
	{ }

	void ConfigPostset()
	{
		tor_advertise = false;
	}
};
