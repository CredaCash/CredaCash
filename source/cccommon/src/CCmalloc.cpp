/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCmalloc.cpp
*/

// Note: Under Windows, this code requires a pdb format symbol table
//	which can be created from a gcc exe using cv2pdb at https://github.com/rainers/cv2pdb

#include "CCdef.h"
#include "CCmalloc.h"

#if TEST_LOG_MALLOCS

#undef malloc
#undef calloc
#undef realloc
#undef free

static volatile bool blog = false;
thread_local static bool blog_not_thread = false;
static volatile unsigned counter = 0;

#define MAX_CACHE	200*1000

#define CLEAR_CACHE_AFTER_WALK	0

static const unsigned depth = 10;
static const unsigned skip = 2;
static const char delim[] = " ;; ";
static uintptr_t cache_p[MAX_CACHE];		// pointer
static size_t	 cache_n[MAX_CACHE];		// size
static unsigned  cache_c[MAX_CACHE];		// counter
static unsigned	 cache_t[MAX_CACHE];		// thread id
static uintptr_t cache_s[MAX_CACHE][depth];	// stack
static unsigned free_start = 0;
static unsigned alloc_end = 0;
static unsigned high_water = 0;
static bool sym_inited = false;
static mutex m_mutex;

#include <DbgHelp.h>

static inline void sym_init()
{
	if (!sym_inited)
	{
		SymInitialize(GetCurrentProcess(), NULL, TRUE);

		sym_inited = true;
	}
}

static void add_stack(uintptr_t *cache)
{
	memset(cache, 0, depth * sizeof(uintptr_t));

	sym_init();

	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	CONTEXT context;
	memset(&context, 0, sizeof(CONTEXT));
	context.ContextFlags = CONTEXT_FULL;
	RtlCaptureContext(&context);

	STACKFRAME64 stackframe;
	memset(&stackframe, 0, sizeof(STACKFRAME64));
	stackframe.AddrPC.Offset = context.Rip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Rsp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Rsp;
	stackframe.AddrStack.Mode = AddrModeFlat;

	for (size_t i = 0; i < depth + skip; i++)
	{
		auto rc = StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread,
			&stackframe, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);

		if (!rc)
			break;

		if (i < skip)
			continue;

		cache[i - skip] = stackframe.AddrPC.Offset;
	}
}

static int add_sym(HANDLE process, uintptr_t p, char *str)
{
	if (!p)
		return 0;

	char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
	memset(buffer, 0, sizeof(buffer));
	PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = MAX_SYM_NAME;

	DWORD64 displacement = 0;
	if (!SymFromAddr(process, p, &displacement, symbol))
	{
		strcpy(symbol->Name, "???");

		LPTSTR lpMsgBuf;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
			GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
		printf("### add_sym %llu err %s\n", p, lpMsgBuf);
		LocalFree(lpMsgBuf);
	}

	symbol->Name[MAX_SYM_NAME-1] = 0;

	return sprintf(str, "%s", symbol->Name);
}

static void add_cache(void *p, size_t size)
{
	lock_guard<mutex> lock(m_mutex);

	for (unsigned i = free_start; i < MAX_CACHE; ++i)
	{
		if (cache_p[i])
			continue;

		cache_p[i] = (uintptr_t)p;
		cache_n[i] = size;
		cache_c[i] = ++counter;
		cache_t[i] = cc_thread_id();
		add_stack(cache_s[i]);

		free_start = i + 1;

		if (i > alloc_end)
		{
			alloc_end = i;

			auto hi = i & ~1023;
			if (hi > high_water)
			{
				high_water = hi;

				printf("### add_cache high_water %u ### \n", hi);
			}
		}

		return;
	}

	if (high_water != (unsigned)(-1))
	{
		printf("### ERROR add_cache overflow ### \n");

		high_water = -1;
	}
}

static void delete_cache(void *p)
{
	lock_guard<mutex> lock(m_mutex);

	for (unsigned i = alloc_end; i <= alloc_end; --i)
	{
		if (cache_p[i] != (uintptr_t)p)
			continue;

		cache_p[i] = 0;

		if (i < free_start)
			free_start = i;

		if (i == alloc_end)
		{
			for (unsigned j = alloc_end - 1; j <= alloc_end; --j)
			{
				if (cache_p[j])
				{
					alloc_end = j;

					break;
				}
			}
		}

		return;
	}
}

static void walk_cache()
{
	printf("***** walk_cache\n");

	sym_init();

	HANDLE process = GetCurrentProcess();

	for (unsigned i = 0; i <= alloc_end; ++i)
	{
		if (!cache_p[i])
			continue;

		char str[(MAX_SYM_NAME + 20) * depth + 200];

		auto n = sprintf(str, "### %llu %llu %u 0x%08x", cache_n[i], cache_p[i], cache_c[i], cache_t[i]);

		for (unsigned j = 0; j < depth; ++j)
		{
			n += sprintf(&str[n], "%s", delim);
			n += add_sym(process, cache_s[i][j], &str[n]);
		}

		printf("%s ###\n", str);

		if (CLEAR_CACHE_AFTER_WALK)
			cache_p[i] = 0;
	}

	if (CLEAR_CACHE_AFTER_WALK)
	{
		free_start = 0;
		alloc_end = 0;
	}

	SymCleanup(process);
	sym_inited = false;

	printf("***** walk_cache done.\n");
}

bool cc_malloc_logging(int on)
{
	lock_guard<mutex> lock(m_mutex);

	auto rv = blog;

	blog = on;

	if (rv != blog)
		printf("### 0x%08x cc_malloc_logging %d (was %d) counter %u ###\n", cc_thread_id(), blog, rv, counter);

	if (rv and !blog)
		walk_cache();

	return rv;
}

bool cc_malloc_logging_not_this_thread(int on)
{
	auto rv = blog_not_thread;

	if (on >= 0)
	{
		printf("### 0x%08x cc_malloc_logging_not_this_thread %d ###\n", cc_thread_id(), on);

		blog_not_thread = on;
	}

	return rv;
}

void* operator new(size_t size)
{
	auto p = malloc(size);

	if (!p) throw std::bad_alloc();

	if (blog && !blog_not_thread)
		add_cache(p, size);

	return p;
}

void* operator new[](size_t size)
{
	auto p = malloc(size);

	if (!p) throw std::bad_alloc();

	if (blog && !blog_not_thread)
		add_cache(p, size);

	return p;
}

void* operator new(size_t size, const std::nothrow_t& tag)
{
	auto p = malloc(size);

	if (blog && !blog_not_thread)
		add_cache(p, size);

	return p;
}

void* operator new[](size_t size, const std::nothrow_t& tag)
{
	auto p = malloc(size);

	if (blog && !blog_not_thread)
		add_cache(p, size);

	return p;
}

void operator delete(void* p)
{
	if (blog && !blog_not_thread)
		delete_cache(p);

	free(p);
}

void operator delete[](void* p)
{
	if (blog && !blog_not_thread)
		delete_cache(p);

	free(p);
}

void* std::cc_malloc(size_t size, const char *file, int line)
{
	auto p = malloc(size);

	if (blog && !blog_not_thread)
		add_cache(p, size);

	return p;
}

void* std::cc_calloc(size_t num, size_t size, const char *file, int line)
{
	auto p = calloc(num, size);

	if (blog && !blog_not_thread)
		add_cache(p, size);

	return p;
}

void* std::cc_realloc(void* p, size_t size, const char *file, int line)
{
	if (blog && !blog_not_thread)
		delete_cache(p);

	p = realloc(p, size);

	if (blog && !blog_not_thread)
		add_cache(p, size);

	return p;
}

void std::cc_free(void* p, const char *file, int line)
{
	if (blog && !blog_not_thread)
		delete_cache(p);

	free(p);
}

#endif
