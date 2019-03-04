/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * config.h for sqlite
*/

#pragma once

//!#define TEST_SQLITE_DEBUG_BUILD			1

#ifndef TEST_SQLITE_DEBUG_BUILD
#ifdef _DEBUG
#define TEST_SQLITE_DEBUG_BUILD			1	// for _DEBUG build
#else
#define TEST_SQLITE_DEBUG_BUILD			0	// don't test
#endif
#endif

#define SQLITE_OMIT_DECLTYPE			1
#define SQLITE_OMIT_DEPRECATED			1
#define SQLITE_OMIT_PROGRESS_CALLBACK	1
#define SQLITE_USE_ALLOCA				1

#define SQLITE_OMIT_AUTOINIT			1
#define SQLITE_OMIT_AUTORESET			1
#define SQLITE_OMIT_UTF16				1

#define HAVE_MALLOC_USABLE_SIZE			1
#define HAVE_USLEEP						1

#ifdef _WIN32
#warning SQLITE _WIN32
//#define SQLITE_4_BYTE_ALIGNED_MALLOC	1	// not needed
#if TEST_SQLITE_DEBUG_BUILD
//#define SQLITE_WIN32_MALLOC				1
//#define SQLITE_WIN32_MALLOC_VALIDATE	1
#endif

#else
#define HAVE_LOCALTIME_R				1
#define HAVE_STRCHRNUL					1
#define HAVE_FDATASYNC					1
#endif

#if TEST_SQLITE_DEBUG_BUILD
#warning SQLITE _DEBUG
//#error SQLITE _DEBUG
#define SQLITE_DEBUG					1
#define SQLITE_ENABLE_API_ARMOR			1
#define SQLITE_ENABLE_EXPLAIN_COMMENTS	1
#ifndef SQLITE_WIN32_MALLOC
#define SQLITE_MEMDEBUG					1
#endif
//#define SQLITE_ENABLE_SQLLOG			1	// for this to work, append "test_sqllog.c" to the end of "sqlite3.c"
#else
//#error _DEBUG is not set
#endif
