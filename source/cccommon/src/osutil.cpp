/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * osutil.cpp
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "osutil.h"

volatile bool g_shutdown;

static mutex shutdown_mutex;
static condition_variable shutdown_condition_variable;
static volatile bool shutdown_done;

void set_trace_level(int level)
{
    boost::log::core::get()->set_filter(boost::log::trivial::severity > (((int)(fatal)) - level));
}

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

static wstring get_process_dir_static()
{
#ifdef _WIN32
	wstring path(MAX_PATH, 0);
	while (true)
	{
		unsigned int len = GetModuleFileNameW(NULL, &path[0], static_cast< unsigned int >(path.size()));
		if (len < path.size())
		{
			path.resize(len);
			break;
		}

		if (path.size() > 65536)
			return wstring();

		path.resize(path.size() * 2);
	}

	return boost::filesystem::path(path).parent_path().wstring();

#else

	if (boost::filesystem::exists("/proc/self/exe"))
		return boost::filesystem::read_symlink("/proc/self/exe").parent_path().wstring();

	if (boost::filesystem::exists("/proc/curproc/file"))
		return boost::filesystem::read_symlink("/proc/curproc/file").parent_path().wstring();

	if (boost::filesystem::exists("/proc/curproc/exe"))
		return boost::filesystem::read_symlink("/proc/curproc/exe").parent_path().wstring();

	return wstring();
#endif
}

wstring get_process_dir()
{
	auto dir = get_process_dir_static();

	if (dir.empty())
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to get process directory";
		exit(-1);
		throw exception();
		return wstring();
	}

	BOOST_LOG_TRIVIAL(debug) << "process directory = " << w2s(dir);

	return dir;
}

static wstring get_app_data_dir_static(const boost::program_options::variables_map& config_options, const wstring& appname)
{
	wstring path;

	if (config_options.count("datadir"))
	{
		path = config_options.at("datadir").as<wstring>();
		//cerr << "datadir " << w2s(path) << endl;
	}

	if (path.empty())
	{

#ifdef _WIN32

		path.resize(MAX_PATH);
		if (S_OK != SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, &path[0]))
			return wstring();

		path.resize(wcslen(path.c_str()));

		path += WIDE(PATH_DELIMITER);

#else

		const char *home = getenv("HOME");
		if (!home)
			return wstring();

		path = s2w(home);
		path += WIDE(PATH_DELIMITER);
		path += L".";

#endif

		path += L"CredaCash";

		if (create_directory(path))
		{
			BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path);
			exit(-1);
			throw exception();
			return wstring();
		}

		path += WIDE(PATH_DELIMITER);
		path += appname;
	}

	if (create_directory(path))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path);
		exit(-1);
		throw exception();
		return wstring();
	}

	wstring path2 = path + s2w(TOR_SUBDIR);
	if (create_directory(path2))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path2);
		exit(-1);
		throw exception();
		return wstring();
	}

	path2 = path + s2w(TOR_HOSTNAMES_SUBDIR);
	if (create_directory(path2))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path2);
		exit(-1);
		throw exception();
		return wstring();
	}

	return path;
}

wstring get_app_data_dir(const boost::program_options::variables_map& config_options, const wstring& appname)
{
	auto dir = get_app_data_dir_static(config_options, appname);

	if (dir.empty())
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to get application data directory";
		exit(-1);
		throw exception();
		return wstring();
	}

	BOOST_LOG_TRIVIAL(debug) << "application data directory = " << w2s(dir);

	return dir;
}

int create_directory(const wstring& path)
{

#ifdef _WIN32

	DWORD fa = GetFileAttributesW(path.c_str());
	if ((fa == INVALID_FILE_ATTRIBUTES || !(fa & FILE_ATTRIBUTE_DIRECTORY))
				&& !CreateDirectoryW(path.c_str(), NULL))
		return -1;

#else

	if (mkdir(w2s(path).c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP) && errno != EEXIST)
		return -1;

#endif

	return 0;
}

int open_file(const wstring& path, int flags, int mode)
{
	//cout << "open_file " << w2s(path) << endl;

#ifdef _WIN32
	int fd = _wopen(path.c_str(), flags, mode);
#else
	int fd = open(w2s(path).c_str(), flags, mode);
#endif

	return fd;
}

int delete_file(const wstring& path)
{
#ifdef _WIN32
	return _wunlink(path.c_str());
#else
	return unlink(w2s(path).c_str());
#endif
}
