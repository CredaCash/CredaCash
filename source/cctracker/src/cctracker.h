/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * cctracker.h
*/

#pragma once

#define CCAPPNAME	"CredaCash Directory Server"
#define CCVERSION	 "1.00" //@@!
#define CCEXENAME	"cctracker"
#define CCAPPDIR	"CCTracker"

#include <CCdef.h>
#include <CCboost.hpp>
#include <apputil.h>
#include <osutil.h>

#include <boost/program_options/variables_map.hpp>

extern boost::program_options::variables_map g_config_options;
extern wstring g_datadir;
extern string g_tor_hostname;
extern long long g_difficulty;
extern long long g_magic_nonce;
extern int g_trace_level;
extern int g_port;
extern int g_nthreads;
extern int g_nconns;
extern int g_time_allowance;
extern int g_datamem;
extern int g_blockfrac;
extern int g_hashfill;
extern int g_expire;

extern class Dir g_relaydir;
extern class Dir g_blockdir;

#ifdef DECLARE_GLOBALS

boost::program_options::variables_map g_config_options;
wstring g_datadir;
string g_tor_hostname;
long long g_difficulty;
long long g_magic_nonce;
int g_trace_level;
int g_port;
int g_nthreads;
int g_nconns;
int g_time_allowance;
int g_datamem;
int g_blockfrac;
int g_hashfill;
int g_expire;

#include "dir.hpp"

class Dir g_relaydir;
class Dir g_blockdir;

#endif
