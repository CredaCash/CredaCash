/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * ccnode.h
*/

#pragma once

#define CCAPPNAME	"CredaCash node"
#define CCVERSION	"2.1.0" //@@!
#define CCEXENAME	"ccnode"
#define CCAPPDIR	"CCNode2-#" //@@!

#define TRANSACT_PORT		0
#define RELAY_PORT			1
#define PRIVRELAY_PORT		2
#define BLOCKSERVE_PORT		3
#define NODE_CONTROL_PORT	4
#define TOR_CONTROL_PORT	5
#define TOR_PORT			6

//!#define TEST_SMALL_BUFS	1

#ifndef TEST_SMALL_BUFS
#define TEST_SMALL_BUFS		0	// don't test
#endif

#include <CCdef.h>
#include <CCboost.hpp>
#include <apputil.h>
#include <osutil.h>
#include <ccserver/torservice.hpp>

#include <boost/program_options/variables_map.hpp>

DECLARE_EXTERN char g_bigbuf[256*1024];

DECLARE_EXTERN struct global_params_struct
{
	boost::program_options::variables_map *config_options = NULL;

	wstring process_dir;
	wstring app_data_dir;
	wstring proof_key_dir;

	wstring tor_exe;
	wstring tor_config;

	int		base_port;
	int		torproxy_port;

	wstring rendezvous_servers_file;
	long long rendezvous_server_difficulty;
	long long rendezvous_magic_nonce;

	wstring history_data_file;

	wstring	genesis_data_file;
	int		genesis_nwitnesses;
	int		genesis_maxmal;

	uint64_t server_version;
	uint64_t protocol_version;
	uint64_t params_last_modified_level;

	uint64_t blockchain;
	uint32_t default_domain;
	int32_t  max_param_age;
	uint64_t tx_work_difficulty;
	uint64_t xcx_naked_buy_work_difficulty;
	uint64_t xcx_pay_work_difficulty;

	int		max_obj_mem;
	int		tx_validation_threads;
	int		block_future_tolerance;
	int		db_checkpoint_sec;
	bool	index_txouts;
	bool	index_mint_donations;
	bool	test1;

	int		trace_level;
	bool	trace_tx_server;
	bool	trace_relay;
	bool	trace_block_serve;
	bool	trace_block_sync;
	bool	trace_host_dir;
	bool	trace_witness;
	bool	trace_tx_validation;
	bool	trace_xreq_processing;
	bool	trace_block_validation;
	bool	trace_serialnum_check;
	bool	trace_commitments;
	bool	trace_exchange;
	bool	trace_exchange_mining;
	bool	trace_delibletx_check;
	bool	trace_blockchain;
	bool	trace_foreign_rpc;
	bool	trace_foreign_conn;
	bool	trace_persistent_db_reads;
	bool	trace_persistent_db_writes;
	bool	trace_wal_db;
	bool	trace_pending_serialnum_db;
	bool	trace_relay_db;
	bool	trace_validation_q_db;
	bool	trace_validobj_db;
	bool	trace_exchange_db;
	bool	trace_expire;

} g_params;

#ifdef DECLARING_EXTERN

// declare global singletons

#include "transact.hpp"
#include "relay.hpp"
#include "blockserve.hpp"
#include "blocksync.hpp"
#include "foreign-rpc.hpp"
#include "hostdir.hpp"

#define TOR_TRANSACT_SUBDIR		TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "transact"
#define TOR_RELAY_SUBDIR		TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "relay"
#define TOR_WITNESS_SUBDIR		TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "witness"
#define TOR_BLOCKSERVE_SUBDIR	TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "blockserve"
#define TOR_NODE_CONTROL_SUBDIR	TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "node-control"
#define TOR_TOR_CONTROL_SUBDIR	TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "tor-control"

vector<TorService*> g_tor_services;
class TransactService g_transact_service("Tx-service", g_params.app_data_dir, TOR_TRANSACT_SUBDIR);
class RelayService g_relay_service("Relay", g_params.app_data_dir, TOR_RELAY_SUBDIR, false);
class RelayService g_privrelay_service("Private-relay", g_params.app_data_dir, TOR_WITNESS_SUBDIR, true);
class BlockService g_blockserve_service("Blockserve", g_params.app_data_dir, TOR_BLOCKSERVE_SUBDIR);
class BlockSyncClient g_blocksync_client("Blocksync", g_params.app_data_dir, "");
class ForeignRpcClient g_foreignrpc_client("ForeignRpc", g_params.app_data_dir, "");
class ControlService g_control_service("Node-control", g_params.app_data_dir, TOR_NODE_CONTROL_SUBDIR);
class TorControlService g_tor_control_service("Tor-control", g_params.app_data_dir, TOR_TOR_CONTROL_SUBDIR);
class HostDir g_hostdir;

#else

DECLARE_EXTERN vector<TorService*> g_tor_services;
DECLARE_EXTERN class TransactService g_transact_service;
DECLARE_EXTERN class RelayService g_relay_service;
DECLARE_EXTERN class RelayService g_privrelay_service;
DECLARE_EXTERN class BlockService g_blockserve_service;
DECLARE_EXTERN class BlockSyncClient g_blocksync_client;
DECLARE_EXTERN class ForeignRpcClient g_foreignrpc_client;
DECLARE_EXTERN class ControlService g_control_service;
DECLARE_EXTERN class TorControlService g_tor_control_service;
DECLARE_EXTERN class HostDir g_hostdir;

#endif
