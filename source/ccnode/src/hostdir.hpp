/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * hostdir.hpp
*/

#pragma once

#include <boost/asio.hpp>
#include <jsoncpp/json/json.h>

class HostDir
{
public:
	enum HostType
	{
		Relay,
		Blockserve,
		N_HostTypes
	};

	bool Init();

	string GetHostName(HostType type);

private:
	void QueryServer();
	void ParseNameArray(Json::Value &root, const char* label, const HostType type);

	mutex classlock;
	vector<string> m_directory_servers;
	array<deque<string>, N_HostTypes> hostnames;
};
