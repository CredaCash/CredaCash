/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCassert.cpp
*/

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <CCassert.h>

#include <iostream>

using namespace std;

//#define TEST_BREAK_ON_ASSERT	1	// !!! must be commented out for production

#ifndef TEST_BREAK_ON_ASSERT
#define TEST_BREAK_ON_ASSERT	0	// don't break
#endif

static unsigned threadid()
{
#ifdef _WIN32
	return GetCurrentThreadId();
#else
	return syscall(SYS_gettid);
#endif
}

extern "C" void __ccassert(const char *msg, const char *file, int line)
{
	cout << "error thread 0x" << hex << threadid() << dec << " assert(" << msg << ") false at " << file << ":" << line << endl;

#if TEST_BREAK_ON_ASSERT
	*(int*)0 = 0;
#endif

	throw -1;
}

extern "C" void __ccassertz(const char *msg, uintptr_t x, const char *file, int line)
{
	cout << "error thread 0x" << hex << threadid() << dec << " assertz(" << msg << ") is " << x << " at " << file << ":" << line << endl;

#if TEST_BREAK_ON_ASSERT
	*(int*)0 = 0;
#endif

	throw -1;
}
