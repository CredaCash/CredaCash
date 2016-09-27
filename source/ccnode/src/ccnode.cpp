/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * ccnode.cpp
*/

#define DECLARE_EXTERN

#include "CCdef.h"

#include "blockchain.hpp"
#include "processblock.hpp"
#include "processtx.hpp"
#include "witness.hpp"
#include "expire.hpp"
#include "util.h"
#include "tor.h"

#include <CCproof.h>

#include <sqlite/sqlite3.h>
#include <dblog.h>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>

#define DEFAULT_TRACE_LEVEL				4
#define DEFAULT_TX_VALIDATION_THREADS	16

#define TOR_EXE			"Tor" PATH_DELIMITER "tor.exe"
#define TOR_CONFIG		"tor.conf"

#define DEFAULT_DIR_SERVERS_FILE		 "rendezvous.lis"
#define DEFAULT_GENESIS_DATA_FILE		 "genesis.dat"
#define DEFAULT_PRIVATE_RELAY_HOSTS_FILE "private_relay_hosts.lis"

static void handle_signal(int)
{
	g_shutdown = true;
}

static void handle_terminate()
{
	cerr << "handle_terminate" << endl;

#if 0
	void *array[20];

	auto size = backtrace(array, sizeof(array)/sizeof(void*));
	auto strings = backtrace_symbols(array, size);

	for (unsigned i = 0; i < size; ++i)
		cerr << strings[i] << endl;
#endif

	g_shutdown = true;

	//abort();
}

static void set_trace_level(int level)
{
    boost::log::core::get()->set_filter(boost::log::trivial::severity > (((int)(fatal)) - level));
}

#if 0 // not yet used
static void set_storage()
{
	g_store_blocks = 1;
	g_store_created = 1;
	g_store_spent = 1;

	if (g_params.config_options.count("micronode"))
	{
		g_store_blocks = 0;
		g_store_created = 0;
		g_store_spent = 0;
	}

	if (g_params.config_options.count("mininode"))
	{
		g_store_blocks = 0;
		g_store_created = 0;
		g_store_spent = 1;
	}

	if (g_params.config_options.count("store-blocks"))
		g_store_blocks = g_params.config_options.at("store-blocks").as<int>();

	if (g_params.config_options.count("store-created"))
		g_store_created = g_params.config_options.at("store-created").as<int>();

	if (g_params.config_options.count("store-spent"))
		g_store_spent = g_params.config_options.at("store-spent").as<int>();
}
#endif

#define TRANSACT_PORT		"0"
#define RELAY_PORT			"1"
#define PRIVRELAY_PORT		"2"
#define BLOCKSERVE_PORT		"3"
#define NODE_CONTROL_PORT	"4"
#define TOR_CONTROL_PORT	"5"
#define TOR_PORT			"6"

void set_service_configs()
{
	g_transact_service.SetConfig(atoi(TRANSACT_PORT), "transact");
	g_relay_service.SetConfig(atoi(RELAY_PORT), "relay");
	g_privrelay_service.SetConfig(atoi(PRIVRELAY_PORT), "privrelay");
	g_blockserve_service.SetConfig(atoi(BLOCKSERVE_PORT), "blockserve");
	g_blocksync_client.SetConfig(atoi(0), "blocksync");
	g_control_service.SetConfig(atoi(NODE_CONTROL_PORT), "control");
	g_tor_control_service.SetConfig(atoi(TOR_CONTROL_PORT), "tor-control");
	g_params.torproxy_port = g_params.base_port + atoi(TOR_PORT);
}

static void do_show_config()
{
	cout << endl;
	cout << "Configuration settings:" << endl;
	cout << "   process directory = " << w2s(g_params.process_dir) << endl;
	cout << "   data directory = " << w2s(g_params.app_data_dir) << endl;
	cout << "   path to tor exe = " << w2s(g_params.tor_exe) << endl;
	cout << "   path to tor config file = " << w2s(g_params.tor_config) << endl;
	cout << "   rendezvous servers file = " << w2s(g_params.directory_servers_file) << endl;
	cout << "   genesis block data file = " << w2s(g_params.genesis_data_file) << endl;
	//cout << "   store blocks = " << yesno(g_store_blocks) << endl;
	//cout << "   store created bills = " << yesno(g_store_created) << endl;
	//cout << "   store spent bills = " << yesno(g_store_spent) << endl;
	cout << "   base port = " << g_params.base_port << endl;
	cout << "   tx validation threads = " << g_params.tx_validation_threads << endl;
	cout << endl;

	g_transact_service.DumpConfig();
	g_relay_service.DumpConfig();
	g_privrelay_service.DumpConfig();
	g_blockserve_service.DumpConfig();
	g_blocksync_client.DumpConfig();

	if (g_witness.witness_index >= 0)
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

	//g_control_service.DumpConfig();
	g_tor_control_service.DumpConfig();

	cout << "Trace output settings:" << endl;
	cout << "   trace level = " << g_params.trace_level << endl;
	cout << "   trace transaction server = " << yesno(g_params.trace_tx_server) << endl;
	cout << "   trace relay service = " << yesno(g_params.trace_relay) << endl;
	cout << "   trace block server service = " << yesno(g_params.trace_block_serve) << endl;
	cout << "   trace block sync service = " << yesno(g_params.trace_block_sync) << endl;
	cout << "   trace host directory queries = " << yesno(g_params.trace_host_dir) << endl;
	cout << "   trace witness = " << yesno(g_params.trace_witness) << endl;
	cout << "   trace tx validation = " << yesno(g_params.trace_tx_validation) << endl;
	cout << "   trace block validation = " << yesno(g_params.trace_block_validation) << endl;
	cout << "   trace serialnum check = " << yesno(g_params.trace_serialnum_check) << endl;
	cout << "   trace commitments = " << yesno(g_params.trace_commitments) << endl;
	cout << "   trace delible tx_check = " << yesno(g_params.trace_delibletx_check) << endl;
	cout << "   trace blockchain = " << yesno(g_params.trace_blockchain) << endl;
	cout << "   trace persistent DB = " << yesno(g_params.trace_persistent_db) << endl;
	cout << "   trace WAL DB operations = " << yesno(g_params.trace_wal_db) << endl;
	cout << "   trace pending serialnum DB = " << yesno(g_params.trace_pending_serialnum_db) << endl;
	cout << "   trace relay DB = " << yesno(g_params.trace_relay_db) << endl;
	cout << "   trace validation queue DB = " << yesno(g_params.trace_validation_q_db) << endl;
	cout << "   trace valid object DB = " << yesno(g_params.trace_validobj_db) << endl;
	cout << "   trace object expiration = " << yesno(g_params.trace_expire) << endl;
	cout << endl;
}

static int check_config_values()
{
	if (g_params.genesis_nwitnesses < 1 || g_params.genesis_nwitnesses > MAX_NWITNESSES)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: invalid value for genesis nwitnesses";
		return -1;
	}

	if (g_params.genesis_maxmal < 0 || g_params.genesis_maxmal >= (g_params.genesis_nwitnesses + 1) / 2)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: invalid value for genesis malicious witness allowance";
		return -1;
	}

	if (g_params.tx_validation_threads < 1 || g_params.tx_validation_threads > 2000)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: tx validation threads value not in valid range";
		return -1;
	}

	if (g_params.base_port < 1 || g_params.base_port > 0xFFFF - atoi(TOR_PORT))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: baseport value not in valid range";
		return -1;
	}

	if (g_transact_service.max_inconns < 0 || g_transact_service.max_inconns > 100000)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: max connections for transaction support service not in valid range";
		return -1;
	}

	if (g_transact_service.threads_per_conn <= 0 || g_transact_service.threads_per_conn > 2)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: max connections for transaction support service not in valid range";
		return -1;
	}

	#if 1	// this can be disabled for testing

	if (g_relay_service.max_outconns < 4)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: number of outgoing relay connections must be at least 4";
		return -1;
	}

	if (g_relay_service.max_inconns * 2 < 3 * g_relay_service.max_outconns)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: maximum number of incoming relay connections must be at least 1.5 * the number of outgoing relay connections";
		return -1;
	}

	#endif

	return 0;
}

static int process_options(int argc, char **argv)
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

	if (atoi(TRANSACT_PORT) != 0)
		CCASSERT("expected TRANSACT_PORT == 0");

	po::options_description advanced_options("ADVANCED OPTIONS", 99, 0);
	advanced_options.add_options()
		("config", po::wvalue<wstring>(), "Path to file with additional configuration options.")
		("datadir", po::wvalue<wstring>(), "Path to program data directory (default: \""
#if _WIN32
				"LOCAL_APPDATA\\CredaCash\\CCNode\").")
#else
				"~/.CCNode\").")
#endif
		("tor-exe", po::wvalue<wstring>(&g_params.tor_exe), "Path to Tor executable (default: \"" TOR_EXE "\" in same directory as this program).")
		("tor-config", po::wvalue<wstring>(&g_params.tor_config), "Path to Tor configuration file (default: \"" TOR_CONFIG "\" in same directory as this program).")
		("rendezvous-file", po::wvalue<wstring>(&g_params.directory_servers_file), "Path to file containing a list of peer rendezvous servers (default: \"" DEFAULT_DIR_SERVERS_FILE "\" in same directory as this program).")
		("genesis-file", po::wvalue<wstring>(&g_params.genesis_data_file), "Path to file containing data for the genesis block (default \"" DEFAULT_GENESIS_DATA_FILE "\").")
		("genesis-generate", "Generate new genesis block data files.")
		("genesis-nwitnesses", po::value<int>(&g_params.genesis_nwitnesses)->default_value(3), "Initial # of witnesses generating new genesis block data files.")
		("genesis-maxmal", po::value<int>(&g_params.genesis_maxmal)->default_value(0), "Initial allowance for malicious witnesses when generating new genesis block data files.")
		("tx-validation-threads", po::value<int>(&g_params.tx_validation_threads)->default_value(-1), "Transaction validation threads (-1 = auto config).")
		("baseport", po::value<int>(&g_params.base_port)->default_value(9223), "Base port for node interfaces\n"
			"(node software uses ports baseport through baseport+" TOR_PORT ").")
		//("store-blocks", po::value<int>(), "Store entire blockchain;\ndefaults to 0 if micronode=1 or mininode=1.")
		//("store-created", po::value<int>(), "Store database of created bills;\ndefaults to 0 if micronode=1 or mininode=1.")
		//("store-spent", po::value<int>(), "Store database of spent bills\ndefaults to 0 if micronode=1 and mininode=0; if this is set to zero, transactions can only be partially validated by node.")
		("transact", po::value<bool>(&g_transact_service.enabled)->default_value(1), "Answer transaction queries from wallet applications (at port baseport).")
		("transact-addr", po::value<string>(&g_transact_service.address_string)->default_value(LOCALHOST), "Network address for transaction support service;\n"
				"by default, this service is available from the localhost only;\n"
				"this setting can be used to bind to another address for access via the local network or internet.")
		//("transact-password", po::value<string>(&g_transact_service.password_string), "SHA1 hash of password to access transaction support service (default: no password required).")
		("transact-tor", po::value<bool>(&g_transact_service.tor_service)->default_value(0), "Make the transaction support service available as a Tor hidden service.")
		("transact-tor-auth", po::value<string>(&g_transact_service.tor_auth_string)->default_value("basic"), "Tor hidden service authentication method (none, basic or stealth).")
		("transact-conns", po::value<int>(&g_transact_service.max_inconns)->default_value(20), "Maximum number of incoming connections for transaction support service.")
		("transact-threads", po::value<float>(&g_transact_service.threads_per_conn)->default_value(1), "Threads per connection for transaction support service.")	// !!! change this?
		("relay", po::value<bool>(&g_relay_service.enabled)->default_value(1), "Fetch and relay blocks and transactions (at port baseport+" RELAY_PORT ");\n"
				"if no relay is enabled, this node will receive no updates and will only use data previously stored.")
		("relay-addr", po::value<string>(&g_relay_service.address_string)->default_value(LOCALHOST), "Network address for relay service;\n"
				"by default, this service binds to localhost and is available as a Tor hidden service;\n"
				"this setting can be used to bind to another address for direct access via the local network or internet.")
		("relay-out", po::value<int>(&g_relay_service.max_outconns)->default_value(8), "Target number of outgoing relay connections (must be at least 4).")
		("relay-in", po::value<int>(&g_relay_service.max_inconns)->default_value(16), "Maximum number of incoming relay connections (must be at least 1.5 * relay-out).")
		("privrelay", po::value<bool>(&g_privrelay_service.enabled)->default_value(0), "Fetch and relay blocks and transactions (at port baseport+" PRIVRELAY_PORT ");\n"
				"if no relay is enabled, this node will receive no updates and will only use data previously stored.")
		("privrelay-file", po::wvalue<wstring>(&g_privrelay_service.priv_hosts_file), "Path to file containing a list of private relay hostnames (default: \"" DEFAULT_PRIVATE_RELAY_HOSTS_FILE "\").")
		("privrelay-host-index", po::value<int>(&g_privrelay_service.priv_host_index)->default_value(-1), "Index (zero-based) of this host in the hosts file (-1 = not applicable).")
		("privrelay-addr", po::value<string>(&g_privrelay_service.address_string)->default_value(LOCALHOST), "Network address for private relay service;\n"
				"by default, this service binds to localhost and is available as a Tor hidden service;\n"
				"this setting can be used to bind to another address for direct access via the local network or internet.")
		("privrelay-tor", po::value<bool>(&g_privrelay_service.tor_service)->default_value(1), "Make the private relay service available as a Tor hidden service.")
		("privrelay-tor-new-hostname", po::value<bool>(&g_privrelay_service.tor_new_hostname)->default_value(0), "Give the private relay Tor hidden service a new hostname.")
		("privrelay-tor-auth", po::value<string>(&g_privrelay_service.tor_auth_string)->default_value("none"), "Tor hidden service authentication method (none, basic or stealth).")
		("privrelay-in", po::value<int>(&g_privrelay_service.max_inconns)->default_value(-1), "Maximum number of incoming private relay connections  (-1 = auto config).")
		("blockserve", po::value<bool>(&g_blockserve_service.enabled)->default_value(1), "Provide blockchain data to other nodes (at port baseport+" BLOCKSERVE_PORT ");\n"
				"note: this option is disabled when store-blocks=0.")
		("blockserve-addr", po::value<string>(&g_blockserve_service.address_string)->default_value(LOCALHOST), "Network address for blockchain service;\n"
				"by default, this service binds to localhost and is available as a Tor hidden service;\n"
				"this setting can be used to bind to another address for direct access via the local network or internet.")
		//("blockserve-password", po::value<string>(&g_blockserve_service.password_string), "SHA1 hash of password to access blockchain service (default: no password required).")
		("blockserve-tor", po::value<bool>(&g_blockserve_service.tor_service)->default_value(1), "Make the blockchain service available as a Tor hidden service.")
		("blockserve-tor-new-hostname", po::value<bool>(&g_blockserve_service.tor_new_hostname)->default_value(1), "Give the blockchain Tor hidden service a new hostname.")
		("blockserve-tor-auth", po::value<string>(&g_blockserve_service.tor_auth_string)->default_value("none"), "Tor hidden service authentication method (none, basic or stealth).")
		("blockserve-conns", po::value<int>(&g_blockserve_service.max_inconns)->default_value(1), "Maximum number of incoming connections for blockchain service.")
		("blocksync-conns", po::value<int>(&g_blocksync_client.max_outconns)->default_value(10), "Maximum number of outgoing connections for blockchain synchonization.")
		("witness-index", po::value<int>(&g_witness.witness_index)->default_value(-1), "Witness index (-1 = disable).")
		("witness-block-ms", po::value<int>(&g_witness.block_time_ms)->default_value(2000), "Nominal milliseconds between blocks.")
		("witness-block-min-work-ms", po::value<int>(&g_witness.block_min_work_ms)->default_value(200), "Minimum milliseconds to work assembling a block.")
		("witness-block-idle-sec", po::value<int>(&g_witness.block_max_time)->default_value(20), "Seconds between blocks when there are no transactions to witness.")
		("witness-test-block-random-ms", po::value<int>(&g_witness.test_block_random_ms)->default_value(-1), "Test randomly generating blocks, with this average milliseconds between blocks (-1 = diabled).")
		("witness-test-mal", po::value<bool>(&g_witness.test_mal)->default_value(0), "Act as a malicious witness.")
		//("control", po::value<bool>(&g_control_service.enabled)->default_value(0), "Allow other programs to control node operation (at port baseport+" NODE_CONTROL_PORT ").")
		//("control-addr", po::value<string>(&g_control_service.address_string)->default_value(LOCALHOST), "Network address for node control service;\n"
		//		"by default, this service is available from the localhost only;\n"
		//		"this setting can be used to bind to another address for access via the local network or internet.")
		//("control-password", po::value<string>(&g_control_service.password_string), "SHA1 hash of password to access node control service (default: no password required).")
		//("control-tor", po::value<bool>(&g_control_service.tor_service)->default_value(0), "Make the node control port available through a Tor hidden service.")
		//("control-tor-auth", po::value<string>(&g_control_service.tor_auth_string)->default_value("basic"), "Tor hidden service authentication method (none, basic or stealth).")
		("tor-control", po::value<bool>(&g_tor_control_service.enabled)->default_value(0), "Allow other programs to control Tor (at port baseport+" TOR_CONTROL_PORT ").")
		("tor-control-addr", po::value<string>(&g_tor_control_service.address_string)->default_value(LOCALHOST), "Network address for node control service;\n"
				"by default, this service is available from the localhost only;\n"
				"this setting can be used to bind to another address for access via the local network or internet.")
		("tor-control-password", po::value<string>(&g_tor_control_service.password_string), "SHA1 hash of password to access Tor control service (default: no password required).")
		("tor-control-tor", po::value<bool>(&g_tor_control_service.tor_service)->default_value(0), "Make the Tor control port available through a Tor hidden service.")
		("tor-control-tor-auth", po::value<string>(&g_tor_control_service.tor_auth_string)->default_value("basic"), "Tor hidden service authentication method (none, basic or stealth).")
		("trace", po::value<int>(&g_params.trace_level)->default_value(DEFAULT_TRACE_LEVEL), "Trace level; affects all trace settings (0=none, 1=fatal, 2=errors, 3=warnings, 4=info, 5=debug, 6=trace)")
		("trace-tx-server", po::value<bool>(&g_params.trace_tx_server)->default_value(1), "Trace transaction server")
		("trace-relay", po::value<bool>(&g_params.trace_relay)->default_value(1), "Trace relay service")
		("trace-block-serve", po::value<bool>(&g_params.trace_block_serve)->default_value(1), "Trace block server service")
		("trace-block-sync", po::value<bool>(&g_params.trace_block_sync)->default_value(1), "Trace block sync service")
		("trace-host-dir", po::value<bool>(&g_params.trace_host_dir)->default_value(1), "Trace host directory queries")
		("trace_witness", po::value<bool>(&g_params.trace_witness)->default_value(1), "Trace witness")
		("trace-tx-validation", po::value<bool>(&g_params.trace_tx_validation)->default_value(1), "Trace tx validation")
		("trace-block-validation", po::value<bool>(&g_params.trace_block_validation)->default_value(1), "Trace block validation")
		("trace-serialnum-check", po::value<bool>(&g_params.trace_serialnum_check)->default_value(1), "Trace serial number check")
		("trace-commitments", po::value<bool>(&g_params.trace_commitments)->default_value(0), "Trace commitments")
		("trace-delibletx-check", po::value<bool>(&g_params.trace_delibletx_check)->default_value(0), "Trace delible tx check")
		("trace-blockchain", po::value<bool>(&g_params.trace_blockchain)->default_value(1), "Trace blockchain")
		("trace-persistent-db", po::value<bool>(&g_params.trace_persistent_db)->default_value(0), "Trace persistent DB")
		("trace-wal-db", po::value<bool>(&g_params.trace_wal_db)->default_value(0), "Trace write-ahead-log DB operations")
		("trace-pending-serialnum-db", po::value<bool>(&g_params.trace_pending_serialnum_db)->default_value(0), "Trace pending serial number DB")
		("trace-relay-db", po::value<bool>(&g_params.trace_relay_db)->default_value(0), "Trace relay DB")
		("trace-validation-q-db", po::value<bool>(&g_params.trace_validation_q_db)->default_value(0), "Trace validation queue DB")
		("trace-valid-obj-db", po::value<bool>(&g_params.trace_validobj_db)->default_value(0), "Trace valid object DB")
		("trace-expire", po::value<bool>(&g_params.trace_expire)->default_value(0), "Trace object expiration")
	;

	po::options_description all;
	all.add(basic_options).add(advanced_options);

	po::store(po::parse_command_line(argc, argv, all), g_params.config_options);

	set_trace_level(debug);

	if (g_params.config_options.count("help"))
	{
		cout << "CredaCash node v" CCVERSION << endl << endl;
		cout << basic_options << endl;
		cout << advanced_options << endl;
		return 1;
	}

	if (g_params.config_options.count("config"))
	{
		boost::filesystem::ifstream fs;
		auto fname = g_params.config_options.at("config").as<wstring>();
		fs.open(fname, fstream::in);
		if(!fs.is_open())
		{
			BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to open config file \"" << fname << "\"";
			exit(-1);
			throw exception();
			return -1;
		}

		po::store(po::parse_config_file(fs, all), g_params.config_options);

		set_trace_level(g_params.trace_level);
	}

	po::notify(g_params.config_options);

	set_trace_level(g_params.trace_level);

	//for (auto v : g_params.config_options)
	//	cout << "config option: " << v.first << endl;

	// !!! move these to a function

	if (!g_params.directory_servers_file.length())
		g_params.directory_servers_file = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(DEFAULT_DIR_SERVERS_FILE);

	if (!g_params.genesis_data_file.length())
		g_params.genesis_data_file = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(DEFAULT_GENESIS_DATA_FILE);

	if (!g_privrelay_service.priv_hosts_file.length())
		g_privrelay_service.priv_hosts_file = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(DEFAULT_PRIVATE_RELAY_HOSTS_FILE);

	if (g_privrelay_service.priv_host_index == -1)
		g_privrelay_service.priv_host_index = g_witness.witness_index;

	if (init_globals())
		return -1;

	if (!g_params.tor_exe.length())
		g_params.tor_exe = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(TOR_EXE);

	if (!g_params.tor_config.length())
		g_params.tor_config = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(TOR_CONFIG);

	g_params.query_work_difficulty = (uint64_t)1 << 44;
	g_params.tx_work_difficulty = (uint64_t)1 << 41;

	//set_storage(); // not implemented

	if (g_params.tx_validation_threads < 0)
		g_params.tx_validation_threads = thread::hardware_concurrency();

	if (g_params.tx_validation_threads < 0)
	{
		g_params.tx_validation_threads = DEFAULT_TX_VALIDATION_THREADS;

		BOOST_LOG_TRIVIAL(warning) << "std::thread::hardware_concurrency is indeterminant; using program default value " << DEFAULT_TX_VALIDATION_THREADS;
	}

	set_service_configs();

	if (g_params.config_options.count("show-config"))
		do_show_config();

	if (check_config_values())
		return -1;

	return 0;
}

int main(int argc, char **argv)
{
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	#if defined(SIGQUIT)
	signal(SIGQUIT, handle_signal);
	#endif

	//set_terminate(handle_terminate);
	(void)handle_terminate;

	//srand(0);
	srand(time(NULL));

	g_params.trace_level = DEFAULT_TRACE_LEVEL;
	set_trace_level(g_params.trace_level);

	if (init_app_dir())
		return -1;

	try
	{
		if (process_options(argc, argv))
			return -1;
	}
	catch (const exception& e)
	{
		cerr << "FATAL ERROR EXCEPTION: " << e.what() << endl;

		return -1;
	}

	if (g_params.config_options.count("dry-run"))
		return 1;

	if (g_params.config_options.count("genesis-generate"))
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

		return 1;
	}

	thread tor_thread(tor_start);

	CCProof_Init();
	CCProof_PreloadVerifyKeys();

	DbInit dbinit;
	dbinit.CreateDBs();

	//DbConnPersistData::TestConcurrency();	// for testing

	g_blockchain.Init();
	if (g_blockchain.HasFatalError())
		goto do_fatal;

	g_processtx.Init();
	g_processblock.Init();

	g_expire.Init();
	g_blockserve_service.Start();
	g_blocksync_client.Start();
	g_relay_service.Start();
	g_privrelay_service.Start();
	g_transact_service.Start();
	g_witness.Init();

#if 0 // !!! must be 0 for gdb breakpoints to work
	string in;
	cout << "Press return to exit..." << endl;
	getline(cin, in);

	cout << "Sending SIGTERM..." << endl;
	g_shutdown = true;
	raise(SIGTERM);
#endif

	while (!g_shutdown)
		sleep(1);

	cerr << "Shutting down..." << endl;
	BOOST_LOG_TRIVIAL(info) << "Shutting down...";

	g_witness.DeInit();
	g_transact_service.WaitForShutdown();
	g_privrelay_service.WaitForShutdown();
	g_relay_service.WaitForShutdown();
	g_blocksync_client.WaitForShutdown();
	g_blockserve_service.WaitForShutdown();
	g_processblock.DeInit();

	g_processtx.DeInit();
	g_expire.DeInit();

do_fatal:

	g_shutdown = true;
	g_blockchain.DeInit();

	dbinit.DeInit();

	dblog(sqlite3_shutdown());

	tor_thread.join();

	BOOST_LOG_TRIVIAL(info) << "ccnode done";
	cerr << "ccnode done" << endl;

	//*(int*)0 = 0;

	return 0;
}
