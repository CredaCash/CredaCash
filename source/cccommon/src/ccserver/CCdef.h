/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 *CCdef.h
*/

#pragma once

#warning Including CCdef.h from cccommon/ccserver

#define CCVERSION "0.90"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#define WINVER		0x0502
#define _WIN32_WINNT 0x0502
#define _WIN32_IE	0x0500

#define _UNICODE

#include <windows.h>

#endif

#include <cstdint>

#include <boost/system/error_code.hpp>
#include <boost/log/trivial.hpp>

#include <CCassert.h>

using namespace std;
using namespace boost::log::trivial;

class NoLock
{
	static void lock() {}
};

extern volatile bool g_shutdown;
