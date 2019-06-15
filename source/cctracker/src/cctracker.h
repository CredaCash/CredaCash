/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * cctracker.h
*/

#pragma once

#define CCAPPNAME	"CredaCash Directory Server"
#define CCVERSION	 "0.97" //@@!
#define CCEXENAME	"cctracker"
#define CCAPPDIR	"CCTracker"

#include <CCdef.h>
#include <CCboost.hpp>
#include <apputil.h>
#include <osutil.h>

#include <boost/program_options/variables_map.hpp>

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
