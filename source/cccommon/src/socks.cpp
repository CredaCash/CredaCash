/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * socks.cpp
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "socks.hpp"
#include "CCcrypto.hpp"

#define TOR_PROXY_TIMEOUT	140

boost::asio::ip::basic_endpoint<boost::asio::ip::tcp> Socks::ConnectPoint(unsigned port)
{
	//static boost::asio::ip::address localhost = boost::asio::ip::address::from_string(LOCALHOST);

	return boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>(boost::asio::ip::address_v4::loopback(), port);
}

string Socks::ConnectString(const string& dest, const string& toruser)
{
	static const char start[] = "\x04\x01\x01\xBB\x00\x00\x00\x01";	// socks4, connect, port 443, ip 0.0.0.1
	string result = string(start, sizeof(start)-1);

	if (toruser.length())
		result += toruser;					// proxy user id
	else
	{
		char user[21];
		PseudoRandomLetters(user, sizeof(user) - 1);
		user[sizeof(user) - 1] = 0;
		result += user;						// proxy user id
	}

	result.push_back(0);
	result += dest + ".onion";				// destination
	result.push_back(0);

	return result;
}

// socks4a reply should be 8 bytes. on success, 2nd byte should be 0x90

boost::system::error_code Socks::SendString(boost::asio::ip::tcp::socket& socket, const unsigned port, const string& str, string& reply)
{
	size_t ntotal = 0;
	boost::system::error_code e;

	{
		auto dest = ConnectPoint(port);

		BOOST_LOG_TRIVIAL(trace) << "Socks::SendString torproxy synchronous connect localhost port " << port << "...";

		socket.connect(dest, e);	// !!! need to make this connect async?

		if (e)
		{
			BOOST_LOG_TRIVIAL(info) << "Socks::SendString torproxy connect error " << e << " " << e.message();
			goto done;
		}

		BOOST_LOG_TRIVIAL(trace) << "Socks::SendString torproxy connected";

		//BOOST_LOG_TRIVIAL(trace) << "sending " << s2hex(str);

		boost::asio::write(socket, boost::asio::buffer(str), e);
		if (e)
		{
			BOOST_LOG_TRIVIAL(error) << "Socks::SendString torproxy command write failed error " << e << " " << e.message();
			goto done;
		}

		BOOST_LOG_TRIVIAL(trace) << "Socks::SendString torproxy command write done";

		socket.non_blocking(true, e);
		if (e)
		{
			BOOST_LOG_TRIVIAL(error) << "Socks::SendString socket non_blocking failed error " << e << " " << e.message();
			goto done;
		}

		reply.resize(reply.capacity());

		for (unsigned count = 0; ; )
		{
			if (g_shutdown)
			{
				BOOST_LOG_TRIVIAL(info) << "Socks::SendString shutting down";
				e.assign(boost::system::errc::no_such_process, boost::system::system_category());
				goto done;
			}

			//BOOST_LOG_TRIVIAL(info) << "Socks::SendString recv have " << ntotal;

			auto nread = boost::asio::read(socket, boost::asio::buffer(&reply[ntotal], reply.size() - ntotal), boost::asio::transfer_at_least(1), e);

			if (e && e != boost::asio::error::would_block && e != boost::asio::error::try_again && e != boost::asio::error::interrupted)
			{
				BOOST_LOG_TRIVIAL(info) << "Socks::SendString torproxy read failed after " << ntotal << " bytes; error " << e << " " << e.message();
				goto done;
			}

			if (nread <= 0)
			{
				if (++count < TOR_PROXY_TIMEOUT)
				{
					sleep(1);

					continue;
				}

				BOOST_LOG_TRIVIAL(info) << "Socks::SendString torproxy read timeout after " << ntotal << " bytes";
				e.assign(boost::system::errc::timed_out, boost::system::system_category());
				goto done;
			}

			ntotal += nread;

			BOOST_LOG_TRIVIAL(trace) << "Socks::SendString read nbytes " << nread << " total " << ntotal;

			if (ntotal > 1 && reply[1] != 90)
			{
				if (reply[1] == 91)
					BOOST_LOG_TRIVIAL(info) << "Socks::SendString torproxy returned " << (unsigned)reply[1];
				else
					BOOST_LOG_TRIVIAL(error) << "Socks::SendString torproxy returned error " << (unsigned)reply[1];
				e.assign(reply[1], boost::system::generic_category());
				goto done;
			}

			for (unsigned i = ntotal - nread; i < ntotal; ++i)
				if (i >= SOCKS_REPLY_SIZE && reply[i] == 0)
					goto done;

			if (ntotal >= reply.size())
			{
				BOOST_LOG_TRIVIAL(error) << "Socks::SendString read buffer overflow; read " << nread << " total " << ntotal << " size " << reply.size();
				e.assign(boost::system::errc::value_too_large, boost::system::system_category());
				goto done;
			}
		}
	}

done:

	if (socket.is_open())
	{
		boost::system::error_code ec;
		socket.close(ec);
	}

	reply.resize(ntotal);
	reply.replace(0, SOCKS_REPLY_SIZE, "");

	if (!e && ntotal < SOCKS_REPLY_SIZE)
	{
		BOOST_LOG_TRIVIAL(error) << "Socks::SendString torproxy command returned only " << ntotal << " bytes";
		e.assign(boost::system::errc::no_message, boost::system::system_category());
	}

	if (!e)
	{
		ntotal -= SOCKS_REPLY_SIZE;
		BOOST_LOG_TRIVIAL(trace) << "Socks::SendString read " << ntotal << " bytes: " << s2hex(reply);
	}

	return e;
}
