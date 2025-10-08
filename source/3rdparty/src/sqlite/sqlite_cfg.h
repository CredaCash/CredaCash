/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * config.h for sqlite
*/

#pragma once

//!#define TEST_SQLITE_DEBUG_BUILD			1

#ifndef TEST_SQLITE_DEBUG_BUILD
#ifdef _DEBUG
#define TEST_SQLITE_DEBUG_BUILD			1	// for _DEBUG build (this line ok for production release)
#else
#define TEST_SQLITE_DEBUG_BUILD			0	// don't test
#endif
#endif

#define SQLITE_DQS						0
#define SQLITE_ENABLE_NULL_TRIM			1
#define SQLITE_LIKE_DOESNT_MATCH_BLOBS	1
#define SQLITE_MAX_EXPR_DEPTH			0

#define SQLITE_OMIT_AUTOINIT			1
#define SQLITE_OMIT_AUTORESET			1
#define SQLITE_OMIT_COMPLETE			1
#define SQLITE_OMIT_DECLTYPE			1
#define SQLITE_OMIT_DEPRECATED			1
#define SQLITE_OMIT_DESERIALIZE			1
#define SQLITE_OMIT_INTROSPECTION_PRAGMAS 1
#define SQLITE_OMIT_JSON				1
#define SQLITE_OMIT_LIKE_OPTIMIZATION	1
#define SQLITE_OMIT_LOAD_EXTENSION		1
#define SQLITE_OMIT_PROGRESS_CALLBACK	1
#define SQLITE_OMIT_TCL_VARIABLE		1
#define SQLITE_OMIT_TRACE				1
#define SQLITE_OMIT_UTF16				1

#define SQLITE_USE_ALLOCA				1

#define HAVE_MALLOC_USABLE_SIZE			1
#define HAVE_USLEEP						1

#ifdef _WIN32
//#warning SQLITE _WIN32
#define SQLITE_USE_SEH					1
//#define SQLITE_4_BYTE_ALIGNED_MALLOC	1	// not needed
#if TEST_SQLITE_DEBUG_BUILD
//#define SQLITE_WIN32_MALLOC				1
//#define SQLITE_WIN32_MALLOC_VALIDATE	1
#endif

#else
#define HAVE_LOCALTIME_R				1
#ifndef __ANDROID__
#define HAVE_STRCHRNUL					1
#endif
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
