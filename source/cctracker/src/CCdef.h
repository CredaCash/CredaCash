/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCdef.h
*/

#pragma once

#define CCVERSION "0.90"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#define WINVER		0x0502
#define _WIN32_WINNT 0x0502
#define _WIN32_IE	0x0500

#define _UNICODE

#include <windows.h>

#endif

#include <cstdlib>
#include <limits>
#include <cstdint>
#include <climits>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <thread>
#include <mutex>

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

using namespace std;
using namespace boost::log::trivial;

class NoLock
{
	static void lock() {}
};

#define LOCALHOST				"127.0.0.1"

extern volatile bool g_shutdown;
extern boost::program_options::variables_map g_config_options;
extern int g_trace_level;
extern int g_port;
extern int g_nthreads;
extern int g_nconns;
extern int g_datamem;
extern int g_blockfrac;
extern int g_hashfill;
extern int g_expire;

extern class Dir g_relaydir;
extern class Dir g_blockdir;

#ifdef DECLARE_GLOBALS

volatile bool g_shutdown;
boost::program_options::variables_map g_config_options;
int g_trace_level;
int g_port;
int g_nthreads;
int g_nconns;
int g_datamem;
int g_blockfrac;
int g_hashfill;
int g_expire;

#include "dir.hpp"

class Dir g_relaydir;
class Dir g_blockdir;

#endif
