/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * hostdir.hpp
*/

#pragma once

#include <boost/asio.hpp>
#include <jsoncpp/json/json.h>

class HostDir
{
public:
	HostDir()
	: m_socket(m_io_service)
	{ }

	enum HostType
	{
		Relay,
		Blockserve,
		N_HostTypes
	};

	bool Init();
	void DeInit();

	string GetHostName(HostType type);

private:
	void QueryServer();
	void ParseNameArray(Json::Value &root, const char* label, const HostType type);

	mutex classlock;
	vector<string> m_directory_servers;
	array<deque<string>, N_HostTypes> hostnames;

	boost::asio::io_service m_io_service;
	boost::asio::ip::tcp::socket m_socket;
	atomic<bool> m_query_in_progress;
};
