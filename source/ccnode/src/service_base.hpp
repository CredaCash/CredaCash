/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * service_base.hpp
*/

#pragma once

#include <boost/asio.hpp>

class ServiceBase
{
	string tor_hostname;

public:
	const string name;
	const string tor_hostname_subdir;
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

	const string& Name() const
	{
		return name;
	}

	ServiceBase(string n, string s) :
		name(n),
		tor_hostname_subdir(s),
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

	virtual ~ServiceBase() = default;

	static int TorAuthInt(const string& str);
	static const char* TorAuthString(int val);

	virtual void ConfigPreset() {}
	virtual void ConfigPostset() {}

	int SetConfig(int port_offset, const string& config_prefix);
	void DumpConfig();

	const string& TorHostname();
};


class ControlService : public ServiceBase
{
public:
	ControlService(string n, string s)
		: ServiceBase(n, s)
	{ }

	void ConfigPostset()
	{
		tor_advertise = false;
	}
};

class TorControlService : public ServiceBase
{
public:
	TorControlService(string n, string s)
		: ServiceBase(n, s)
	{ }

	void ConfigPostset()
	{
		tor_advertise = false;
	}
};
