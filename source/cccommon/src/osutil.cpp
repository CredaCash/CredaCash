/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * osutil.cpp
*/

#include "CCdef.h"
#include "osutil.h"

#include <boost/core/demangle.hpp>

#ifndef _WIN32
#include <execinfo.h>
#endif

bool g_is_dll = false;
volatile bool g_shutdown = false;
void (*g_shutdown_callback)() = NULL;

static mutex shutdown_mutex;
static condition_variable shutdown_condition_variable;
static volatile bool return_injected;
static volatile bool terminate_is_recursing = false;
static volatile bool shutdown_done;

#ifdef _WIN32

static void inject_return_into_console()
{
	if (return_injected)
		return;

	return_injected = true;

	//Beep(2000, 500);

	// inject VK_RETURN into console to allow ReadConsole/getline to finish

	DWORD nwritten;
	INPUT_RECORD inp;
	memset(&inp, 0, sizeof(inp));
	inp.EventType = KEY_EVENT;
	inp.Event.KeyEvent.bKeyDown = TRUE;
	inp.Event.KeyEvent.uChar.AsciiChar = VK_RETURN;
	WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &inp, 1, &nwritten);
}

static BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
	if (dwCtrlType != CTRL_CLOSE_EVENT)
		return false;

	//Beep(800, 500);

	start_shutdown();

	raise(SIGTERM);

	while (!shutdown_done)
		usleep(50*1000);

	return true;
}

#endif // _WIN32

static void handle_signal(int)
{
	start_shutdown();
}

static void handle_terminate()
{
	auto e = current_exception();

	if (!e)
	{
		cerr << "\nTerminate handler called";
		if (terminate_is_recursing) cerr << " (recursing)";
		cerr << endl;
	}

	try
	{
		if (e) rethrow_exception(e);
	}
	catch(const exception& e)
	{
		cerr << "\nERROR unexpected exception " << boost::core::demangle(typeid(e).name()) << endl;
		cerr << e.what() << "\n" << endl;
	}

#ifndef _WIN32
#ifndef __ANDROID__
	if (!terminate_is_recursing)
	{
		void *array[20];
		auto size = backtrace(array, sizeof(array)/sizeof(void*));
		backtrace_symbols_fd(array, size, STDERR_FILENO);
		cerr << endl;
	}
#endif
#endif

	terminate_is_recursing = true;

	start_shutdown();

	sleep(10);

	if (!terminate_is_recursing)
	{
		terminate();

		sleep(15);
	}

	abort();
	exit(-1);
}

void set_handlers()
{
	g_shutdown = false;
	return_injected = false;
	terminate_is_recursing = false;
	shutdown_done = false;

#ifdef _WIN32
	SetConsoleCtrlHandler(HandlerRoutine, true);
#endif

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	#if defined(SIGQUIT)
	signal(SIGQUIT, handle_signal);
	#endif

	set_terminate(handle_terminate);
}

void finish_handlers()
{
	shutdown_done = true;
}

void start_shutdown()
{
	if (g_shutdown)
		return;

	lock_guard<mutex> lock(shutdown_mutex);

	if (g_shutdown)
		return;

	//cerr << "shutdown" << endl;

	g_shutdown = true;

	//Beep(2000, 500);

	shutdown_condition_variable.notify_all();

	if (g_shutdown_callback)
		g_shutdown_callback();

#ifdef _WIN32
	inject_return_into_console();
#endif
}

bool wait_for_shutdown(unsigned millisec)
{
	if (g_shutdown)
		return true;

	unique_lock<mutex> lock(shutdown_mutex);

	if (g_shutdown)
		return true;

	if (millisec != (unsigned)(-1))
	{
		shutdown_condition_variable.wait_for(lock, chrono::milliseconds(millisec));
	}
	else while (!g_shutdown)
	{
		shutdown_condition_variable.wait(lock);
	}

	return g_shutdown;
}

bool ccsleep(int sec)
{
	if (sec > 0)
		return wait_for_shutdown(sec*1000);
	else
		return g_shutdown;
}

void set_nice(int _nice)
{
	if (!_nice)
		return;

#ifdef _WIN32
	auto hthread = GetCurrentThread();
	int priority = GetThreadPriority(hthread);

	priority -= _nice;

	if (priority > THREAD_PRIORITY_TIME_CRITICAL)
		priority = THREAD_PRIORITY_TIME_CRITICAL;
	else if (priority > THREAD_PRIORITY_HIGHEST)
		priority = THREAD_PRIORITY_HIGHEST;

	if (priority < THREAD_PRIORITY_IDLE)
		priority = THREAD_PRIORITY_IDLE;
	else if (priority < THREAD_PRIORITY_LOWEST)
		priority = THREAD_PRIORITY_LOWEST;

	auto rc = SetThreadPriority(hthread, priority);
	(void)rc;

	//cerr << "SetThreadPriority(" << hthread << "," << priority << ") returned " << rc << " GetLastError " << GetLastError() << endl;
#else
	auto rc = nice(_nice);
	(void)rc;

	//cerr << "nice(" << _nice << ") returned " << rc << " errno " << errno << endl;
#endif
}
