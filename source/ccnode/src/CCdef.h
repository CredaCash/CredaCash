/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCdef.h
*/

#pragma once

//#define TEST_SMALL_BUFS	1

#ifndef TEST_SMALL_BUFS
#define TEST_SMALL_BUFS		0	// don't test
#endif

#define CCVERSION "0.90"

#ifdef DECLARE_EXTERN
#define DECLARING_EXTERN
#else
#define DECLARE_EXTERN extern
#endif

#define _LARGEFILE64_SOURCE

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#define WINVER		 0x0502
#define _WIN32_WINNT 0x0502
#define _WIN32_IE	 0x0500

#define _UNICODE

#include <windows.h>
#include <Shlobj.h>

#define PATH_DELIMITER	"\\"

#else

#define PATH_DELIMITER	"/"

#endif

#define CONCAT_TOKENS(a,b)	a##b
#define WIDE(x)				CONCAT_TOKENS(L,x)

#include <cstdlib>
#include <limits>
#include <cstdint>
#include <climits>
#include <string>
#include <array>
#include <vector>
#include <deque>
#include <sstream>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <exception>

#include <unistd.h>
#include <wchar.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BOOST_FILESYSTEM_NO_DEPRECATED
#include <boost/system/error_code.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options/variables_map.hpp>

#include <CCassert.h>
#include <CCticks.hpp>

#include "CCutil.h"

using namespace std;
using namespace boost::log::trivial;

#define LOCALHOST				"127.0.0.1"
#define TOR_SUBDIR				PATH_DELIMITER "tor"
#define TOR_HOSTNAMES_SUBDIR	TOR_SUBDIR PATH_DELIMITER "hostnames"

DECLARE_EXTERN volatile bool g_shutdown;
DECLARE_EXTERN char g_bigbuf[256*1024];

DECLARE_EXTERN struct global_params_struct
{
	boost::program_options::variables_map config_options;

	wstring process_dir;
	wstring app_data_dir;

	wstring tor_exe;
	wstring tor_config;

	wstring directory_servers_file;

	wstring	genesis_data_file;
	int		genesis_nwitnesses;
	int		genesis_maxmal;

	int		base_port;
	int		torproxy_port;

	uint64_t query_work_difficulty;
	uint64_t tx_work_difficulty;

	int		tx_validation_threads;

	int		trace_level;
	bool	trace_tx_server;
	bool	trace_relay;
	bool	trace_block_serve;
	bool	trace_block_sync;
	bool	trace_host_dir;
	bool	trace_witness;
	bool	trace_tx_validation;
	bool	trace_block_validation;
	bool	trace_serialnum_check;
	bool	trace_commitments;
	bool	trace_delibletx_check;
	bool	trace_blockchain;
	bool	trace_persistent_db;
	bool	trace_wal_db;
	bool	trace_pending_serialnum_db;
	bool	trace_relay_db;
	bool	trace_validation_q_db;
	bool	trace_validobj_db;
	bool	trace_expire;

} g_params;

#ifdef DECLARING_EXTERN

// declare global the singletons

#include "service_base.hpp"
#include "transact.hpp"
#include "relay.hpp"
#include "blockserve.hpp"
#include "blocksync.hpp"
#include "hostdir.hpp"

#define TOR_TRANSACT_SUBDIR		TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "transact"
#define TOR_RELAY_SUBDIR		TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "relay"
#define TOR_WITNESS_SUBDIR		TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "witness"
#define TOR_BLOCKSERVE_SUBDIR	TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "blockserve"
#define TOR_NODE_CONTROL_SUBDIR	TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "node-control"
#define TOR_TOR_CONTROL_SUBDIR	TOR_HOSTNAMES_SUBDIR PATH_DELIMITER "tor-control"

class TransactService g_transact_service("Tx-service", TOR_TRANSACT_SUBDIR);
class RelayService g_relay_service("Relay", TOR_RELAY_SUBDIR, false);
class RelayService g_privrelay_service("Private-relay", TOR_WITNESS_SUBDIR, true);
class BlockService g_blockserve_service("Blockserve", TOR_BLOCKSERVE_SUBDIR);
class BlockSyncClient g_blocksync_client("Blocksync", "");
class ControlService g_control_service("Node-control", TOR_NODE_CONTROL_SUBDIR);
class TorControlService g_tor_control_service("Tor-control", TOR_TOR_CONTROL_SUBDIR);
class HostDir g_hostdir;

#else

DECLARE_EXTERN class TransactService g_transact_service;
DECLARE_EXTERN class RelayService g_relay_service;
DECLARE_EXTERN class RelayService g_privrelay_service;
DECLARE_EXTERN class BlockService g_blockserve_service;
DECLARE_EXTERN class BlockSyncClient g_blocksync_client;
DECLARE_EXTERN class ControlService g_control_service;
DECLARE_EXTERN class TorControlService g_tor_control_service;
DECLARE_EXTERN class HostDir g_hostdir;

#endif
