/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * txrpc.cpp
*/

#include "ccwallet.h"
#include "txrpc.h"
#include "txrpc_btc.h"
#include "rpc_errors.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "btc_block.hpp"
#include "txparams.hpp"
#include "txquery.hpp"
#include "walletdb.hpp"

#include <jsonutil.h>

#define TRACE_TX	(g_params.trace_txrpc)

using namespace snarkfront;

static RPC_Exception txrpc_invalid_destination_error(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet destination");

void cc_mint(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_mint";

	Transaction tx;

	tx.CreateTxMint(dbconn, txquery);

	rstream << tx.GetBtcTxid();
}

void cc_poll_destination(string destination, uint64_t last_receive_max, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "cc_poll_destination destination " << destination << " last_receive_max " << last_receive_max;

	if (last_receive_max == 0)
		last_receive_max = 48 * 3600;

	bigint_t dest;
	uint64_t dest_chain;
	auto rc = Secret::DecodeDestination(destination, dest_chain, dest);
	if (rc) throw txrpc_invalid_destination_error;

	Secret secret;
	rc = dbconn->SecretSelectSecret(&dest, TX_INPUT_BYTES, secret);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (rc) throw txrpc_invalid_destination_error;

	if (secret.dest_chain != dest_chain)
		throw txrpc_invalid_destination_error;

	//bool watchonly = secret.TypeIsWatchOnlyDestination();
	//bool ismine = (watchonly || secret.type == SECRET_TYPE_SPENDABLE_DESTINATION);

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Checking for new transactions to all addresses associated with this destination...\n" << endl;
	}

	rc = Secret::PollDestination(dbconn, txquery, secret.id, last_receive_max);
	if (rc < 0) throw txrpc_wallet_error;

	//rstream << "done";
}
