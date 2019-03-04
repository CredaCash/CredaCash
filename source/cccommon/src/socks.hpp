/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * socks.hpp
*/

#pragma once

#include <string>
#include <boost/asio.hpp>

#define SOCK_REPLY_SIZE		8

class Socks
{
private:
	Socks();
	~Socks();

public:

	static boost::asio::ip::basic_endpoint<boost::asio::ip::tcp> ConnectPoint(unsigned port);

	static std::string ConnectString(const std::string& dest, const std::string& toruser);

	static boost::system::error_code SendString(boost::asio::ip::tcp::socket& socket, const unsigned port, const std::string& str, std::string& reply);
};
