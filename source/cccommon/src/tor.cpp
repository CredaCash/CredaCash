/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * tor.cpp
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "ccserver/torservice.hpp"
#include "tor.h"
#include "apputil.h"
#include "osutil.h"

static const wchar_t space_char = 0x0003;
static const wstring space = L"\x0003";

static void tor_hidden_service_config(bool external_tor, wostringstream& params, wstring& service_port_list, const TorService& service)
{
	if (service.enabled && service.tor_service)
	{
		wstring dir = service.app_data_dir + s2w(service.tor_hostname_subdir);

		if (service.tor_new_hostname && !external_tor)
		{
			delete_file(dir + WIDE(PATH_DELIMITER) + L"private_key");
			delete_file(dir + WIDE(PATH_DELIMITER) + L"hs_ed25519_secret_key");
			delete_file(dir + WIDE(PATH_DELIMITER) + L"hs_ed25519_public_key");
			delete_file(dir + WIDE(PATH_DELIMITER) + L"hostname");
		}

		params << space << "+HiddenServiceDir" << space << "\"" << dir << "\"";
		params << space << "HiddenServicePort" << space << "\"443 " << service.port << "\"";
		params << space << "HiddenServiceVersion" << space << "3";
		if (service.tor_auth == 1)
			params << space << "HiddenServiceAuthorizeClient" << space << "basic";

		service_port_list += L" ";
		service_port_list += to_wstring(service.port);
	}
}

void tor_start(const wstring& process_dir, const wstring& tor_exe, const int tor_port, const wstring& tor_config, const wstring& app_data_dir, bool need_outgoing, vector<TorService*>& services, unsigned tor_control_service_index)
{
#ifndef __ANDROID__
	bool external_tor = (tor_exe == L"external");
	bool need_tor = need_outgoing;
	wostringstream params;
	wstring service_port_list;
	if (external_tor)
		params << "tor";
	else
		params << "\"" << tor_exe << "\"";
	if (tor_config.length() && tor_config.compare(L"."))
		params << space << "-f" << space << "\"" << tor_config << "\"";
	params << space << "DataDirectory" << space << "\"" << app_data_dir << TOR_SUBDIR << "\"";
	params << space << "+SOCKSPort" << space << tor_port;

	if (tor_control_service_index < services.size() && services[tor_control_service_index]->enabled)
	{
		need_tor = true;
		params << space << "+ControlPort" << space << services[tor_control_service_index]->port;
		params << space << "HashedControlPassword" << space << "16:" << s2w(services[tor_control_service_index]->password_string);
	}

	if (!need_tor)
	{
		BOOST_LOG_TRIVIAL(info) << "No Tor services or outgoing proxy needed";
		return;
	}

	for (auto service : services)
		tor_hidden_service_config(external_tor, params, service_port_list, *service);

	params << space << "+LongLivedPorts" << space << "\"443" << service_port_list << "\"";

	auto paramline = params.str();

	auto paramline_text = paramline;
	for (unsigned i = 0; i < paramline_text.length(); ++i)
	{
		if (paramline_text[i] == space_char)
			paramline_text[i] = ' ';
	}

	if (external_tor)
	{
		BOOST_LOG_TRIVIAL(info) << "Skipping Tor launch; Tor must be launched and managed externally";

		BOOST_LOG_TRIVIAL(info) << "Tor command line: " << w2s(paramline_text);

		return;
	}

	BOOST_LOG_TRIVIAL(info) << "Tor command line: " << w2s(paramline_text);

#ifdef _WIN32
	PROCESS_INFORMATION pi;
#else
	pid_t pid;
#endif
	bool tor_running = false;

	while (!g_shutdown)
	{

#ifdef _WIN32
		STARTUPINFOW si;
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);
		si.dwFlags |= STARTF_USESTDHANDLES;
		si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		si.hStdError = si.hStdOutput;
		si.hStdInput = si.hStdOutput;

		if (! CreateProcessW(tor_exe.c_str(), &paramline_text[0], NULL, NULL, TRUE,
				CREATE_NO_WINDOW | DEBUG_PROCESS, NULL, process_dir.c_str(), &si, &pi))
		{
			BOOST_LOG_TRIVIAL(error) << "Unable to start Tor; error = " << GetLastError();

			{
				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
				cerr << "ERROR: Unable to start Tor" << endl;
			}

			ccsleep(20);
			continue;
		}

		tor_running = true;
		BOOST_LOG_TRIVIAL(info) << "Tor started";

		// Tor processes was created with DEBUG_PROCESS so that it will exit if this process exits
		// this loop (run in a separate thread) handles and ignores all debug events coming from the subprocess

		while (!g_shutdown)
		{
			DEBUG_EVENT de;

			if (WaitForDebugEvent(&de, 1000))
			{
				ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_EXCEPTION_NOT_HANDLED);

				if (de.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && de.u.CreateProcessInfo.hFile)
					CloseHandle(de.u.CreateProcessInfo.hFile);

				if (de.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && de.u.LoadDll.hFile)
					CloseHandle(de.u.LoadDll.hFile);

				if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT && de.dwProcessId == pi.dwProcessId && !g_shutdown)
				{
					CloseHandle(pi.hProcess);
					CloseHandle(pi.hThread);

					tor_running = false;
					BOOST_LOG_TRIVIAL(error) << "Tor process exited unexpectedly--restart will be attempted in 10 seconds...";

					ccsleep(10);

					//BOOST_LOG_TRIVIAL(error) << "Attempting to restart Tor...";
					break;
				}
			}
			else if (GetLastError() != ERROR_SEM_TIMEOUT)
			{
				BOOST_LOG_TRIVIAL(debug) << "WaitForDebugEvent error " << GetLastError();
				ccsleep(2);
			}
		}
#else

		#define MAX_TOR_ARGS 256
		const char *argv[MAX_TOR_ARGS];
		memset(argv, 0, sizeof(argv));

		auto sparamline = w2s(paramline);
		argv[0] = &sparamline[1];
		unsigned nparams = 1;

		if (sparamline[sparamline.length()-1] == '"')
			sparamline[sparamline.length()-1] = 0;	// delete the trailing double-quote character

		for (unsigned i = 0; i < sparamline.length(); ++i)
		{
			if ((sparamline[i] & 0xFF) == (space_char & 0xFF))
			{
				sparamline[i] = 0;
				if (sparamline[i-1] == '"')
					sparamline[i-1] = 0;			// delete the trailing double-quote character
				argv[nparams++] = &sparamline[i+1];
				CCASSERT(nparams < MAX_TOR_ARGS);
			}
		}

		argv[nparams] = 0;

		for (unsigned i = 0; argv[i]; ++i)
		{
			if (*argv[i] == '"')
				++argv[i];							// skip the leading double-quote character

			//cout << "Tor arg " << i << " = " << argv[i] << endl;
		}

		auto rc = posix_spawnp(&pid,
				w2s(tor_exe).c_str(),
				NULL,							// const posix_spawn_file_actions_t *file_actions
				NULL,							// const posix_spawnattr_t *restrict attrp
				(char* const*)argv,				// char *const argv[restrict]
				environ);						// char *const envp[restrict]
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Unable to start Tor; error = " << rc;

			{
				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
				cerr << "ERROR: Unable to start Tor" << endl;
			}

			ccsleep(20);
			continue;
		}

		tor_running = true;
		BOOST_LOG_TRIVIAL(info) << "Tor started";

		while (true)
		{
			if (ccsleep(20))
				break;

			auto rc = waitpid(pid, NULL, WNOHANG);
			if (rc > 0)
			{
				tor_running = false;
				BOOST_LOG_TRIVIAL(error) << "Tor process exited unexpectedly--attempting to restart";

				break;
			}
		}
#endif
	}

	//ccsleep(5);	// for testing

	if (tor_running)
	{
		BOOST_LOG_TRIVIAL(info) << "Terminating Tor process";

#ifdef _WIN32
		TerminateProcess(pi.hProcess, 0);
#else
		kill(pid, SIGTERM);
#endif
	}

	BOOST_LOG_TRIVIAL(info) << "Tor monitor thread ending";
#endif // #ifndef __ANDROID__
}
