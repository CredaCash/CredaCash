/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCdef.h
*/

#pragma once

#ifdef DECLARE_EXTERN
#define DECLARING_EXTERN
#else
#define DECLARE_EXTERN extern
#endif

#define _LARGEFILE64_SOURCE

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#define WINVER			0x0502
#define _WIN32_WINNT	0x0502
#define _WIN32_IE		0x0500

#define _UNICODE

#include <windows.h>
#include <Wincrypt.h>
#include <Shlobj.h>
#include <Winsock2.h>

#define PATH_DELIMITER	"\\"

#define bswap_64	_bswap64
#define bswap_32	_bswap32
#define bswap_16	_bswap16

#else

#include <byteswap.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/socket.h>

#define PATH_DELIMITER	"/"

#endif

#include <cstdlib>
#include <cstdint>
#include <limits>
#include <climits>
#include <string>
#include <cstring>
#include <array>
#include <vector>
#include <deque>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <stdexcept>
#include <random>
#include <utility>
#include <chrono>

#include <unistd.h>
#include <string.h>
#include <wchar.h>
#include <malloc.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std;

#define CONCAT_TOKENS(a,b)		a##b
#define WIDE(x)					CONCAT_TOKENS(L,x)

#define LOCALHOST				"127.0.0.1"

#define BASE_PORT				9200
#define BASE_PORT_TOP			32768

#define ZK_KEY_DIR				"zkkeys"

#define TOR_EXE					"Tor" PATH_DELIMITER "tor.exe"
#define TOR_CONFIG				"tor.conf"

#define TOR_SUBDIR				PATH_DELIMITER "tor"
#define TOR_HOSTNAMES_SUBDIR	TOR_SUBDIR PATH_DELIMITER "hostnames"

#define TOR_HOSTNAME_CHARS		56
#define TOR_HOSTNAME_BYTES		35	// v3 .onion addresses are 56 characters in base32 = 35 bytes

#define RELAY_QUERY_MAX_NAMES	10
#define BLOCK_QUERY_MAX_NAMES	10

#include <boost/version.hpp>

#include "CCmalloc.h"
#include "CCutil.h"
#include "CCticks.hpp"
#include "CCassert.h"
#include "Finally.hpp"

extern bool g_is_dll;
extern volatile bool g_shutdown;
extern void (*g_shutdown_callback)();
