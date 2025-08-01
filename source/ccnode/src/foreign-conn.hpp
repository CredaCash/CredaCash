/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * foreign-conn.hpp
*/

#pragma once

#include <ccserver/service.hpp>
#include <ccserver/connection.hpp>

#define WITNESS_FOREIGN_CONTENT_LENGTH_MAX	(400*1024)	//@@! note: for bitcoin, approx 1.5 inputs per KB -> 400 KB = 600 BTC inputs


class ForeignConnection : public CCServer::Connection
{
public:
	ForeignConnection(class CCServer::ConnectionManagerBase& manager, boost::asio::io_service& io_service, const class CCServer::ConnectionFactoryBase& connfac)
	 :	CCServer::Connection(manager, io_service, connfac),
		m_pquery(NULL)
	{
		m_terminator = 10;
		m_read_after_write = true;
	}

	const string *m_pquery;

	unsigned m_content_length;
	unsigned m_content_start;

private:

	unsigned m_parse_point;

	void StartConnection();
	void StartRead();
	void HandleReadComplete();
	void HandleContentReadComplete(const boost::system::error_code& e, size_t bytes_transferred, AutoCount pending_op_counter);
};
