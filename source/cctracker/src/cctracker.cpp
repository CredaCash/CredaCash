/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * cctracker.cpp
*/

#define DECLARE_GLOBALS

#include "CCdef.h"
#include "dirserver.h"

#include <memory>
#include <utility>

#include <boost/asio.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/filesystem/fstream.hpp>

#define DEFAULT_TRACE_LEVEL			4
#define DEFAULT_THREADS_PER_SERVICE	"32"

static void set_trace_level(int level)
{
    boost::log::core::get()->set_filter(boost::log::trivial::severity > (((int)(fatal)) - level));
}

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

static int check_config_values()
{
	if (g_port < 1 || g_port > 0xFFFF)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: port value not in valid range";
		return -1;
	}

	if (g_nthreads < 1 || g_nthreads > 5000)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: nthreads value not in valid range";
		return -1;
	}

	if (g_nconns < 1 || g_nconns > 50000)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: nconns value not in valid range";
		return -1;
	}

	if (g_datamem < 1 || g_datamem > (1 << 30))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: memory value not in valid range";
		return -1;
	}

	if (g_blockfrac < 1 || g_blockfrac > 50)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: blockserver memory percentage not in valid range";
		return -1;
	}

	if (g_hashfill < 50 || g_hashfill > 99)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: hash fill precentage not in valid range";
		return -1;
	}

	if (g_expire < 1 || g_expire > 11000)
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: expire minutes not in valid range";
		return -1;
	}

	return 0;
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
		("port", po::value<int>(&g_port)->default_value(9221), "Port for server.")
		("threads", po::value<int>(&g_nthreads), "Number of threads;"
				" default is the value returned by std::thread::hardware_concurrency,"
				" or " DEFAULT_THREADS_PER_SERVICE " if that result is indeterminate.")
		("conns", po::value<int>(&g_nconns)->default_value(100), "Number of connections per thread.")
		("datamem", po::value<int>(&g_datamem)->required(), "Memory for directory data, in tenths of a GB.")
		("blockfrac", po::value<int>(&g_blockfrac)->default_value(2), "Percentage of memory for blockserver directory.")
		("hashfill", po::value<int>(&g_hashfill)->default_value(70), "Hash table fill percentage.")
		("expire", po::value<int>(&g_expire)->default_value(300), "Number of minutes until a directory entry expires.")
	;

	po::options_description all;
	all.add(basic_options); //.add(advanced_options).add(hidden_options);

	po::store(po::parse_command_line(argc, argv, all), g_config_options);

	set_trace_level(debug);

	if (g_config_options.count("help"))
	{
		cout << "CredaCash Directory Server v" CCVERSION << endl << endl;
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
		{
			BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to open config file \"" << fname << "\"";
			exit(-1);
			throw exception();
			return -1;
		}

		po::store(po::parse_config_file(fs, all), g_config_options);

		set_trace_level(g_trace_level);
	}

	po::notify(g_config_options);

	set_trace_level(g_trace_level);

	//for (auto v : g_config_options)
	//	cout << "config option: " << v.first << endl;

	//if (init_globals())
	//	return -1;

	set_nthreads();

	if (check_config_values())
		return -1;

	return 0;
}


int main(int argc, char* argv[])
{
	g_trace_level = DEFAULT_TRACE_LEVEL;
	set_trace_level(g_trace_level);

	cerr << endl << endl;
	cerr << "     *** This program is only useful to setup your own test network." << endl;
	cerr << "     *** If you are not trying to setup your own test network, then you do not need this program." << endl;
	cerr << endl << endl;
	sleep(7);

	try
	{
		if (process_options(argc, argv))
			return -1;

		BOOST_LOG_TRIVIAL(info) << "cctracker listening on port " << g_port;

		RunServer();
	}
	catch (const exception& e)
	{
		cerr << "FATAL ERROR EXCEPTION: " << e.what() << endl;

		return -1;
	}

	cerr << "cctracker done" << endl;

	return 0;
}

#if 0
	g_dir.Add("yan4bf3fzhz7cen");
	g_dir.Add("yan4bf3fzhz7censs");
	g_dir.Add("yan4bf3fzhz7cen1");
	g_dir.Add("yan4bf3fzhz7cens");
	g_dir.Add("yan4bf3fzhz7cens");
	g_dir.Find("Xan4bf3fzhz7cens");
	g_dir.Find("yan4bf3fzhz7cens");
	g_dir.Delete("Xan4bf3fzhz7cens");
	g_dir.Delete("yan4bf3fzhz7cens");
	g_dir.Delete("yan4bf3fzhz7cens");
	g_dir.Find("yan4bf3fzhz7cens");
#endif

#if 0
	for (unsigned i = 0; i < 20; ++i)
	{
		char name[17];
		sprintf(name, "%016d", i);
		for (unsigned j = 0; j < strlen(name); ++j)
			name[j] += 'a' - '0';
		g_dir.Add(name);
		if (i == 14)
			g_dir.Add("aaaaaaaaaaaaaaac");
		ccsleep(1);
	}
	ccsleep(25);
#endif
