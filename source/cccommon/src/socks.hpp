/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * socks.hpp
*/

#pragma once

#include <string>
#include <boost/asio.hpp>

#define SOCKS_REPLY_SIZE	8

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
