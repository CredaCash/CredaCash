/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCdef.h
*/

#pragma once

#define CCVERSION "0.90"

#define _LARGEFILE64_SOURCE

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#define WINVER		 0x0502
#define _WIN32_WINNT 0x0502
#define _WIN32_IE	 0x0500

#define _UNICODE

#include <windows.h>

#endif

#include <cstdint>
#include <string>
#include <memory>
#include <algorithm>
#include <iostream>
#include <unistd.h>

#include <CCassert.h>

#include "CCbigint.hpp"

#include <snarkfront/snarkfront.hpp>

using namespace std;
using namespace snarkfront;

#define CCRESULT std::int32_t

#ifdef CC_DLL_EXPORTS
#define CCAPI extern "C" __stdcall __declspec(dllexport) CCRESULT
#else
#define CCAPI CCRESULT
#endif

#include "CCapi.h"

