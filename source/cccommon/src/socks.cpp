/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * socks.cpp
*/

#include "socks.hpp"
#include "CCcrypto.hpp"
#include "CCutil.h"

#include <boost/system/error_code.hpp>
#include <boost/log/trivial.hpp>

#include <string>
#include <iostream>

using namespace std;
using namespace boost::log::trivial;

boost::asio::ip::basic_endpoint<boost::asio::ip::tcp> Socks::ConnectPoint(unsigned port)
{
	//static boost::asio::ip::address localhost = boost::asio::ip::address::from_string("127.0.0.1");

	return boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>(boost::asio::ip::address_v4::loopback(), port);
}

const string Socks::UsernamePrefix()
{
	//static string prefix;		// if the prefix string is static, this function can return a const string&

	string prefix;				// pick a unique username for every connection to isolate circuits

	if (prefix.empty())
		prefix = PseudoRandomLetters(12);

	BOOST_LOG_TRIVIAL(trace) << "Socks::UsernamePrefix = " << prefix;

	return prefix;
}

string Socks::ConnectString(const string& dest)
{
	static char start[] = "\x04\x01\x01\xBB\x00\x00\x00\x01";	// socks4, connect, port 443, ip 0.0.0.1
	string result = string(start, sizeof(start)-1);
	result += UsernamePrefix() + dest;		// user id
	result.push_back(0);
	result += dest + ".onion";					// destination
	result.push_back(0);

	return result;
}

// socks4a reply should be 8 bytes. on success, 2nd byte should be 0x90

boost::system::error_code Socks::SendString(const unsigned port, const string& str, string& reply)
{
	size_t ntotal = 0;
	boost::system::error_code e;

	{
		auto dest = ConnectPoint(port);

		boost::asio::io_service io_service;
		boost::asio::ip::tcp::socket socket(io_service);

		// !!! make this asynchronous?
		// !!! because it is synchronous, it can take a while to finish and lead to a long delay when shutting down

		socket.connect(dest, e);
		if (e)
		{
			BOOST_LOG_TRIVIAL(warning) << "torproxy connect failed error " << e << " " << e.message();
			goto done;
		}

		//BOOST_LOG_TRIVIAL(trace) << "sending " << s2hex(str);

		boost::asio::write(socket, boost::asio::buffer(str), e);
		if (e)
		{
			BOOST_LOG_TRIVIAL(error) << "torproxy command write failed error " << e << " " << e.message();
			goto done;
		}

		array<unsigned char, SOCK_REPLY_SIZE> socksreply;

		size_t nread = boost::asio::read(socket, boost::asio::buffer(socksreply), boost::asio::transfer_exactly(SOCK_REPLY_SIZE), e);

		if (e)
		{
			BOOST_LOG_TRIVIAL(warning) << "torproxy command read failed error " << e << " " << e.message();
			goto done;
		}

		if (nread < SOCK_REPLY_SIZE)
		{
			BOOST_LOG_TRIVIAL(error) << "torproxy command returned only " << nread << " bytes";
			e.assign(boost::system::errc::no_message, boost::system::system_category());
			goto done;
		}

		if (socksreply[1] != 90)
		{
			if (socksreply[1] == 91)
				BOOST_LOG_TRIVIAL(info) << "torproxy command returned " << (unsigned)socksreply[1];
			else
				BOOST_LOG_TRIVIAL(error) << "torproxy command returned error " << (unsigned)socksreply[1];
			e.assign(socksreply[1], boost::system::generic_category());
			goto done;
		}

		reply.resize(reply.capacity());

		while (true)
		{
			size_t nread = boost::asio::read(socket, boost::asio::buffer(&reply[ntotal], reply.size() - ntotal), boost::asio::transfer_at_least(1), e);

			if (e)
			{
				BOOST_LOG_TRIVIAL(info) << "torproxy server read failed error " << e << " " << e.message();
				goto done;
			}

			ntotal += nread;

			if (nread < 1)
			{
				BOOST_LOG_TRIVIAL(error) << "torproxy server read returned " << nread << " bytes";
				e.assign(boost::system::errc::no_message_available, boost::system::system_category());
				goto done;
			}

			for (unsigned i = ntotal - nread; i < ntotal; ++i)
				if (reply[i] == 0)
					goto done;

			if (ntotal >= reply.size())
			{
				BOOST_LOG_TRIVIAL(error) << "torproxy server read buffer overflow; read " << nread << " total " << ntotal << " size " << reply.size();
				e.assign(boost::system::errc::value_too_large, boost::system::system_category());
				goto done;
			}
		}
	}

done:

	reply.resize(ntotal);

	//BOOST_LOG_TRIVIAL(trace) << "torproxy read " << ntotal << " bytes: " << s2hex(reply);

	return e;
}
