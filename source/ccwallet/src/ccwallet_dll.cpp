/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * ccwallet_dll.cpp
*/

#ifdef __ANDROID__

#include <jni.h>
#include <android/log.h>

#endif // __ANDROID__

#ifdef __ANDROID__
#define CCWALLET_EXPORT extern "C" JNIEXPORT JNICALL
#else
#define CCWALLET_EXPORT extern "C" __stdcall __declspec(dllexport)
#endif

#include "ccwallet.h"

#include <inttypes.h>

#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/make_shared.hpp>

static int log_port;
static string log_password;

struct ccwallet_dll_log_sink : public boost::log::sinks::basic_sink_backend<boost::log::sinks::concurrent_feeding>
{
	void consume(const boost::log::record_view& rec)
	{
		auto timestamp = unixtime();

		static mutex log_mutex;
		lock_guard<mutex> lock(log_mutex);

		auto log_sev = rec[severity].get();
		auto log_msg = rec[boost::log::expressions::smessage].get();

		auto log_module = "ccwallet";
		//if (rec[module]) log_module = rec[module].get();

		const char* severity_string[] = {"trace", "debug", "info", "warn", "error", "fatal", "unknown"};
		const int max_severity = sizeof(severity_string)/sizeof(char*) - 1;

		unsigned severity_index = log_sev;
		//severity_index = rand() & 7;
		if (severity_index > max_severity)
			severity_index = max_severity;
		//cerr << "ccwallet_dll_log_sink severity_index " << severity_index << " max_severity " << max_severity << endl;

		auto max_len = log_msg.length() + 80;
		auto str = (char*)alloca(max_len);
		auto len = snprintf(str, max_len, "(%s %" PRIu64 ") %s\n", severity_string[severity_index], timestamp, log_msg.c_str());

		#ifdef __ANDROID__
		__android_log_write(log_sev + ANDROID_LOG_VERBOSE, log_module, log_msg.c_str());
		#else
		//cerr << str;
		(void)log_module;
		#endif

		#if 0 // for testing -- output log msg to file
		static int file = -1;
		if (file == -1)
		{
			file = open("ccwallet_console.out", O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
			cerr << "ccwallet_dll_log_sink open returned " << file << " errno " << errno << endl;
		}
		write(file, str, len);
		#endif

		static int log_socket = -1;
		if (log_socket == -1)
		{
			log_socket = socket(AF_INET, SOCK_STREAM, 0);
			cerr << "ccwallet_dll_log_sink log_socket " << log_socket << " errno " << errno << endl;
			int intval = 1;
			//auto rc = setsockopt(log_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&intval, sizeof(intval));
			//if (rc) cerr << "ccwallet_dll_log_sink setsockopt TCP_NODELAY returned " << rc << " errno " << errno << endl;
			auto rc = setsockopt(log_socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&intval, sizeof(intval));
			if (rc) cerr << "ccwallet_dll_log_sink setsockopt SO_KEEPALIVE returned " << rc << " errno " << errno << endl;
			intval = 32*1024;
			rc = setsockopt(log_socket, SOL_SOCKET, SO_SNDBUF, (char*)&intval, sizeof(intval));
			if (rc) cerr << "ccwallet_dll_log_sink setsockopt SO_SNDBUF returned " << rc << " errno " << errno << endl;
	
			struct sockaddr_in addr;
			memset(&addr, 0, sizeof(addr));
			addr.sin_addr.s_addr = inet_addr("127.0.0.1");
			addr.sin_family = AF_INET;
			addr.sin_port = htons(log_port);
			rc = connect(log_socket, (sockaddr*)&addr, sizeof(addr));
			if (rc) cerr << "ccwallet_dll_log_sink connect returned " << rc << " errno " << errno << endl;
			rc = send(log_socket, log_password.c_str(), log_password.length(), 0);
			if (rc != (int)log_password.length())
				cerr << "ccwallet_dll_log_sink send password returned " << rc << " errno " << errno << endl;
		}

		auto n = send(log_socket, str, len, 0);
		if (n != len)
		{
			cerr << "ccwallet_dll_log_sink send returned " << n << " expected " << len << " errno " << errno << endl;
			close(log_socket);
			log_socket = -1;
		}
	}
};

CCWALLET_EXPORT void ccwallet_logging_start(int port, const char *password)
{
	static mutex run_mutex;
	lock_guard<mutex> lock(run_mutex);

	cerr << "ccwallet_logging_start port " << port << " password " << password << endl;

	log_port = port;
	log_password = password;

	static bool need_sink = true;
	if (need_sink)
	{
		need_sink = false;
	    boost::log::core::get()->add_sink(boost::make_shared<boost::log::sinks::synchronous_sink<ccwallet_dll_log_sink>>());
	}
}

static atomic<int> now_serving(0);
static volatile int status = 0;

void ccwallet_server_gate(vector<string> &args, int my_number)
{
	//cerr << (intptr_t)&args << " " << buf2hex(&args, 10) << endl;

	int argc = args.size();

	BOOST_LOG_TRIVIAL(info) << "ccwallet_server_gate argc " << argc << " my_number " << my_number;

	if (my_number != now_serving)
		return;

	static mutex run_mutex;

	// shutdown server if running

	if (status > 0) BOOST_LOG_TRIVIAL(info) << "ccwallet_server stopping...";

	while (status > 0)
	{
		start_shutdown();

		sleep(1);
	}

	lock_guard<mutex> lock(run_mutex);

	if (my_number != now_serving)
		return;

	status = my_number; // status > 0 means running

	BOOST_LOG_TRIVIAL(info) << "ccwallet_server_gate status " << status;

	const char *argv[argc];

	for (int i = 0; i < argc; ++i)
	{
		argv[i] = args[i].c_str();
		//BOOST_LOG_TRIVIAL(info) << "ccwallet_server_gate " << argc << " " << i << " " << argv[i];
	}

	// start server

	BOOST_LOG_TRIVIAL(info) << "ccwallet_server starting...";

	int main(int argc, const char **argv);
	if (argc) main(argc, argv);

	BOOST_LOG_TRIVIAL(warning) << "ccwallet_server stopped.";

	// free the allocated args

	delete &args;

	if (!argc) cc_malloc_logging(false);

	status = -my_number; // status <= 0 means stopped

	BOOST_LOG_TRIVIAL(info) << "ccwallet_server_gate status " << status;
}

// call with argc > 0 to asynchonously start ccwallet
// call with argc = 0 to synchonously stop ccwallet

CCWALLET_EXPORT int ccwallet_server_start_stop(int argc, const char **argv)
{
	g_is_dll = true; // set ccwallet DLL mode

	//init_console();

	cc_malloc_logging(true);

	// take a number so the most recent request controls
	auto my_number = now_serving.fetch_add(1) + 1;

	cerr << "ccwallet_server_start_stop argc " << argc << " my_number " << my_number << endl;

	// copy the args to allocated memory

	vector<string> &args = *(new vector<string>(argc));

	for (int i = 0; i < argc; ++i)
	{
		//BOOST_LOG_TRIVIAL(info) << "ccwallet_server_start_stop " << argc << " " << i << " " << argv[i];
		args[i] = argv[i];
	}

	//cerr << (intptr_t)&args << " " << buf2hex(&args, 10) << endl;

	if (!argc)
		ccwallet_server_gate(args, my_number);
	else
	{
		std::thread t(ccwallet_server_gate, ref(args), my_number);
		t.detach();
	}

	return my_number;
}

CCWALLET_EXPORT int ccwallet_server_status()
{
	//BOOST_LOG_TRIVIAL(info) << "ccwallet_server_status " << status;

	return status;
}
