/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * txconn.hpp
*/

#pragma once

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

class TxConnection : public CCServer::Connection
{
public:
	TxConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	CCServer::Connection(manager, io_service, connfac),
		m_pquery(NULL),
		m_result_code(-1)
	{
		m_read_after_write = true;
	}

	vector<char> *m_pquery;
	bool m_data_written;
	int m_result_code;

private:

	void StartConnection();
	void HandleReadComplete();
};
