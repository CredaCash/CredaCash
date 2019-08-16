/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * cctracker.cpp
*/

#define DECLARE_GLOBALS

#include "cctracker.h"
#include "dirserver.h"

#include <memory>
#include <utility>

#include <boost/asio.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>

#define DEFAULT_THREADS_PER_SERVICE	"32"

#define DEFAULT_TRACE_LEVEL	4

static void set_nthreads()
{
	if (!g_nthreads)
	{
		g_nthreads = thread::hardware_concurrency();

		if (!g_nthreads)
		{
			g_nthreads = atoi(DEFAULT_THREADS_PER_SERVICE);

			BOOST_LOG_TRIVIAL(warning) << "std::thread::hardware_concurrency is indeterminant; using program default value " << g_nthreads;
		}
	}
}

static void check_config_values()
{
	if (g_port < 1 || g_port > 0xFFFF)
		throw range_error("port value not in valid range");

	if (g_nthreads < 1 || g_nthreads > 5000)
		throw range_error("nthreads value not in valid range");

	if (g_nconns < 1 || g_nconns > 50000)
		throw range_error("nconns value not in valid range");

	if (g_datamem < 1 || g_datamem > (1L << 30))
		throw range_error("memory value not in valid range");

	if (g_blockfrac < 1 || g_blockfrac > 50)
		throw range_error("blockserver memory percentage not in valid range");

	if (g_hashfill < 50 || g_hashfill > 99)
		throw range_error("hash fill precentage not in valid range");

	if (g_expire < 1 || g_expire > 11000)
		throw range_error("expire minutes not in valid range");
}

static int process_options(int argc, char **argv)
{
	namespace po = boost::program_options;

	g_nthreads = 0;

	po::options_description basic_options("");
	basic_options.add_options()
		("help", "Display this message.")
		("trace", po::value<int>(&g_trace_level)->default_value(DEFAULT_TRACE_LEVEL), "Trace level (0=none; 6=all).")
		("config", po::wvalue<wstring>(), "Path to file with additional configuration options.")
		("port", po::value<int>(&g_port)->default_value(9180), "Port for server.")
		("threads", po::value<int>(&g_nthreads), "Number of threads;"
				" default is the value returned by std::thread::hardware_concurrency,"
				" or " DEFAULT_THREADS_PER_SERVICE " if that result is indeterminate.")
		("conns", po::value<int>(&g_nconns)->default_value(100), "Number of connections per thread.")
		("datamem", po::value<int>(&g_datamem)->default_value(1), "Memory for directory data, in units of 8MB ~= 125K directory entries.")
		("blockfrac", po::value<int>(&g_blockfrac)->default_value(5), "Percentage of memory for blockserver directory.")
		("hashfill", po::value<int>(&g_hashfill)->default_value(70), "Hash table fill percentage.")
		("expire", po::value<int>(&g_expire)->default_value(35), "Number of minutes until a directory entry expires.")
	;

	po::options_description all;
	all.add(basic_options); //.add(advanced_options).add(hidden_options);

	po::store(po::parse_command_line(argc, argv, all), g_config_options);

	if (g_config_options.count("help"))
	{
		cout << CCAPPNAME " v" CCVERSION << endl << endl;
		cout << basic_options << endl;
		//cout << advanced_options << endl;

		return 1;
	}

	if (g_config_options.count("config"))
	{
		boost::filesystem::ifstream fs;
		auto fname = g_config_options.at("config").as<wstring>();
		fs.open(fname, fstream::in);
		if(!fs.is_open())
			throw runtime_error(string("Unable to open config file \"") + w2s(fname) + "\"");

		po::store(po::parse_config_file(fs, all), g_config_options);

		set_trace_level(g_trace_level);
	}

	po::notify(g_config_options);

	set_trace_level(g_trace_level);

	//for (auto v : g_config_options)
	//	cout << "config option: " << v.first << endl;

	set_nthreads();

	check_config_values();

	return 0;
}

#ifdef __MINGW64__
int _dowildcard = 0;	// disable wildcard globbing
#endif

int main(int argc, char* argv[])
{
	cerr << endl;

	set_handlers();

	//srand(0);
	srand(time(NULL));

	g_trace_level = DEFAULT_TRACE_LEVEL;
	//g_params.trace_level = 9;
	set_trace_level(g_trace_level);

	cerr << endl;
	cerr << "     *** This program is only useful to set up your own network." << endl;
	cerr << "     *** If you are not trying to set up your own network, then you do not need this program." << endl;
	cerr << endl << endl;
	ccsleep(7);

	try
	{
		auto rc = process_options(argc, argv);
		if (rc) return rc;

		BOOST_LOG_TRIVIAL(info) << "cctracker listening on port " << g_port;

		RunServer();
	}
	catch (const exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return -1;
	}

	while (!g_shutdown)
		wait_for_shutdown(2000);

//do_fatal:

	BOOST_LOG_TRIVIAL(info) << "Shutting down...";

	start_shutdown();
	wait_for_shutdown();

	BOOST_LOG_TRIVIAL(info) << CCEXENAME << " done";
	cerr << CCEXENAME << " done" << endl;

	finish_handlers();

	//*(int*)0 = 0;

	return 0;
}
