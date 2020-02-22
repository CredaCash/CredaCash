/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * cclib.h
*/

#pragma once

#define _LARGEFILE64_SOURCE

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#define WINVER			0x0502
#define _WIN32_WINNT	0x0502
#define _WIN32_IE		0x0500

#define _UNICODE

#include <windows.h>

#define PATH_DELIMITER	"\\"

#else

#define PATH_DELIMITER	"/"

#endif

#include <cstdint>
#include <cstring>
#include <string>
#include <memory>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <unistd.h>

#include <CCutil.h>
#include <CCticks.hpp>
#include <CCassert.h>

#include "CCbigint.hpp"

#include <snarkfront/snarkfront.hpp>

using namespace std;
using namespace snarkfront;

#define CONCAT_TOKENS(a,b)		a##b
#define WIDE(x)					CONCAT_TOKENS(L,x)

#define CCRESULT std::int32_t

#ifdef CC_DLL_EXPORTS
#ifdef _WIN32
#define CCAPI extern "C" __stdcall __declspec(dllexport) CCRESULT
#else
#define CCAPI extern "C" CCRESULT
#endif // _WIN32
#else
#define CCAPI CCRESULT
#endif // CC_DLL_EXPORTS

#include "CCapi.h"

