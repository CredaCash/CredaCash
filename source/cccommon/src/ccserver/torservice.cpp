/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * torservice.cpp
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "torservice.hpp"
#include "apputil.h"

void TorService::SetPorts(vector<TorService*>& services, unsigned base_port)
{
	auto port = base_port;

	for (auto service : services)
	{
		if (!service->no_port)
			service->port = port++;
	}
}

int TorService::TorAuthInt(const string& str)
{
	if (str.empty() || str == "none")
		return 0;
	else if (str == "basic")
		return 1;
	else if (str == "v3")
		return 2;
	else
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: invalid Tor hidden service auth value " << str;
		exit(-1);
		throw exception();
	}

	return -1;
}

const char* TorService::TorAuthString(int val)
{
	if (val == 0)
		return "none";
	else if (val == 1)
		return "basic";
	else if (val == 2)
		return "v3";
	else
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: invalid Tor hidden service auth value " << val;
		exit(-1);
		throw exception();
	}

	return "INVALID";
}

int TorService::SetConfig()
{
	if (max_outconns < 0)
		max_outconns = 0;

	if (max_inconns < 0)
		max_inconns = 0;

	if (max_outconns <= 0 && max_inconns <= 0)
		enabled = false;

	if (!enabled)
	{
		max_outconns = 0;
		max_inconns = 0;
	}

	if (!enabled || max_inconns <= 0)
	{
		tor_service = false;
	}
	else
	{
		try
		{
			address = boost::asio::ip::address::from_string(address_string);
		}
		catch (const exception& e)
		{
			BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: " << Name() << " Unable to parse address " << address_string;
			exit(-1);
			throw e;
			return -1;
		}
		//cout << "string " << address_string << " -> " << address << endl;
	}

	if (!tor_service)
	{
		tor_advertise = false;
	}
	else
	{
		tor_auth = TorAuthInt(tor_auth_string);

		if (tor_auth)
			tor_advertise = false;
		else
			tor_advertise = true;
	}

	ConfigPostset();

	return 0;
}

void TorService::DumpConfig() const
{
	cout << name << " service configuration:" << endl;
	cout << "   enabled = " << yesno(enabled) << endl;
	if (enabled)
	{
		DumpExtraConfigTop();

		if (max_inconns > 0)
		{
			cout << "   port = " << port << endl;
			cout << "   address = " << address << endl;
			// cout << "   password hash = " << stringorempty(password_string) << endl;	// not implemented
			cout << "   accessible as Tor hidden service = " << yesno(tor_service) << endl;

			if (tor_service)
			{
				cout << "   give hidden service a new hostname = " << yesno(tor_new_hostname) << endl;
				cout << "   hidden service auth method = " << TorAuthString(tor_auth) << endl;
				cout << "   anonymously advertise hidden service = " << yesno(tor_advertise) << endl;
			}
		}
		cout << "   max incoming connections = " << max_inconns << endl;
		cout << "   max outgoing connections = " << max_outconns << endl;
		cout << "   threads per connection = " << threads_per_conn << endl;

		DumpExtraConfigBottom();
	}
	cout << endl;
}

const string& TorService::TorHostname(bool override_advertise)
{
	if (!enabled || !tor_service || (!tor_advertise && !override_advertise))
		tor_hostname.clear();
	else if (tor_hostname.empty())
	{
		// open, read and close the file as quickly as possible so we don't collide with the Tor process

		tor_hostname.resize(88);

		wstring fname = app_data_dir + s2w(tor_hostname_subdir) + s2w(PATH_DELIMITER) + L"hostname";
		int fd = open_file(fname, O_RDONLY);

		if (fd == -1)
			tor_hostname.clear();
		else
		{
			auto rc = read(fd, &tor_hostname[0], tor_hostname.length()-1);
			(void)rc;
			close(fd);

			for (unsigned i = 0; i < tor_hostname.length(); ++i)
			{
				if (	(tor_hostname[i] < '0' || tor_hostname[i] > '9')
					 && (tor_hostname[i] < 'A' || tor_hostname[i] > 'Z')
					 && (tor_hostname[i] < 'a' || tor_hostname[i] > 'z'))

					tor_hostname[i] = 0;
			}

			tor_hostname.resize(strlen(tor_hostname.c_str()));

			BOOST_LOG_TRIVIAL(info) << Name() << " TorHostname for " << tor_hostname_subdir << " = " << tor_hostname;
		}
	}

	return tor_hostname;
}
