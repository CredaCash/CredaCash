/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * osutil.cpp
*/

#include "CCdef.h"
#include "osutil.h"

volatile bool g_shutdown;
void (*g_shutdown_callback)() = NULL;

static mutex shutdown_mutex;
static condition_variable shutdown_condition_variable;
static volatile bool shutdown_done;

#ifdef _WIN32

static void inject_return_into_console()
{
	static bool already_done = false;
	if (already_done)
		return;
	already_done = true;

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
	cerr << "handle_terminate" << endl;

#if 0
	void *array[20];

	auto size = backtrace(array, sizeof(array)/sizeof(void*));
	auto strings = backtrace_symbols(array, size);

	for (unsigned i = 0; i < size; ++i)
		cerr << strings[i] << endl;
#endif

	start_shutdown();

	//abort();
}

void set_handlers()
{
#ifdef _WIN32
	SetConsoleCtrlHandler(HandlerRoutine, true);
#endif

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	#if defined(SIGQUIT)
	signal(SIGQUIT, handle_signal);
	#endif

	//set_terminate(handle_terminate);	// for debugging
	(void)handle_terminate;
}

void finish_handlers()
{
	shutdown_done = true;
}

void start_shutdown()
{
	lock_guard<mutex> lock(shutdown_mutex);

	//Beep(2000, 500);

	g_shutdown = true;

	shutdown_condition_variable.notify_all();

	if (g_shutdown_callback)
		g_shutdown_callback();

#ifdef _WIN32
	inject_return_into_console();
#endif
}

void wait_for_shutdown(unsigned millisec)
{
	unique_lock<mutex> lock(shutdown_mutex);

	if (g_shutdown)
		return;

	if (millisec != (unsigned)(-1))
	{
		shutdown_condition_variable.wait_for(lock, chrono::milliseconds(millisec));

		return;
	}
	else
	{
		while (!g_shutdown)
		{
			shutdown_condition_variable.wait(lock);
		}
	}
}

void ccsleep(int sec)
{
	if (sec > 0)
		wait_for_shutdown(sec*1000);
}

void set_nice(int nice)
{
	if (!nice)
		return;

#ifdef _WIN32
	auto hthread = GetCurrentThread();
	int priority = GetThreadPriority(hthread);

	priority -= nice;

	if (priority > THREAD_PRIORITY_TIME_CRITICAL)
		priority = THREAD_PRIORITY_TIME_CRITICAL;
	else if (priority > THREAD_PRIORITY_HIGHEST)
		priority = THREAD_PRIORITY_HIGHEST;

	if (priority < THREAD_PRIORITY_IDLE)
		priority = THREAD_PRIORITY_IDLE;
	else if (priority < THREAD_PRIORITY_LOWEST)
		priority = THREAD_PRIORITY_LOWEST;

	SetThreadPriority(hthread, priority);
#else
	sched_param sp;
	sched_getparam(0, &sp);

	if (nice < 0)
	{
		if (sp.sched_priority > nice)
			sp.sched_priority -= nice;
		else if (sp.sched_priority > 1)
			sp.sched_priority = 1;
	}
	else
	{
		sp.sched_priority -= nice;

		if (sp.sched_priority > 99)
			sp.sched_priority = 99;
	}

	sched_setparam(0, &sp);
#endif
}
