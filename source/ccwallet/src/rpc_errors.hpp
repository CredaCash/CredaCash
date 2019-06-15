/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * rpc_errors.hpp
*/

#pragma once

#include "rpc_errors.h"

class RPC_Exception : public std::exception
{
public:
	RPCErrorCode code;
	const string msg;

	RPC_Exception(RPCErrorCode c, const char *m)
	:	code(c),
		msg(m)
	{ }

	RPC_Exception(RPCErrorCode c, const string& m)
	:	code(c),
		msg(m)
	{ }

	const char* what() const noexcept
	{
		return msg.c_str();
	}
};

extern const RPC_Exception txrpc_server_error;
extern const RPC_Exception txrpc_wallet_error;
extern const RPC_Exception txrpc_wallet_db_error;
extern const RPC_Exception txrpc_not_implemented_error;
extern const RPC_Exception txrpc_invalid_txid_error;
extern const RPC_Exception txrpc_simulated_error;

extern const RPC_Exception txrpc_block_not_found_err;
extern const RPC_Exception txrpc_invalid_address;
extern const RPC_Exception txrpc_tx_not_in_block;
extern const RPC_Exception txrpc_block_height_range_err;
extern const RPC_Exception txrpc_insufficient_funds;
extern const RPC_Exception txrpc_tx_rejected;
