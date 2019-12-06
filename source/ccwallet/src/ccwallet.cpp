/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * ccwallet.cpp
*/

#define DECLARE_EXTERN

#include "ccwallet.h"
#include "interactive.h"
#include "btc_block.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "txbuildlist.hpp"
#include "polling.hpp"
#include "txrpc.h"
#include "walletdb.hpp"

#include <CCproof.h>
#include <transaction.hpp>
#include <encode.h>

#include <tor.h>

#include <sqlite/sqlite3.h>
#include <dblog.h>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/errors.hpp>

#define TRANSACT_HOSTS		"transact_tor_hosts-#.lis"

#define DEFAULT_TRACE_LEVEL	3
#define TRACE_SHUTDOWN		1

void set_service_configs()
{
	g_tor_services.push_back(&g_rpc_service);
	g_tor_services.push_back(&g_tor_control_service);

	TorService::SetPorts(g_tor_services, g_params.base_port + WALLET_RPC_PORT);

	g_params.torproxy_port = g_tor_services[g_tor_services.size()-1]->port + 1;

	//cerr << "torproxy_port " << g_params.torproxy_port << endl;

	for (unsigned i = 0; i < g_tor_services.size(); ++i)
		g_tor_services[i]->SetConfig();

	g_lpc_service.SetConfig();
}

static void do_show_config()
{
	cout << endl;
	cout << "Configuration settings:" << endl;
	cout << "   process directory = " << w2s(g_params.process_dir) << endl;
	cout << "   data directory = " << w2s(g_params.app_data_dir) << endl;
	cout << "   proof key directory = " << w2s(g_params.proof_key_dir) << endl;
	cout << "   wallet file = " << g_params.wallet_file << endl;
	cout << "   path to Tor exe = " << w2s(g_params.tor_exe) << endl;
	cout << "   path to Tor config file = " << w2s(g_params.tor_config) << endl;
	cout << "   base port = " << g_params.base_port << endl;
	cout << endl;
	if (!g_params.transact_tor)
	{
		cout << "   transaction server ip address = " << g_params.transact_host << endl;
		cout << "   transaction server port = " << g_params.transact_port << endl;
	}
	cout << "   access transaction server via Tor = " << g_params.transact_tor << endl;
	if (g_params.transact_tor)
	{
		cout << "   new Tor circuit for each query = " << yesno(g_params.transact_tor_single_query) << endl;
		cout << "   path to file of transaction server hostnames = " << w2s(g_params.transact_tor_hosts_file) << endl;
	}

	cout << "   transaction query retries = " << g_params.tx_query_retries << endl;
	cout << "   transaction submit retries = " << g_params.tx_submit_retries << endl;
	cout << "   new billet wait seconds = " << g_params.billet_wait_time << endl;
	cout << "   transaction create timeout seconds = " << g_params.tx_create_timeout << endl;
	cout << "   max asynchronous transactions = " << g_params.tx_threads_max << endl;
	cout << "   cleared confirmations = " << g_params.cleared_confirmations << endl;
	cout << "   polled addresses per destination = " << g_params.polling_addresses << endl;
	cout << "   polling threads = " << g_params.polling_threads << endl;
	cout << endl;

	for (unsigned i = 0; i < g_tor_services.size(); ++i)
		g_tor_services[i]->DumpConfig();

	cout << "Trace output settings:" << endl;
	cout << "   trace level = " << g_params.trace_level << endl;
	cout << "   trace LPC service = " << yesno(g_params.trace_lpcserve) << endl;
	cout << "   trace RPC service = " << yesno(g_params.trace_rpcserve) << endl;
	cout << "   trace JSON RPC calls = " << yesno(g_params.trace_jsonrpc) << endl;
	cout << "   trace transaction methods = " << yesno(g_params.trace_txrpc) << endl;
	cout << "   trace transactions = " << yesno(g_params.trace_transactions) << endl;
	cout << "   trace billets = " << yesno(g_params.trace_billets) << endl;
	cout << "   trace totals = " << yesno(g_params.trace_totals) << endl;
	cout << "   trace secrets = " << yesno(g_params.trace_secrets) << endl;
	cout << "   trace accounts = " << yesno(g_params.trace_accounts) << endl;
	cout << "   trace polling = " << yesno(g_params.trace_polling) << endl;
	cout << "   trace transaction server queries = " << yesno(g_params.trace_txquery) << endl;
	cout << "   trace transaction server parameter fetches = " << yesno(g_params.trace_txparams) << endl;
	cout << "   trace transaction server commnication = " << yesno(g_params.trace_txconn) << endl;
	cout << "   trace database calls = " << yesno(g_params.trace_db) << endl;

	cout << endl;
}

static void check_config_values()
{
	if (g_params.blockchain < 1 || (g_params.blockchain > (TX_CHAIN_BITS < 64 ? ((uint64_t)1 << TX_CHAIN_BITS) - 1 : (uint64_t)(-1))))
		throw range_error("Invalid value for blockchain");

	if (g_params.secret_gen_time < 1 || g_params.secret_gen_time > 16383)					// matches generate_master_secret()
		throw range_error("New secret generation time not in valid range");

	if (g_params.secret_gen_memory < 0 || g_params.secret_gen_memory > 1000000)				// matches generate_master_secret()
		throw range_error("New secret generation memory not in valid range");

	if (g_params.tx_query_retries < 0)
		throw range_error("Transaction query retries not in valid range");

	if (g_params.tx_submit_retries < 0)
		throw range_error("Transaction submit retries not in valid range");

	if (g_params.billet_wait_time < 0)
		throw range_error("New billet wait time not in valid range");

	if (g_params.tx_create_timeout < 0)
		throw range_error("Transaction create timeout not in valid range");

	if (g_params.tx_threads_max < 0)
		throw range_error("Maximum asynchronous transaction threads is not in valid range");

	if (g_params.cleared_confirmations < 1 || g_params.cleared_confirmations > 2000)
		throw range_error("tx-cleared-confirmations value not in valid range");

	if (g_params.polling_addresses < 1 || g_params.polling_addresses > 500)
		throw range_error("Number of transaction polling addresses not in valid range");

	if (g_params.polling_threads < 0 || g_params.polling_threads > 1000)
		throw range_error("Number of transaction polling threads not in valid range");

	if (!g_params.polling_threads)
	{
		cerr <<

R"(WARNING: The wallet is configured with tx-polling-threads = 0 and therefore will not detect when a transaction clears unless
the gettransaction command is used, or the transaction send destination and change destination are both manually polled.)"

		"\n" << endl;
	}

	if (g_params.base_port < 1 || g_params.base_port > 0xFFFF - TOR_PORT)
		throw range_error("baseport value not in valid range");

	if (g_rpc_service.max_inconns < 0 || g_rpc_service.max_inconns > 100000)
		throw range_error("Max connections for transaction support service not in valid range");
}

static int process_options(int argc, char **argv)
{
	namespace po = boost::program_options;

	po::options_description basic_options("BASIC OPTIONS");
	basic_options.add_options()
		("help", "Display this message.")
		("show-config", "Show configuration information.")
		("dry-run", "Exit after parsing configuration.")
		("create-wallet", "Create wallet data file if it does not exist.")
		("reset-wallet", "Free all allocated billets.")
		("update-wallet", "Update wallet for this version of the software.")
		//("create-master-secret", "Create master secret if it does not exist.")	// create-wallet does this
		("interactive", "Run the wallet in interactive mode after executing the command line.")
		;

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
		("wallet-file", po::value<string>(&g_params.wallet_file)->default_value("CCWallet"), "Wallet filename.")
		("secret-generation-ms", po::value<int>(&g_params.secret_gen_time)->default_value(4000), "Number of millseconds to expend generating a new secret.")
		("secret-generation-memory", po::value<int>(&g_params.secret_gen_memory)->default_value(10), "Memory (MB) used to generate a new secret.")
		("proof-key-dir", po::wvalue<wstring>(&g_params.proof_key_dir), "Path to zero knowledge proof keys; if set to \"env\", the environment variable " KEY_PATH_ENV_VAR " is used (default: the subdirectory \"" ZK_KEY_DIR "\" in same directory as this program).")
		("baseport", po::value<int>(&g_params.base_port)->default_value(0), (string("Base port for wallet interfaces\n")
			+ "(default: " STRINGIFY(BASE_PORT) "+20*(blockchain modulo " + to_string((BASE_PORT_TOP-BASE_PORT)/20) + ")"
			"; wallet interfaces use ports baseport+" STRINGIFY(WALLET_RPC_PORT) " through baseport+" STRINGIFY(TOR_PORT) ").").c_str())

		("transact-host", po::value<string>(&g_params.transact_host)->default_value(LOCALHOST), "IP address of transaction support server.\n"
			"If transact-tor=1, then this value is ignored and transact-tor-hosts-file is used instead.")
		("transact-port", po::value<int>(&g_params.transact_port)->default_value(0), "Port of transaction support server.\n"
			"If transact-tor=1, then this value is ignored and transact-tor-hosts-file is used instead"
			" (default: baseport).")
		("transact-tor", po::value<bool>(&g_params.transact_tor)->default_value(false), "Connect to transaction support server via Tor.")
		("transact-tor-single-query", po::value<bool>(&g_params.transact_tor_single_query)->default_value(false), "Create a new Tor circuit for each transaction server query (slower but more private).")
		("transact-tor-hosts-file", po::wvalue<wstring>(&g_params.transact_tor_hosts_file), "Path to file with transaction server Tor hostnames; a \"#\" character in this path will be replaced by the blockchain number (default: \"" TRANSACT_HOSTS "\" in same directory as this program).")

		("tx-query-retries", po::value<int>(&g_params.tx_query_retries)->default_value(2), "Number of times to retry a query to the transaction server before aborting.")
		("tx-submit-retries", po::value<int>(&g_params.tx_submit_retries)->default_value(4), "Number of times to retry submitting a transaction to the network before aborting.")

		("tx-new-billet-wait-sec", po::value<int>(&g_params.billet_wait_time)->default_value(300), "Maximum seconds to wait for an expected incoming billet when required to complete a transaction.")
		("tx-create-timeout", po::value<int>(&g_params.tx_create_timeout)->default_value(86400), "Maximum seconds allowed to create and submit a transaction (0 = unlimited).")
		("tx-async-max", po::value<int>(&g_params.tx_threads_max)->default_value(20), "Maximum number of asynchronous transactions.")
		("tx-cleared-confirmations", po::value<int>(&g_params.cleared_confirmations)->default_value(6), "Number of emulated confirmations for a cleared transaction.")

		("tx-polling-addresses", po::value<int>(&g_params.polling_addresses)->default_value(6), "Number of addresses to poll per receive destination.")
		("tx-polling-threads", po::value<int>(&g_params.polling_threads)->default_value(10), "Transaction polling threads.")

		("wallet-rpc", po::value<bool>(&g_rpc_service.enabled)->default_value(0), "Provide wallet RPC service.")
		#if 0 // for security, only allow RPC services on localhost
		("wallet-rpc-addr", po::value<string>(&g_rpc_service.address_string)->default_value(LOCALHOST), "Network address for wallet RPC;\n"
				"by default, this service is available from the localhost only;\n"
				"this setting can be used to bind to another address for access via the local network or internet.")
		#endif
		("wallet-rpc-user", po::value<string>(&g_rpc_service.user_string)->default_value("rpc"), "Arbitrary string used to restrict access to wallet RPC.")
		("wallet-rpc-password", po::value<string>(&g_rpc_service.pass_string), "Arbitrary password used to restrict access to wallet RPC (default: generate and write password to .cookie file).")
		("wallet-rpc-tor", po::value<bool>(&g_rpc_service.tor_service)->default_value(0), "Make the transaction support service available as a Tor hidden service.")
		("wallet-rpc-tor-auth", po::value<string>(&g_rpc_service.tor_auth_string)->default_value("v3"), "Tor hidden service authentication method (none, basic, or v3).")
		("wallet-rpc-conns", po::value<int>(&g_rpc_service.max_inconns)->default_value(20), "Maximum number of incoming connections for transaction support service.")
		("wallet-rpc-threads", po::value<float>(&g_rpc_service.threads_per_conn)->default_value(1), "Threads per connection for transaction support service.")

		//("control", po::value<bool>(&g_control_service.enabled)->default_value(0), "Allow other programs to control node operation (at port baseport+" STRINGIFY(NODE_CONTROL_PORT) ").")
		//("control-addr", po::value<string>(&g_control_service.address_string)->default_value(LOCALHOST), "Network address for node control service;\n"
		//		"by default, this service is available from the localhost only;\n"
		//		"this setting can be used to bind to another address for access via the local network or internet.")
		//("control-password", po::value<string>(&g_control_service.password_string), "SHA1 hash of password to access node control service (default: no password required).")
		//("control-tor", po::value<bool>(&g_control_service.tor_service)->default_value(0), "Make the node control port available through a Tor hidden service.")
		//("control-tor-auth", po::value<string>(&g_control_service.tor_auth_string)->default_value("v3"), "Tor hidden service authentication method (none, basic, or v3).")
		("tor-exe", po::wvalue<wstring>(&g_params.tor_exe), "Path to Tor executable; if set to \"external\", Tor is not launched by this program, and must be launched and managed externally (default: \"" TOR_EXE "\" in same directory as this program).")
		("tor-config", po::wvalue<wstring>(&g_params.tor_config), "Path to Tor configuration file (default: \"" TOR_CONFIG "\" in same directory as this program).")
		("tor-control", po::value<bool>(&g_tor_control_service.enabled)->default_value(0), "Allow other programs to control Tor (at port baseport+" STRINGIFY(TOR_CONTROL_PORT) ").")
		("tor-control-addr", po::value<string>(&g_tor_control_service.address_string)->default_value(LOCALHOST), "Network address for node control service;\n"
				"by default, this service is available from the localhost only;\n"
				"this setting can be used to bind to another address for access via the local network or internet.")
		("tor-control-password", po::value<string>(&g_tor_control_service.password_string), "SHA1 hash of password to access Tor control service (default: no password required).")
		("tor-control-tor", po::value<bool>(&g_tor_control_service.tor_service)->default_value(0), "Make the Tor control port available through a Tor hidden service.")
		("tor-control-tor-auth", po::value<string>(&g_tor_control_service.tor_auth_string)->default_value("v3"), "Tor hidden service authentication method (none, basic, or v3).")
		("trace", po::value<int>(&g_params.trace_level)->default_value(DEFAULT_TRACE_LEVEL), "Trace level; affects all trace settings (0=none, 1=fatal, 2=errors, 3=warnings, 4=info, 5=debug, 6=trace)")
		("trace-lpcserve", po::value<bool>(&g_params.trace_lpcserve)->default_value(0), "Trace LPC service")
		("trace-rpcserve", po::value<bool>(&g_params.trace_rpcserve)->default_value(0), "Trace RPC service")
		("trace-jsonrpc", po::value<bool>(&g_params.trace_jsonrpc)->default_value(0), "Trace JSON RPC calls")
		("trace-txrpc", po::value<bool>(&g_params.trace_txrpc)->default_value(0), "Trace transaction RPC methods")
		("trace-transactions", po::value<bool>(&g_params.trace_transactions)->default_value(0), "Trace transactions")
		("trace-billets", po::value<bool>(&g_params.trace_billets)->default_value(0), "Trace billets")
		("trace-totals", po::value<bool>(&g_params.trace_totals)->default_value(0), "Trace totals")
		("trace-secrets", po::value<bool>(&g_params.trace_secrets)->default_value(0), "Trace secrets")
		("trace-accounts", po::value<bool>(&g_params.trace_accounts)->default_value(0), "Trace accounts")
		("trace-polling", po::value<bool>(&g_params.trace_polling)->default_value(0), "Trace polling")
		("trace-txquery", po::value<bool>(&g_params.trace_txquery)->default_value(0), "Trace transaction server queries")
		("trace-txparams", po::value<bool>(&g_params.trace_txparams)->default_value(0), "Trace transaction server parameter fetches")
		("trace-txconn", po::value<bool>(&g_params.trace_txconn)->default_value(0), "Trace communication with transaction server")
		("trace-db", po::value<bool>(&g_params.trace_db)->default_value(0), "Trace database calls")
	;

	po::options_description hidden_options("");
	hidden_options.add_options()
		("command", po::value< vector<string> >())
		("rpcuser", po::value<string>(&g_rpc_service.user_string))
		("rpcpassword", po::value<string>(&g_rpc_service.pass_string))
		("initial-master-secret", po::value<string>(&g_params.initial_master_secret))
		("initial-master-secret-passphrase", po::value<string>(&g_params.initial_master_secret_passphrase))
		("foundation-wallet", po::value<bool>(&g_params.foundation_wallet)->default_value(0))
	;

	po::positional_options_description positional_options;
	positional_options.add("command", -1);

	po::options_description all;
	all.add(basic_options).add(advanced_options).add(hidden_options);

	po::store(po::command_line_parser(argc, argv).options(all).positional(positional_options).run(), g_params.config_options);

	if (g_params.config_options.count("help"))
	{
		cout << CCAPPNAME " v" CCVERSION << endl;
		cout << "\nUsage: " << argv[0] << " [command] [params...]" << endl;
		cout << "   or: " << argv[0] << " [command] [params...] --interactive > logfile\n" << endl;
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
			throw runtime_error(string("Unable to open config file \"") + w2s(fname) + "\"");

		po::store(po::parse_config_file(fs, all), g_params.config_options);

		set_trace_level(g_params.trace_level);
	}

	po::notify(g_params.config_options);

	set_trace_level(g_params.trace_level);

	#if 0
	BOOST_LOG_TRIVIAL(fatal) << "fatal";
	BOOST_LOG_TRIVIAL(error) << "error";
	BOOST_LOG_TRIVIAL(warning) << "warning";
	BOOST_LOG_TRIVIAL(debug) << "debug";
	BOOST_LOG_TRIVIAL(trace) << "trace";
	#endif

	//for (auto v : g_params.config_options)
	//	cout << "config option: " << v.first << endl;

	// TODO: move these to a function

	if (!g_params.tor_exe.length())
		g_params.tor_exe = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(TOR_EXE);

	if (!g_params.tor_config.length())
		g_params.tor_config = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(TOR_CONFIG);

	if (!g_params.transact_tor_hosts_file.length())
	{
		string def = TRANSACT_HOSTS;
		expand_number(def, g_params.blockchain);
		g_params.transact_tor_hosts_file = g_params.process_dir + WIDE(PATH_DELIMITER) + s2w(def);
	}
	else
		expand_number_wide(g_params.transact_tor_hosts_file, g_params.blockchain);

	g_lpc_service.max_outconns = 1 + g_params.polling_threads + g_params.tx_threads_max;

	// polling_table[secret type][last_receive>0][list elements][period/endtime]
		//#define SECRET_TYPE_SEND_ADDRESS			13	// + paynum if known
		//#define SECRET_TYPE_SELF_ADDRESS			14	// + paynum if known
		//#define SECRET_TYPE_RECV_ADDRESS			15	// + paynum if known
		//#define SECRET_TYPE_POLL_ADDRESS			16	// + paynum if known
		//#define SECRET_TYPE_STATIC_ADDRESS		17	// + paynum if known

	// SECRET_TYPE_SEND_ADDRESS nothing received
	unsigned a = 0, r = 0;
	g_params.polling_table[a][r].push_back(make_pair(	10,				2 * 60));
	g_params.polling_table[a][r].push_back(make_pair(	30,				20 * 60));
	g_params.polling_table[a][r].push_back(make_pair(30 * 60,	2 * 24*60*60));

	// SECRET_TYPE_SELF_ADDRESS nothing received
	a++; r = 0;
	g_params.polling_table[a][r].push_back(make_pair(	3,					30));
	g_params.polling_table[a][r].push_back(make_pair(	5,				2 * 60));
	g_params.polling_table[a][r].push_back(make_pair(	20,				20 * 60));
	g_params.polling_table[a][r].push_back(make_pair(30 * 60,	2 * 24*60*60));

	// SECRET_TYPE_SELF_ADDRESS with payment
		r++;
	g_params.polling_table[a][r].push_back(make_pair(30 * 60,	2 * 24*60*60));

	// SECRET_TYPE_RECV_ADDRESS nothing received
	a++; r = 0;
	g_params.polling_table[a][r].push_back(make_pair(	10,					90));
	g_params.polling_table[a][r].push_back(make_pair(	15,				10 * 60));
	g_params.polling_table[a][r].push_back(make_pair(	30,			1 * 1*60*60));
	g_params.polling_table[a][r].push_back(make_pair(	45,			4 * 1*60*60));
	g_params.polling_table[a][r].push_back(make_pair(2 * 60,	2 * 24*60*60));

	// SECRET_TYPE_RECV_ADDRESS with payment
		r++;
	g_params.polling_table[a][r].push_back(make_pair(	30,				2 * 60));
	g_params.polling_table[a][r].push_back(make_pair(2 * 60,			10 * 60));
	g_params.polling_table[a][r].push_back(make_pair(5 * 60,	1 * 24*60*60));

	// SECRET_TYPE_POLL_ADDRESS nothing received
	a++; r = 0;
	g_params.polling_table[a][r].push_back(make_pair(1 * 60,			20 * 60));
	g_params.polling_table[a][r].push_back(make_pair(5 * 60,	2 * 24*60*60));

	// SECRET_TYPE_POLL_ADDRESS with payment (copy SECRET_TYPE_RECV_ADDRESS)
		r++;
	g_params.polling_table[a][r] = g_params.polling_table[a-1][r];

	// SECRET_TYPE_STATIC_ADDRESS nothing received
	a++; r = 0;
	g_params.polling_table[a][r].push_back(make_pair(	20,					2 * 60));
	g_params.polling_table[a][r].push_back(make_pair(	30,					5 * 60));
	g_params.polling_table[a][r].push_back(make_pair(	60,					10 * 60));
	g_params.polling_table[a][r].push_back(make_pair(4 * 60,			1 * 60*60));
	g_params.polling_table[a][r].push_back(make_pair(10 * 60,			12 * 60*60));
	g_params.polling_table[a][r].push_back(make_pair(20 * 60,		7 * 24*60*60));
	g_params.polling_table[a][r].push_back(make_pair(6 * 60*60,		60 * 24*60*60));
	g_params.polling_table[a][r].push_back(make_pair(12 * 60*60,	INT_MAX/4));

	// SECRET_TYPE_STATIC_ADDRESS with payment (copy nothing received)
		r++;
	g_params.polling_table[a][r] = g_params.polling_table[a][0];

	get_proof_key_dir(g_params.proof_key_dir, g_params.process_dir);

	string def = CCAPPDIR;
	expand_number(def, g_params.blockchain);
	get_app_data_dir(g_params.app_data_dir, def);
	if (!g_params.app_data_dir.length())
		throw runtime_error("No data directory specified");

	g_rpc_service.address_string = LOCALHOST; // for now, only allow RPC on localhost

	if (!g_params.base_port)
		g_params.base_port = BASE_PORT + 20 * (g_params.blockchain % ((BASE_PORT_TOP - BASE_PORT)/20));

	if (!g_params.transact_port)
		g_params.transact_port = g_params.base_port;

	set_service_configs();

	if (g_params.config_options.count("show-config"))
		do_show_config();

	check_config_values();

	return 0;
}

static int set_rpc_auth_string()
{
	// note that the "cookie" is not an HTTP cookie; it is an HTTP Basic Authentication user:password
	// this basically duplicates bitcoin, i.e, "__cookie__:" + 256-bit random saved in file ".cookie"

	string cookie;

	if (g_rpc_service.pass_string.length())
	{
		cookie = g_rpc_service.user_string + ":" + g_rpc_service.pass_string;
	}
	else
	{
		char r[256/8];
		CCRandom(r, sizeof(r));

		cookie = "__cookie__:";
		cookie += buf2hex(r, sizeof(r), 0);
	}

	wstring fname = g_params.app_data_dir + s2w(PATH_DELIMITER) + L".cookie";

	BOOST_LOG_TRIVIAL(info) << "Saving RCP user and password to file " << fname;
	//BOOST_LOG_TRIVIAL(info) << "RCP cookie = " << cookie;	// logging this would be a security leak

	auto fd = open_file(fname, O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);
	if (fd == -1)
	{
		perror("FATAL ERROR opening cookie file");
		return -1;
	}

	auto rc = write(fd, cookie.data(), cookie.size());
	if ((unsigned)rc != cookie.size())
	{
		perror("FATAL ERROR writing to cookie file");
		return -1;
	}

	rc = close(fd);
	if (rc)
	{
		perror("FATAL ERROR closing cookie file");
		return -1;
	}

	string encoded;
	base64_encode(base64, cookie, encoded);

	g_rpc_service.auth_string = " Basic ";
	g_rpc_service.auth_string += encoded;

	//BOOST_LOG_TRIVIAL(info) << "RCP Authorization string = " << g_rpc_service.auth_string;	// logging this would be a security leak

	lock_guard<FastSpinLock> lock(g_cout_lock);
	cerr << "RPC service username and password saved in file " << w2s(fname) << endl;

	return 0;
}

static void shutdown_callback()
{
	Billet::Shutdown();

	g_txbuildlist.Shutdown();
}

#if 0 // not used
	for (unsigned i = 2048; i > 512; --i)
	{
		auto rc = _setmaxstdio(i);
		if (rc >= 0)
		{
			//cerr << "_setmaxstdio " << i << endl;
			break;
		}
	}
#endif

#ifdef __MINGW64__
int _dowildcard = 0;	// disable wildcard globbing
#endif

int main(int argc, char **argv)
{
	cerr << endl;

	g_shutdown_callback = shutdown_callback;

	set_handlers();

	//srand(0);
	srand(time(NULL));

	g_params.trace_level = DEFAULT_TRACE_LEVEL;
	set_trace_level(g_params.trace_level);

	g_interactive = true;

	g_params.process_dir = get_process_dir();
	if (!g_params.process_dir.length())
		return -1;

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

	//base64_test(base64, base64int); return 0;

	if (g_params.config_options.count("interactive"))
		g_params.interactive = true;
	else
		g_params.interactive = false;

	auto command_line_json = command_line_to_json();

	if (g_params.config_options.count("dry-run"))
	{
		if (command_line_json.size())
			cerr << "\nJSON command to execute:\n" << command_line_json << endl;

		return 1;
	}

	bool need_tor_proxy = g_params.transact_tor;
	thread tor_thread(tor_start, g_params.process_dir, g_params.tor_exe, g_params.tor_config, g_params.app_data_dir, need_tor_proxy, ref(g_tor_services), g_tor_services.size()-1);

	int result_code = -99;

	DbConn *dbconn = NULL;
	TxQuery *txquery_interactive = NULL;

	auto create_wallet = g_params.config_options.count("create-wallet");
	auto reset_wallet = g_params.config_options.count("reset-wallet");

	try
	{
		CCProof_Init(g_params.proof_key_dir);
		CCProof_PreloadVerifyKeys();

		dbconn = new DbConn(false);

		dbconn->Startup(create_wallet);

		dbconn->ReadWalletId();

		if (create_wallet)
		{
			try
			{
				auto rc = Secret::CreateBaseSecrets(dbconn);

				if (rc) goto do_fatal;
			}
			catch (const exception& e)
			{
				BOOST_LOG_TRIVIAL(warning) << "Deleting all secrets";

				CCASSERTZ(dbconn->Exec("delete from Secrets;"));

				throw;
			}

			dbconn->TransactionInitDb();
		}

		if (reset_wallet)
		{
			auto rc = Billet::ResetAllocated(dbconn, true);

			if (rc) goto do_fatal;
		}

	}
	catch (const exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;

		goto do_fatal;
	}
	catch (...)
	{
		goto do_fatal;
	}

	#if 0
	dbconn->LockTest();
	dbconn->TestConcurrency();
	return -1;
	#endif

	if (g_btc_block.Init(dbconn))
		goto do_fatal;

	if (g_params.transact_tor)
	{
		auto rc = TxQuery::ReadHostsFile(g_params.transact_tor_hosts_file);
		if (rc)
			goto do_fatal;
	}

	g_lpc_service.Start();

	txquery_interactive = g_lpc_service.GetConnection(false);
	CCASSERT(txquery_interactive);

	if (command_line_json.size())
	{
		result_code = interactive_do_json_command(command_line_json, dbconn, *txquery_interactive);
	}

	//start_shutdown();	// for testing

	if (g_rpc_service.enabled)
	{
		auto rc = set_rpc_auth_string();
		if (rc) goto do_fatal;

		g_rpc_service.Start();

		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "RPC service enabled on port " << g_rpc_service.port << "\n" << endl;
	}
	else if (!command_line_json.size() || g_params.interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Note: RPC service not enabled ";
		if (g_params.interactive)
			cerr << "(start " CCEXENAME " with --wallet-rpc=1 if RPC is required)";
		else
			cerr << "(try using --interactive and/or --wallet-rpc=1)";
		cerr << endl;
	}

	result_code = 0;

	if (g_rpc_service.enabled || g_params.interactive)
	{
		Polling g_polling;

		g_polling.Start(g_params.polling_threads);

		if (g_params.interactive)
		{
			cerr << endl;

			do_interactive(dbconn, *txquery_interactive);

			//cerr << "do_interactive done" << endl;
		}

		while (g_rpc_service.enabled && !g_shutdown)
			wait_for_shutdown(500);

		BOOST_LOG_TRIVIAL(info) << "Shutting down...";

		start_shutdown();

		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr << "\nShutting down..." << endl;
		}

			if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 1...";

		g_polling.StartShutdown();

			if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 2...";

		g_polling.WaitForShutdown();
	}

do_fatal:

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 3...";

	start_shutdown();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 4...";

	wait_for_shutdown();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 5...";

	shutdown_callback();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 6...";

	cc_mint_threads_shutdown();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 7...";

	Transaction::Shutdown();

	if (txquery_interactive)
	{
			if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 8...";

		txquery_interactive->Stop();

			if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 9...";

		txquery_interactive->WaitForStopped();

			if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 10...";

		txquery_interactive->FreeConnection();
	}

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 11...";

	g_rpc_service.StartShutdown();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 12...";

	g_lpc_service.StartShutdown();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 14...";

	g_rpc_service.WaitForShutdown();

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 15...";

	g_lpc_service.WaitForShutdown();

	if (dbconn)
	{
			if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 16...";

		dbconn->CloseDb(1);	// 1 = done

		delete dbconn;
	}

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 17...";

	dblog(sqlite3_shutdown());

		if (TRACE_SHUTDOWN) BOOST_LOG_TRIVIAL(debug) << "shutdown 18...";

	tor_thread.join();

	BOOST_LOG_TRIVIAL(info) << CCEXENAME << " returning " << result_code;

	if (result_code == -99)
		cerr << "ERROR: " << CCEXENAME << " startup or initialization failed" << endl;
	else if (!command_line_json.size() || g_params.interactive)
		cerr << CCEXENAME << " done" << endl;

	finish_handlers();

	//*(int*)0 = 0;

	return result_code;
}
