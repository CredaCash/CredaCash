/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCassert.cpp
*/

#include "CCdef.h"
#include "CCassert.h"

//#define TEST_BREAK_ON_ASSERT	1	// must be commented out for production

#ifndef TEST_BREAK_ON_ASSERT
#define TEST_BREAK_ON_ASSERT	0	// don't break
#endif

extern "C" unsigned cc_thread_id()
{
	#ifdef _WIN32
	return GetCurrentThreadId();
	#else
	return syscall(SYS_gettid);
	#endif
}

extern "C" void __ccassert(const char *msg, const char *file, int line)
{
	ostringstream os;
	os << "error thread 0x" << hex << cc_thread_id() << dec << " assert(" << msg << ") false at " << file << ":" << line;
	cout << os.str() << endl;

#if TEST_BREAK_ON_ASSERT
	*(int*)0 = 0;
#endif

	throw runtime_error(os.str());
}

extern "C" void __ccassertz(const char *msg, uintptr_t x, const char *file, int line)
{
	ostringstream os;
	os << "error thread 0x" << hex << cc_thread_id() << dec << " assertz(" << msg << ") is " << x << " at " << file << ":" << line;
	cout << os.str() << endl;

#if TEST_BREAK_ON_ASSERT
	*(int*)0 = 0;
#endif

	throw runtime_error(os.str());
}
