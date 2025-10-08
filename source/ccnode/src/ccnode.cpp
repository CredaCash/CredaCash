/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * ccnode.cpp
*/

#define DECLARE_EXTERN

#include "ccnode.h"
#include "blockchain.hpp"
#include "processblock.hpp"
#include "processtx.hpp"
#include "process-xreq.hpp"
#include "foreign-query.hpp"
#include "witness.hpp"
#include "expire.hpp"

#include <CCproof.h>
#include <CCmint.h>

#include <tor.h>
#include <ccserver/connection_registry.hpp>

#include <dblog.h>
#include <sqlite/sqlite3.h>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>

//#define TEST_SKIP_RELAY_CONNS_CHECK		1
//#define TEST_SKIP_OBJ_MEM_CHECK			1

//#define TEST_TX_DIFFICULTY_ZERO			1

#ifndef TEST_SKIP_RELAY_CONNS_CHECK
#define TEST_SKIP_RELAY_CONNS_CHECK		0	// don't skip
#endif

#ifndef TEST_SKIP_OBJ_MEM_CHECK
#define TEST_SKIP_OBJ_MEM_CHECK			0	// don't skip
#endif

#ifndef TEST_TX_DIFFICULTY_ZERO
#define TEST_TX_DIFFICULTY_ZERO			0	// don't test
#endif

#define DEFAULT_TX_VALIDATION_THREADS	16

#define DEFAULT_DIR_SERVERS_FILE			"rendezvous-#.lis"
#define DEFAULT_GENESIS_DATA_FILE			"genesis-#.dat"
#define DEFAULT_HISTORY_DATA_FILE			"history-#.dat"
#define DEFAULT_PRIVATE_RELAY_HOSTS_FILE	"private_relay_hosts.lis"

#define DEFAULT_TRACE_LEVEL	4
#define TRACE_SHUTDOWN		1

#if 0 // not yet used
static void set_storage()
{
	g_store_blocks = 1;
	g_store_created = 1;
	g_store_spent = 1;

	if (g_params.config_options->count("micronode"))
	{
		g_store_blocks = 0;
		g_store_created = 0;
		g_store_spent = 0;
	}

	if (g_params.config_options->count("mininode"))
	{
		g_store_blocks = 0;
		g_store_created = 0;
		g_store_spent = 1;
	}

	if (g_params.config_options->count("store-blocks"))
		g_store_blocks = g_params.config_options->at("store-blocks").as<int>();

	if (g_params.config_options->count("store-created"))
		g_store_created = g_params.config_options->at("store-created").as<int>();

	if (g_params.config_options->count("store-spent"))
		g_store_spent = g_params.config_options->at("store-spent").as<int>();
}
#endif

void set_service_pre_configs()
{
	g_tor_services.clear();

	g_tor_services.push_back(&g_transact_service);
	g_tor_services.push_back(&g_relay_service);
	g_tor_services.push_back(&g_privrelay_service);
	g_tor_services.push_back(&g_blockserve_service);
	g_tor_services.push_back(&g_blocksync_client);
	g_tor_services.push_back(&g_foreignrpc_client);
	g_tor_services.push_back(&g_control_service);
	g_tor_services.push_back(&g_tor_control_service);

	for (auto service : g_tor_services)
		service->ConfigPreset();
}

void set_service_configs()
{
	TorService::SetPorts(g_tor_services, g_params.base_port + TRANSACT_PORT);

	if (!g_params.torproxy_port)
		g_params.torproxy_port = g_tor_services[g_tor_services.size()-1]->port + 1;

	//cerr << "torproxy_port " << g_params.torproxy_port << endl;

	for (auto service : g_tor_services)
		service->SetConfig();
}

static void do_show_config()
{
	cout << endl;
	cout << "Configuration settings:" << endl;
	cout << "   process directory = " << w2s(g_params.process_dir) << endl;
	cout << "   data directory = " << w2s(g_params.app_data_dir) << endl;
	cout << "   proof key directory = " << w2s(g_params.proof_key_dir) << endl;
	cout << "   path to Tor exe = " << w2s(g_params.tor_exe) << endl;
	cout << "   path to Tor config file = " << w2s(g_params.tor_config) << endl;
	cout << "   rendezvous servers file = " << w2s(g_params.rendezvous_servers_file) << endl;
	cout << "   genesis block data file = " << w2s(g_params.genesis_data_file) << endl;
	if (g_params.blockchain == MAINNET_BLOCKCHAIN)
		cout << "   blockchain history data file = " << w2s(g_params.history_data_file) << endl;
	//cout << "   store blocks = " << yesno(g_store_blocks) << endl;
	//cout << "   store created bills = " << yesno(g_store_created) << endl;
	//cout << "   store spent bills = " << yesno(g_store_spent) << endl;
	cout << "   base port = " << g_params.base_port << endl;
	cout << "   rendezvous server difficulty = " << g_params.rendezvous_server_difficulty << endl;
	cout << "   max object memory in MB = " << g_params.max_obj_mem << endl;
	cout << "   tx validation threads = " << g_params.tx_validation_threads << endl;
	cout << "   block future tolerance = " << g_params.block_future_tolerance << endl;
	cout << "   db checkpoint interval = " << g_params.db_checkpoint_sec << endl;
	cout << "   index transaction outputs = " << yesno(g_params.index_txouts) << endl;

	cout << endl;

	for (auto service : g_tor_services)
		service->DumpConfig();

	if (IsWitness())
	{
	cout << "Witness settings:" << endl;
	cout << "   witness index = " << g_witness.witness_index << endl;
	cout << "   block time ms = " << g_witness.block_time_ms << endl;
	cout << "   min block work ms = " << g_witness.block_min_work_ms << endl;
	cout << "   idle block secs = " << g_witness.block_max_time << endl;
	cout << "   test random block ms = " << g_witness.test_block_random_ms << endl;
	cout << "   witness malicious test = " << yesno(g_witness.test_mal) << endl;
	cout << endl;
	}

	cout << "Trace output settings:" << endl;
	cout << "   trace level = " << g_params.trace_level << endl;
	cout << "   trace transaction server = " << yesno(g_params.trace_tx_server) << endl;
	cout << "   trace relay service = " << yesno(g_params.trace_relay) << endl;
	cout << "   trace block server service = " << yesno(g_params.trace_block_serve) << endl;
	cout << "   trace block sync service = " << yesno(g_params.trace_block_sync) << endl;
	cout << "   trace host directory queries = " << yesno(g_params.trace_host_dir) << endl;
	cout << "   trace witness = " << yesno(g_params.trace_witness) << endl;
	cout << "   trace tx validation = " << yesno(g_params.trace_tx_validation) << endl;
	cout << "   trace exchange request processing = " << yesno(g_params.trace_xreq_processing) << endl;
	cout << "   trace block validation = " << yesno(g_params.trace_block_validation) << endl;
	cout << "   trace serialnum check = " << yesno(g_params.trace_serialnum_check) << endl;
	cout << "   trace commitments = " << yesno(g_params.trace_commitments) << endl;
	cout << "   trace exchange = " << yesno(g_params.trace_exchange) << endl;
	cout << "   trace exchange mining = " << yesno(g_params.trace_exchange_mining) << endl;
	cout << "   trace delible tx_check = " << yesno(g_params.trace_delibletx_check) << endl;
	cout << "   trace blockchain = " << yesno(g_params.trace_blockchain) << endl;
	cout << "   trace foreign RPC queries = " << yesno(g_params.trace_foreign_rpc) << endl;
	cout << "   trace foreign RPC communication = " << yesno(g_params.trace_foreign_conn) << endl;
	cout << "   trace persistent DB reads = " << yesno(g_params.trace_persistent_db_reads) << endl;
	cout << "   trace persistent DB writes = " << yesno(g_params.trace_persistent_db_writes) << endl;
	cout << "   trace WAL DB operations = " << yesno(g_params.trace_wal_db) << endl;
	cout << "   trace pending serialnum DB = " << yesno(g_params.trace_pending_serialnum_db) << endl;
	cout << "   trace relay DB = " << yesno(g_params.trace_relay_db) << endl;
	cout << "   trace validation queue DB = " << yesno(g_params.trace_validation_q_db) << endl;
	cout << "   trace valid object DB = " << yesno(g_params.trace_validobj_db) << endl;
	cout << "   trace exchange DB = " << yesno(g_params.trace_exchange_db) << endl;
	cout << "   trace object expiration = " << yesno(g_params.trace_expire) << endl;
	cout << "   trace connections = " << yesno(g_trace_ccserver) << endl;
	cout << endl;
}

static void check_config_values()
{
	if (g_params.blockchain < 1 || (g_params.blockchain > (TX_CHAIN_BITS < 64 ? ((uint64_t)1 << TX_CHAIN_BITS) - 1 : (uint64_t)(-1))))
		throw range_error("Invalid value for blockchain");

	if (g_params.genesis_nwitnesses < 1 || g_params.genesis_nwitnesses > MAX_NWITNESSES)
		throw range_error("Invalid value for genesis nwitnesses");

	if (g_params.genesis_maxmal < 0 || g_params.genesis_maxmal >= (g_params.genesis_nwitnesses + 1) / 2)
		throw range_error("Invalid value for genesis malicious witness allowance");

	#if !TEST_SKIP_OBJ_MEM_CHECK

	if (g_params.max_obj_mem < 100 || g_params.max_obj_mem > 4096)
		throw range_error("Max object memory MB not in valid range");

	#endif

	if (g_params.tx_validation_threads < 1 || g_params.tx_validation_threads > 2000)
		throw range_error("Tx validation threads value not in valid range");

	if (g_params.block_future_tolerance < 1 || g_params.block_future_tolerance > 90000)
		throw range_error("Block time tolerance not in valid range");

	if (g_params.db_checkpoint_sec < 0 || g_params.db_checkpoint_sec > 3600)
		throw range_error("Database checkpoint seconds not in valid range");

	if (g_transact_service.enabled && g_transact_service.info_only && g_transact_service.relay_only)
		throw range_error("transaction service info-only and relay-only cannot both be enabled");

	if (g_transact_service.enabled && !g_transact_service.relay_only && !g_params.index_txouts)
		throw range_error("transactions must be indexed for the transaction service to work correctly");

	if (g_params.base_port < 1 || g_params.base_port > 0xFFFF - TOR_PORT)
		throw range_error("baseport value not in valid range");

	if (g_transact_service.max_inconns < 0 || g_transact_service.max_inconns > 100000)
		throw range_error("Max connections for transaction service not in valid range");

	if (g_transact_service.threads_per_conn <= 0 || g_transact_service.threads_per_conn > 2)
		throw range_error("Max connections for transaction service not in valid range");

	if (g_transact_service.query_work_difficulty && g_transact_service.query_work_difficulty < ((uint64_t)1 << 38))
		throw range_error("Transaction server query work difficulty not in valid range");

	if (g_transact_service.max_net_sec < 0 || g_transact_service.max_net_sec > 100000)
		throw range_error("Transaction server maximum network seconds not in valid range");

	if (g_transact_service.max_block_sec < 0 || g_transact_service.max_block_sec > 1000000)
		throw range_error("Transaction server maximum indelible block age not in valid range");

	#if !TEST_SKIP_RELAY_CONNS_CHECK

	if (g_relay_service.enabled && g_relay_service.max_outconns < 4)
		throw range_error("Number of outgoing relay connections must be at least 4");

	if (g_relay_service.enabled && g_relay_service.max_inconns * 2 < 3 * g_relay_service.max_outconns)
		throw range_error("Maximum number of incoming relay connections must be at least 1.5 * the number of outgoing relay connections");

	#endif

	if (IsWitness())
	{
		if (!g_foreignrpc_client.enabled || g_foreignrpc_client.max_outconns < 1)
			throw range_error("Witness must have foreign RPC enabled");

		if (!g_foreignrpc_client.rpc_port[XREQ_BLOCKCHAIN_BTC])
			throw range_error("Witness must have Bitcoin (BTC) RPC enabled");

		if (!g_foreignrpc_client.rpc_port[XREQ_BLOCKCHAIN_BCH])
			throw range_error("Witness must have Bitcoin Cash (BCH) RPC enabled");

		if (g_foreignrpc_client.verify_level[XREQ_BLOCKCHAIN_BTC] < ForeignRpcClient::VerifyLevel::Strict_Relay)
			throw range_error("Witness must have Bitcoin (BTC) verify set to " + to_string(ForeignRpcClient::VerifyLevel::Strict_Relay) + " or higher");

		if (g_foreignrpc_client.verify_level[XREQ_BLOCKCHAIN_BCH] < ForeignRpcClient::VerifyLevel::Strict_Relay)
			throw range_error("Witness must have Bitcoin Cash (BCH) verify set to " + to_string(ForeignRpcClient::VerifyLevel::Strict_Relay) + " or higher");
	}
}

static int process_options(int argc, const char **argv)
{
	namespace po = boost::program_options;

	po::options_description basic_options("BASIC OPTIONS");
	basic_options.add_options()
		("help", "Display this message.")
		//("micronode", "Store minimum data needed to create transactions;\n"
		//	"note: when this is set, transactions can only be partially validated by this node.")
		//("mininode", "Store minimum data needed to create and validate transactions.")
		("show-config", "Show configuration information.")
		("dry-run", "Exit after parsing configuration.")
	;

	g_transact_service.password_string.clear();
	g_blockserve_service.password_string.clear();
	g_control_service.password_string.clear();
	g_tor_control_service.password_string.clear();

	g_params.app_data_dir.clear();
	g_params.rendezvous_servers_file.clear();
	g_params.genesis_data_file.clear();
	g_params.proof_key_dir.clear();
	g_params.tor_exe.clear();
	g_params.tor_config.clear();
	g_privrelay_service.priv_hosts_file.clear();

	po::options_description advanced_options("ADVANCED OPTIONS", 99, 0);
	advanced_options.add_options()
		("config", po::wvalue<wstring>(), "Path to file with additional configuration options.")
		("blockchain", po::value<uint64_t>(&g_params.blockchain)->default_value(MAINNET_BLOCKCHAIN), "Numeric identifier for blockchain; from " STRINGIFY(TESTNET_BLOCKCHAIN_LO) " to " STRINGIFY(TESTNET_BLOCKCHAIN_HI) " is a test network.")
		("datadir", po::wvalue<wstring>(&g_params.app_data_dir), "Path to program data directory; a \"#\" character in this path will be replaced by the blockchain number (default: \""
#if _WIN32
				"%LOCALAPPDATA%\\CredaCash\\" CCAPPDIR "\").")
#else
				"~/." CCAPPDIR "\").")
#endif
		("rendezvous-file", po::wvalue<wstring>(&g_params.rendezvous_servers_file), "Path to file containing a list of peer rendezvous servers; a \"#\" character in this path will be replaced by the blockchain number (default: \"" DEFAULT_DIR_SERVERS_FILE "\" in same directory as this program).")
		("rendezvous-difficulty", po::wvalue<long long>(&g_params.rendezvous_server_difficulty)->default_value(360000), "Proof of work difficulty for peer rendezvous server.")
		("genesis-file", po::wvalue<wstring>(&g_params.genesis_data_file), "Path to file containing data for the genesis block; a \"#\" character in this path will be replaced by the blockchain number (default: \"" DEFAULT_GENESIS_DATA_FILE "\" in same directory as this program).")
		("genesis-generate", "Generate new genesis block data files.")
		("genesis-nwitnesses", po::value<int>(&g_params.genesis_nwitnesses)->default_value(1), "Initial # of witnesses when generating new genesis block data files.")
		("genesis-maxmal", po::value<int>(&g_params.genesis_maxmal)->default_value(0), "Initial allowance for malicious witnesses when generating new genesis block data files.")
		("proof-key-dir", po::wvalue<wstring>(&g_params.proof_key_dir), "Path to zero knowledge proof keys; if set to \"env\", the environment variable " KEY_PATH_ENV_VAR " is used (default: the subdirectory \"" ZK_KEY_DIR "\" in same directory as this program).")
		("tor-exe", po::wvalue<wstring>(&g_params.tor_exe), "Path to Tor executable; if set to \"external\", Tor is not launched by this program, and must be launched and managed externally (default: \"" TOR_EXE "\" in same directory as this program).")
		("tor-port", po::value<int>(&g_params.torproxy_port)->default_value(0), "Tor proxy port (default baseport+" STRINGIFY(TOR_PORT) ").")
		("tor-config", po::wvalue<wstring>(&g_params.tor_config), "Path to Tor configuration file (default: \"" TOR_CONFIG "\" in same directory as this program).")
		("obj-memory-max", po::value<int>(&g_params.max_obj_mem)->default_value(500), "Maximum object (block and transaction) memory in MB.")
		("tx-validation-threads", po::value<int>(&g_params.tx_validation_threads)->default_value(-1), "Transaction validation threads (-1 = auto config).")
		("block-future-tolerance", po::value<int>(&g_params.block_future_tolerance)->default_value(3900), "Block future timestamp tolerance in seconds.")
		("db-checkpoint-sec", po::value<int>(&g_params.db_checkpoint_sec)->default_value(21), "Database checkpoint interval in seconds (0 = continuous).")
		("baseport", po::value<int>(&g_params.base_port)->default_value(0), (string("Base port for node interfaces\n")
			+ "(default: " STRINGIFY(BASE_PORT) "+20*(blockchain modulo " + to_string((BASE_PORT_TOP-BASE_PORT)/20) + ")"
			"; node software uses ports baseport through baseport+" STRINGIFY(TOR_PORT) ").").c_str())
		//("store-blocks", po::value<int>(), "Store entire blockchain;\ndefaults to 0 if micronode=1 or mininode=1.")
		//("store-created", po::value<int>(), "Store database of created bills;\ndefaults to 0 if micronode=1 or mininode=1.")
		//("store-spent", po::value<int>(), "Store database of spent bills\ndefaults to 0 if micronode=1 and mininode=0; if this is set to zero, transactions can only be partially validated by node.")
		("transact", po::value<bool>(&g_transact_service.enabled)->default_value(1), "Enable transact service for wallet applications (at port baseport).")
		("transact-info-only", po::value<bool>(&g_transact_service.info_only)->default_value(false), "Transact service only provides public blockchain info, will not relay transactions.")
		("transact-relay-only", po::value<bool>(&g_transact_service.relay_only)->default_value(false), "Transact service only relays transactions, will not provide blockchain info.")
		("transact-addr", po::value<string>(&g_transact_service.address_string)->default_value(LOCALHOST), "Network address for transaction support service;\n"
				"by default, this service is available from the localhost only;\n"
				"this setting can be used to bind to another address for access via the local network or internet.")
		//("transact-password", po::value<string>(&g_transact_service.password_string), "SHA1 hash of password to access transaction support service (default: no password required).")
		("transact-tor", po::value<bool>(&g_transact_service.tor_service)->default_value(0), "Make the transaction support service available as a Tor hidden service.")
		("transact-tor-auth", po::value<string>(&g_transact_service.tor_auth_string)->default_value("v3"), "Tor hidden service authentication method (none, basic, or v3).")
		("transact-conns", po::value<int>(&g_transact_service.max_inconns)->default_value(20), "Maximum number of incoming connections for transaction support service.")
		("transact-threads", po::value<float>(&g_transact_service.threads_per_conn)->default_value(1), "Threads per connection for transaction support service.")
		("transact-difficulty", po::value<uint64_t>(&g_transact_service.query_work_difficulty)->default_value(0), "Proof-of-work difficulty for transaction server queries (0 = none, otherwise lower numbers have more difficulty).")
		("transact-max-network-sec", po::value<int32_t>(&g_transact_service.max_net_sec)->default_value(420), "Maximum time in seconds since last block received for transaction server to be considered connected to the network (0 = disabled).")
		("transact-max-block-sec", po::value<int32_t>(&g_transact_service.max_block_sec)->default_value(3600), "Maximum timestamp age in seconds of last indelible block for transaction server to be considered connected to the network (0 = disabled).")
		("relay", po::value<bool>(&g_relay_service.enabled)->default_value(1), "Fetch and relay blocks and transactions (at port baseport+" STRINGIFY(RELAY_PORT) ");\n"
				"if no relay is enabled, this node will receive no updates and will only use data previously stored.")
		("relay-addr", po::value<string>(&g_relay_service.address_string)->default_value(LOCALHOST), "Network address for relay service;\n"
				"by default, this service binds to localhost and is available as a Tor hidden service;\n"
				"this setting can be used to bind to another address for direct access via the local network or internet.")
		("relay-out", po::value<int>(&g_relay_service.max_outconns)->default_value(8), "Target number of outgoing relay connections (must be at least 4).")
		("relay-in", po::value<int>(&g_relay_service.max_inconns)->default_value(16), "Maximum number of incoming relay connections (must be at least 1.5*relay-out).")
		("privrelay", po::value<bool>(&g_privrelay_service.enabled)->default_value(0), "Fetch and relay blocks and transactions (at port baseport+" STRINGIFY(PRIVRELAY_PORT) ");\n"
				"if no relay is enabled, this node will receive no updates and will only use data previously stored.")
		("privrelay-file", po::wvalue<wstring>(&g_privrelay_service.priv_hosts_file), "Path to file containing a list of private relay ip_address:port values or Tor onion hostnames (default: \"" DEFAULT_PRIVATE_RELAY_HOSTS_FILE "\").")
		("privrelay-host-index", po::value<int>(&g_privrelay_service.priv_host_index)->default_value(-1), "Index (zero-based) of this host in the hosts file (-1 = not applicable).")
		("privrelay-addr", po::value<string>(&g_privrelay_service.address_string)->default_value(LOCALHOST), "Network address for private relay service;\n"
				"by default, this service binds to localhost and is available as a Tor hidden service;\n"
				"this setting can be used to bind to another address for direct access via the local network or internet.")
		("privrelay-tor", po::value<bool>(&g_privrelay_service.tor_service)->default_value(0), "Make the private relay service available as a Tor hidden service.")
		("privrelay-tor-new-hostname", po::value<bool>(&g_privrelay_service.tor_new_hostname)->default_value(0), "Give the private relay Tor hidden service a new hostname.")
		("privrelay-tor-auth", po::value<string>(&g_privrelay_service.tor_auth_string)->default_value("none"), "Tor hidden service authentication method (none, basic, or v3).")
		("privrelay-in", po::value<int>(&g_privrelay_service.max_inconns)->default_value(-1), "Maximum number of incoming private relay connections  (-1 = auto config).")
		("blockserve", po::value<bool>(&g_blockserve_service.enabled)->default_value(1), "Provide blockchain data to other nodes (at port baseport+" STRINGIFY(BLOCKSERVE_PORT) ");\n"
				"note: this option is disabled when store-blocks=0.")
		("blockserve-addr", po::value<string>(&g_blockserve_service.address_string)->default_value(LOCALHOST), "Network address for blockchain service;\n"
				"by default, this service binds to localhost and is available as a Tor hidden service;\n"
				"this setting can be used to bind to another address for direct access via the local network or internet.")
		//("blockserve-password", po::value<string>(&g_blockserve_service.password_string), "SHA1 hash of password to access blockchain service (default: no password required).")
		("blockserve-tor", po::value<bool>(&g_blockserve_service.tor_service)->default_value(1), "Make the blockchain service available as a Tor hidden service.")
		("blockserve-tor-new-hostname", po::value<bool>(&g_blockserve_service.tor_new_hostname)->default_value(1), "Give the blockchain Tor hidden service a new hostname.")
		("blockserve-tor-auth", po::value<string>(&g_blockserve_service.tor_auth_string)->default_value("none"), "Tor hidden service authentication method (none, basic, or v3).")
		("blockserve-conns", po::value<int>(&g_blockserve_service.max_inconns)->default_value(1), "Maximum number of incoming connections for blockchain service.")
		("blocksync-conns", po::value<int>(&g_blocksync_client.max_outconns)->default_value(10), "Maximum number of outgoing connections for blockchain synchonization.")
		("foreign-rpc-conns", po::value<int>(&g_foreignrpc_client.max_outconns)->default_value(4), "Maximum number of outgoing connections for foreign blockchain RPC.")
		("foreign-btc-verify-level", po::value<int>(&g_foreignrpc_client.verify_level[XREQ_BLOCKCHAIN_BTC])->default_value(ForeignRpcClient::VerifyLevel::If_Possible), "Verification level for BTC exchange requests (0=No RPC, 1=If possible, 2=Strict Relay, 3=Strict Blocks).")
		("foreign-rpc-btc-port", po::value<int>(&g_foreignrpc_client.rpc_port[XREQ_BLOCKCHAIN_BTC])->default_value(0), "Local port for BTC node RPC.")
		("foreign-rpc-btc-username", po::value<string>(&g_foreignrpc_client.rpc_username[XREQ_BLOCKCHAIN_BTC])->default_value("btc"), "Username for BTC node RPC.")
		("foreign-rpc-btc-password", po::value<string>(&g_foreignrpc_client.rpc_password[XREQ_BLOCKCHAIN_BTC])->default_value(""), "Password for BTC node RPC.")
		("foreign-bch-verify-level", po::value<int>(&g_foreignrpc_client.verify_level[XREQ_BLOCKCHAIN_BCH])->default_value(ForeignRpcClient::VerifyLevel::If_Possible), "Verification level for BTC exchange requests (0=No RPC, 1=If possible, 2=Strict Relay, 3=Strict Blocks).")
		("foreign-rpc-bch-port", po::value<int>(&g_foreignrpc_client.rpc_port[XREQ_BLOCKCHAIN_BCH])->default_value(0), "Local port for BCH node RPC.")
		("foreign-rpc-bch-username", po::value<string>(&g_foreignrpc_client.rpc_username[XREQ_BLOCKCHAIN_BCH])->default_value("bch"), "Username for BCH node RPC.")
		("foreign-rpc-bch-password", po::value<string>(&g_foreignrpc_client.rpc_password[XREQ_BLOCKCHAIN_BCH])->default_value(""), "Password for BCH node RPC.")
		("witness-index", po::value<int>(&g_witness.witness_index)->default_value(-1), "Witness index (-1 = disable).")
		("witness-block-ms", po::value<int>(&g_witness.block_time_ms)->default_value(2000), "Nominal milliseconds between blocks.")
		("witness-block-min-work-ms", po::value<int>(&g_witness.block_min_work_ms)->default_value(200), "Minimum milliseconds to work assembling a block.")
		("witness-block-idle-sec", po::value<int>(&g_witness.block_max_time)->default_value(20), "Seconds between blocks when there are no transactions to witness.")
		("witness-test-block-random-ms", po::value<int>(&g_witness.test_block_random_ms)->default_value(0), "Test randomly generating blocks, with this average milliseconds between blocks (0 = disabled).")
		("witness-test-mal", po::value<bool>(&g_witness.test_mal)->default_value(0), "Act as a malicious witness.")
		//("control", po::value<bool>(&g_control_service.enabled)->default_value(0), "Allow other programs to control node operation (at port baseport+" STRINGIFY(NODE_CONTROL_PORT) ").")
		//("control-addr", po::value<string>(&g_control_service.address_string)->default_value(LOCALHOST), "Network address for node control service;\n"
		//		"by default, this service is available from the localhost only;\n"
		//		"this setting can be used to bind to another address for access via the local network or internet.")
		//("control-password", po::value<string>(&g_control_service.password_string), "SHA1 hash of password to access node control service (default: no password required).")
		//("control-tor", po::value<bool>(&g_control_service.tor_service)->default_value(0), "Make the node control port available through a Tor hidden service.")
		//("control-tor-auth", po::value<string>(&g_control_service.tor_auth_string)->default_value("v3"), "Tor hidden service authentication method (none, basic, or v3).")
		("tor-control", po::value<bool>(&g_tor_control_service.enabled)->default_value(0), "Allow other programs to control Tor (at port baseport+" STRINGIFY(TOR_CONTROL_PORT) ").")
		("tor-control-addr", po::value<string>(&g_tor_control_service.address_string)->default_value(LOCALHOST), "Network address for node control service;\n"
				"by default, this service is available from the localhost only;\n"
				"this setting can be used to bind to another address for access via the local network or internet.")
		("tor-control-password", po::value<string>(&g_tor_control_service.password_string), "SHA1 hash of password to access Tor control service (default: no password required).")
		("tor-control-tor", po::value<bool>(&g_tor_control_service.tor_service)->default_value(0), "Make the Tor control port available through a Tor hidden service.")
		("tor-control-tor-auth", po::value<string>(&g_tor_control_service.tor_auth_string)->default_value("v3"), "Tor hidden service authentication method (none, basic, or v3).")
		("trace", po::value<int>(&g_params.trace_level)->default_value(DEFAULT_TRACE_LEVEL), "Trace level; affects all trace settings (0=none, 1=fatal, 2=errors, 3=warnings, 4=info, 5=debug, 6=trace)")
		("trace-tx-server", po::value<bool>(&g_params.trace_tx_server)->default_value(1), "Trace transaction server")
		("trace-relay", po::value<bool>(&g_params.trace_relay)->default_value(1), "Trace relay service")
		("trace-block-serve", po::value<bool>(&g_params.trace_block_serve)->default_value(1), "Trace block server service")
		("trace-block-sync", po::value<bool>(&g_params.trace_block_sync)->default_value(1), "Trace block sync service")
		("trace-host-dir", po::value<bool>(&g_params.trace_host_dir)->default_value(1), "Trace host directory queries")
		("trace-witness", po::value<bool>(&g_params.trace_witness)->default_value(1), "Trace witness")
		("trace-tx-validation", po::value<bool>(&g_params.trace_tx_validation)->default_value(1), "Trace transaction validation")
		("trace-xreq-processing", po::value<bool>(&g_params.trace_xreq_processing)->default_value(1), "Trace exchange request processing")
		("trace-block-validation", po::value<bool>(&g_params.trace_block_validation)->default_value(1), "Trace block validation")
		("trace-serialnum-check", po::value<bool>(&g_params.trace_serialnum_check)->default_value(1), "Trace serial number check")
		("trace-commitments", po::value<bool>(&g_params.trace_commitments)->default_value(0), "Trace commitments")
		("trace-exchange", po::value<bool>(&g_params.trace_exchange)->default_value(0), "Trace exchange")
		("trace-exchange-mining", po::value<bool>(&g_params.trace_exchange_mining)->default_value(0), "Trace exchange mining")
		("trace-delibletx-check", po::value<bool>(&g_params.trace_delibletx_check)->default_value(0), "Trace delible tx check")
		("trace-blockchain", po::value<bool>(&g_params.trace_blockchain)->default_value(1), "Trace blockchain")
		("trace-foreign-rpc", po::value<bool>(&g_params.trace_foreign_rpc)->default_value(0), "Trace foreign RPC queries")
		("trace-foreign-conn", po::value<bool>(&g_params.trace_foreign_conn)->default_value(0), "Trace communication with foreign RPC server")
		("trace-persistent-db-reads", po::value<bool>(&g_params.trace_persistent_db_reads)->default_value(0), "Trace persistent DB reads")
		("trace-persistent-db-writes", po::value<bool>(&g_params.trace_persistent_db_writes)->default_value(0), "Trace persistent DB writes")
		("trace-wal-db", po::value<bool>(&g_params.trace_wal_db)->default_value(0), "Trace write-ahead-log DB operations")
		("trace-pending-serialnum-db", po::value<bool>(&g_params.trace_pending_serialnum_db)->default_value(0), "Trace pending serial number DB")
		("trace-relay-db", po::value<bool>(&g_params.trace_relay_db)->default_value(0), "Trace relay DB")
		("trace-validation-q-db", po::value<bool>(&g_params.trace_validation_q_db)->default_value(0), "Trace validation queue DB")
		("trace-valid-obj-db", po::value<bool>(&g_params.trace_validobj_db)->default_value(0), "Trace valid object DB")
		("trace-exchange-db", po::value<bool>(&g_params.trace_exchange_db)->default_value(0), "Trace exchange DB")
		("trace-expire", po::value<bool>(&g_params.trace_expire)->default_value(0), "Trace object expiration")
		("trace-connections", po::value<bool>(&g_trace_ccserver)->default_value(0), "Trace low level connection operations")
	;

	po::options_description hidden_options("");
	hidden_options.add_options()
		("db-index-txouts", po::value<bool>(&g_params.index_txouts)->default_value(1))
		("db-index-mint-donations", po::value<bool>(&g_params.index_mint_donations)->default_value(0))
		("rendezvous-magic-nonce", po::value<long long>(&g_params.rendezvous_magic_nonce)->default_value(0))
		("test1", po::value<bool>(&g_params.test1)->default_value(0))
	;

	po::options_description all;
	all.add(basic_options).add(advanced_options).add(hidden_options);

	if (g_params.config_options) delete g_params.config_options;
	g_params.config_options = new po::variables_map;

	po::store(po::parse_command_line(argc, argv, all), *g_params.config_options);

	if (g_params.config_options->count("help"))
	{
		cerr << CCAPPNAME " v" CCVERSION << endl;
		cerr << basic_options << endl;
		cerr << advanced_options << endl;

		return 1;
	}

	if (g_params.config_options->count("config"))
	{
		boost::filesystem::ifstream fs;
		auto fname = g_params.config_options->at("config").as<wstring>();
		fs.open(fname, fstream::in);
		if(!fs.is_open())
			throw runtime_error(string("Unable to open config file \"") + w2s(fname) + "\"");

		po::store(po::parse_config_file(fs, all), *g_params.config_options);

		set_trace_level(g_params.trace_level);
	}

	po::notify(*g_params.config_options);

	set_trace_level(g_params.trace_level);

	//for (auto v : g_params.config_options)
	//	cout << "config option: " << v.first << endl;

	// !!! move these to a function

	if (!g_params.rendezvous_servers_file.length())
	{
		string def = DEFAULT_DIR_SERVERS_FILE;
		expand_number(def, g_params.blockchain);
		g_params.rendezvous_servers_file = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(def);
	}
	else
		expand_number_wide(g_params.rendezvous_servers_file, g_params.blockchain);

	if (!g_params.history_data_file.length())
	{
		string def = DEFAULT_HISTORY_DATA_FILE;
		expand_number(def, g_params.blockchain);
		g_params.history_data_file = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(def);
	}
	else
		expand_number_wide(g_params.history_data_file, g_params.blockchain);

	if (!g_params.genesis_data_file.length())
	{
		string def = DEFAULT_GENESIS_DATA_FILE;
		expand_number(def, g_params.blockchain);
		g_params.genesis_data_file = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(def);
	}
	else
		expand_number_wide(g_params.genesis_data_file, g_params.blockchain);

	if (!g_privrelay_service.priv_hosts_file.length())
		g_privrelay_service.priv_hosts_file = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(DEFAULT_PRIVATE_RELAY_HOSTS_FILE);

	if (g_privrelay_service.priv_host_index == -1)
		g_privrelay_service.priv_host_index = g_witness.witness_index;

	g_privrelay_service.ConfigPrivateRelay();

	if (!g_params.tor_exe.length())
		g_params.tor_exe = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(TOR_EXE);

	if (!g_params.tor_config.length())
		g_params.tor_config = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(TOR_CONFIG);

	g_params.server_version = (uint64_t)2 << 32;	//@@!
	g_params.protocol_version = (uint64_t)2 << 32;	//@@!
	g_params.params_last_modified_level = 0;

	g_params.default_domain = 1;						//@@!

	g_params.max_param_age = 16*60*60;
	//g_params.max_param_age = 30;	// for testing

	g_params.tx_work_difficulty = (uint64_t)1 << 41; //@@! larger number is easier
	g_params.xcx_naked_buy_work_difficulty = g_params.tx_work_difficulty >> 8; //@@!
	g_params.xcx_pay_work_difficulty = (uint64_t)3 << 37; //@@!
	if (IsTestnet(g_params.blockchain))
		g_params.xcx_naked_buy_work_difficulty <<= 4; //@@!

	#if TEST_TX_DIFFICULTY_ZERO
	g_params.tx_work_difficulty = 0;
	g_params.xcx_naked_buy_work_difficulty = 0;
	g_params.xcx_pay_work_difficulty = 0;
	#endif

	//set_storage(); // not implemented

	if (g_params.tx_validation_threads < 0)
		g_params.tx_validation_threads = thread::hardware_concurrency();

	if (g_params.tx_validation_threads < 0)
	{
		g_params.tx_validation_threads = DEFAULT_TX_VALIDATION_THREADS;

		BOOST_LOG_TRIVIAL(warning) << "std::thread::hardware_concurrency is indeterminant; using program default value " << DEFAULT_TX_VALIDATION_THREADS;
	}

	get_proof_key_dir(g_params.proof_key_dir, g_params.process_dir);

	string def = CCAPPDIR;
	expand_number(def, g_params.blockchain);
	get_app_data_dir(g_params.app_data_dir, def);
	if (!g_params.app_data_dir.length())
		throw runtime_error("No data directory specified");

	if (!g_params.base_port)
		g_params.base_port = BASE_PORT + 20 * (g_params.blockchain % ((BASE_PORT_TOP - BASE_PORT)/20));

	set_service_configs();

	if (g_params.config_options->count("show-config"))
		do_show_config();

	check_config_values();

	return 0;
}

#ifdef __MINGW64__
int _dowildcard = 0;	// disable wildcard globbing
#endif

int main(int argc, const char **argv)
{
	//srand(0);
	srand(time(NULL));

	//ccticks_test();

	cerr << endl;

	set_handlers();

	#if TEST_EXTRA_RANDOM
	#ifndef _WIN32
	CCCollectEntropy();
	#endif
	#endif

	g_params.trace_level = DEFAULT_TRACE_LEVEL;
	//g_params.trace_level = 9;
	set_trace_level(g_params.trace_level);

	g_params.process_dir = get_process_dir();
	if (!g_params.process_dir.length())
		return -1;

	set_service_pre_configs();

	try
	{
		auto rc = process_options(argc, argv);
		if (rc) return rc;
	}
	catch (const exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return -1;
	}

	if (g_params.config_options->count("dry-run"))
		return 1;

	if (g_params.config_options->count("genesis-generate"))
	{
		try
		{
			BlockChain::CreateGenesisDataFiles();
		}
		catch (...)
		{
			cerr << "Error creating genesis block data file" << endl;

			return -1;
		}

		return 1; // comment out this line for keygen
	}

	bool need_tor_proxy = true;	// always true?
	thread tor_thread(tor_start, g_params.process_dir, g_params.tor_exe, g_params.torproxy_port, g_params.tor_config, g_params.app_data_dir, need_tor_proxy, ref(g_tor_services), g_tor_services.size()-1);

	Xreq::Init();

	CCProof_Init(g_params.proof_key_dir);
	CCProof_PreloadVerifyKeys(true);

	DbInit dbinit;
	dbinit.CreateDBs();
	dbinit.InitDb();
	dbinit.CheckDb();

	//DbConnPersistData::TestConcurrency();	// for testing

	g_blockchain.Init();
	if (g_blockchain.HasFatalError())
		goto do_fatal;

	g_connregistry.Init();

	g_foreignrpc_client.Start();

	for (unsigned i = 1; i <= XREQ_BLOCKCHAIN_MAX; ++i)
	{
		if (g_foreignrpc_client.GetVerifyLevel(i) < ForeignRpcClient::VerifyLevel::Strict_Blocks)
			continue;

		cerr << "Waiting for foreign blockchain " << i << endl;

		while (!g_shutdown)
		{
			uint64_t height;

			ForeignQuery::QueryBlockHeight(i, height);

			//cerr << "height " << height << endl;

			if (height)
			{
				cerr << "Foreign blockchain " << i << " responded with block height " << height << endl;

				break;
			}

			ccsleep(8);
		}
	}

	g_processtx.Init();
	g_processblock.Init();

	g_expire.Init();
	g_blockserve_service.Start();
	g_blocksync_client.Start();
	g_relay_service.Start();
	g_privrelay_service.Start();
	g_transact_service.Start();
	g_witness.Init();

	if (0) // for testing
	{
		//string in;
		//cerr << "Press return to exit..." << endl;
		//getline(cin, in); // gdb breakpoints don't work when used?
	}

	while (!g_shutdown)
		wait_for_shutdown(500);

	BOOST_LOG_TRIVIAL(info) << "Shutting down...";

	start_shutdown();

	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Shutting down..." << endl;
	}

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 1...";

	g_processtx.Stop();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 2...";

	g_processblock.Stop();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 3...";

	g_transact_service.StartShutdown();
	g_privrelay_service.StartShutdown();
	g_relay_service.StartShutdown();
	g_blocksync_client.StartShutdown();
	g_blockserve_service.StartShutdown();
	g_foreignrpc_client.StartShutdown();
	g_hostdir.DeInit();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 4...";

	g_witness.DeInit();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 5...";

	g_transact_service.WaitForShutdown();
	g_privrelay_service.WaitForShutdown();
	g_relay_service.WaitForShutdown();
	g_blocksync_client.WaitForShutdown();
	g_blockserve_service.WaitForShutdown();
	g_foreignrpc_client.WaitForShutdown();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 6...";

	g_connregistry.DeInit();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 7...";

	g_processblock.DeInit();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 8...";

	g_processtx.DeInit();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 9...";

	g_process_xreqs.DeInit();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 10...";

	g_expire.DeInit();

do_fatal:

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 11...";

	start_shutdown();
	wait_for_shutdown();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 12...";

	g_blockchain.DeInit();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 13...";

	dbinit.DeInit();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 14...";

	dblog(sqlite3_shutdown());

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(info) << "shutdown 15...";

	tor_thread.join();

	BOOST_LOG_TRIVIAL(info) << CCEXENAME << " done";
	cerr << CCEXENAME << " done" << endl;

	finish_handlers();

	//*(int*)0 = 0;

	return 0;
}
