/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * txconn.cpp
*/

#include "ccwallet.h"
#include "txconn.hpp"
#include "walletdb.hpp"

#define TRACE_TXCONN	(g_params.trace_txconn)

void TxConnection::StartConnection()
{
	CCASSERT(m_pquery);

	m_conn_state = CONN_CONNECTED;

	auto nbytes = *(uint32_t*)m_pquery->data();
	CCASSERT(nbytes <= m_pquery->size());

	if (TRACE_TXCONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxConnection::StartConnection request nbytes " << nbytes;

	// this function could call SetTimer, but instead it just leaves connect timer running

	// send the request

	if (!WriteAsync("TxConnection::StartConnection", boost::asio::buffer(m_pquery->data(), nbytes),
			boost::bind(&Connection::HandleWrite, this, boost::asio::placeholders::error, AutoCount(this))))
	{
		m_data_written = true;
	}

	m_pquery = NULL;
}

void TxConnection::HandleReadComplete()
{
	// !!! add simulated errors???

	if (TRACE_TXCONN) BOOST_LOG_TRIVIAL(trace) << Name() << " Conn " << m_conn_index << " TxConnection::HandleReadComplete read " << m_nred;

	m_result_code = 0;

	Connection::HandleReadComplete();
}
