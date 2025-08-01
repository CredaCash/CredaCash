/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCmalloc.h
*/

#pragma once

//#define TEST_LOG_MALLOCS	1

#ifndef TEST_LOG_MALLOCS
#define TEST_LOG_MALLOCS	0	// don't test
#endif

#if !TEST_LOG_MALLOCS

inline bool cc_malloc_logging(int on) { return false; }
inline bool cc_malloc_logging_not_this_thread(int on) { return false; }

#else

bool cc_malloc_logging(int on);
bool cc_malloc_logging_not_this_thread(int on);

#undef malloc
#undef calloc
#undef realloc
#undef free

#define malloc(s)		cc_malloc(s, __FILE__, __LINE__);
#define calloc(n, s)	cc_calloc(n, s, __FILE__, __LINE__);
#define realloc(p, s)	cc_realloc(p, s, __FILE__, __LINE__);
#define free(p)			cc_free(p, __FILE__, __LINE__);

namespace std
{
	void* cc_malloc(size_t size, const char *file, int line);
	void* cc_calloc(size_t num, size_t size, const char *file, int line);
	void* cc_realloc(void* p, size_t size, const char *file, int line);
	void  cc_free(void* p, const char *file, int line);
}

#endif
