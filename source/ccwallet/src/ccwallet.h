/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * ccwallet.h
*/

#pragma once

#define CCAPPNAME	"CredaCash RPC Wallet"
#define CCVERSION	 "0.92" //@@!
#define CCEXENAME	"ccwallet"
#define CCAPPDIR	"CCWallet"

#define DEFAULT_BASE_PORT	9423

#define WALLET_RPC_PORT		"0"
#define TOR_CONTROL_PORT	"1"
#define TOR_PORT			"2"

#include <CCdef.h>
#include <CCboost.hpp>
#include <CCbigint.hpp>
#include <osutil.h>
#include <ccserver/torservice.hpp>

#include <boost/program_options/variables_map.hpp>

DECLARE_EXTERN thread_local bool g_interactive;

DECLARE_EXTERN struct global_params_struct
{
	boost::program_options::variables_map config_options;

	snarkfront::bigint_t wallet_id;

	bool	interactive;

	wstring process_dir;
	wstring app_data_dir;
	string wallet_file;

	int secret_gen_time;
	int secret_gen_memory;

	int		base_port;

	string	transact_host;
	int		transact_port;
	bool	transact_tor;
	bool	transact_tor_single_query;
	wstring	transact_tor_hosts_file;

	int tx_query_retries;
	int tx_submit_retries;

	int billet_wait_time;
	int cleared_confirmations;

	int polling_addresses;
	int polling_threads;

	array<array<vector<pair<unsigned, unsigned>>, 2>, 5> polling_table;

	wstring tor_exe;
	wstring tor_config;
	int		torproxy_port;

	int		trace_level;
	bool	trace_lpcserve;
	bool	trace_rpcserve;
	bool	trace_jsonrpc;
	bool	trace_txrpc;
	bool	trace_transactions;
	bool	trace_billets;
	bool	trace_totals;
	bool	trace_secrets;
	bool	trace_accounts;
	bool	trace_polling;
	bool	trace_txparams;
	bool	trace_txquery;
	bool	trace_txconn;
	bool	trace_db;

} g_params;

#ifdef DECLARING_EXTERN

// declare global singletons

#include <ccserver/torservice.hpp>

#include "lpcserve.hpp"
#include "rpcserve.hpp"
#include "btc_block.hpp"

#define TOR_WALLET_RPC_SUBDIR	TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "wallet-rpc"
#define TOR_TOR_CONTROL_SUBDIR	TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "tor-control"

vector<TorService*> g_tor_services;
class LpcService g_lpc_service("WalletLPC", g_params.app_data_dir, TOR_WALLET_RPC_SUBDIR);
class RpcService g_rpc_service("WalletRPC", g_params.app_data_dir, TOR_WALLET_RPC_SUBDIR);
class TorControlService g_tor_control_service("Tor-control", g_params.app_data_dir, TOR_TOR_CONTROL_SUBDIR);
class BtcBlock g_btc_block;

#else

DECLARE_EXTERN vector<TorService*> g_tor_services;
DECLARE_EXTERN class LpcService g_lpc_service;
DECLARE_EXTERN class RpcService g_rpc_service;
DECLARE_EXTERN class TorControlService g_tor_control_service;
DECLARE_EXTERN class BtcBlock g_btc_block;

#endif