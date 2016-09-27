/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * tor.cpp
*/

#include "CCdef.h"
#include "tor.h"
#include "service_base.hpp"
#include "transact.hpp"
#include "relay.hpp"
#include "blockserve.hpp"
#include "util.h"

static void tor_hidden_service_config(wostringstream& params, wstring& service_port_list, const ServiceBase& service)
{
	if (service.enabled && service.tor_service)
	{
		wstring dir = g_params.app_data_dir + s2w(service.tor_hostname_subdir);

		if (service.tor_new_hostname)
		{
			delete_file(dir + WIDE(PATH_DELIMITER) + L"private_key");
			delete_file(dir + WIDE(PATH_DELIMITER) + L"hostname");
		}

		params << " -+HiddenServiceDir " << dir;
		params << " -+HiddenServicePort \"443 " << service.port << "\"";

		service_port_list += L" ";
		service_port_list += to_wstring(service.port);
	}
}

void tor_start()
{
	wostringstream params;
	wstring service_port_list;
	params << "\"" << g_params.tor_exe << "\"";
	if (!g_params.tor_config.length() || g_params.tor_config.compare(L"."))
		params << " -f \"" << g_params.tor_config << "\"";
	params << " -DataDirectory \"" << g_params.app_data_dir << TOR_SUBDIR << "\"";
	params << " -+SOCKSPort " << g_params.torproxy_port;
	if (g_tor_control_service.enabled)
		params << " -+ControlPort " << g_tor_control_service.port << " -+HashedControlPassword 16:" << s2w(g_tor_control_service.password_string);
	tor_hidden_service_config(params, service_port_list, g_transact_service);
	tor_hidden_service_config(params, service_port_list, g_relay_service);
	tor_hidden_service_config(params, service_port_list, g_privrelay_service);
	tor_hidden_service_config(params, service_port_list, g_blockserve_service);
	tor_hidden_service_config(params, service_port_list, g_control_service);
	tor_hidden_service_config(params, service_port_list, g_tor_control_service);
	params << " -+LongLivedPorts \"443" << service_port_list << "\"";
	params << "\0";

	BOOST_LOG_TRIVIAL(info) << "Tor command line: " << w2s(params.str());

#ifdef _WIN32
	PROCESS_INFORMATION pi;
#endif

	while (!g_shutdown)
	{

#ifdef _WIN32
		STARTUPINFOW si;
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);

		if (! CreateProcessW(g_params.tor_exe.c_str(), &params.str()[0], NULL, NULL, FALSE,
				CREATE_NO_WINDOW | DEBUG_PROCESS, NULL, g_params.process_dir.c_str(), &si, &pi))
		{
			BOOST_LOG_TRIVIAL(error) << "Unable to start tor; error = " << GetLastError();
			ccsleep(20);
			continue;
		}

		BOOST_LOG_TRIVIAL(info) << "Tor started";

		// tor processes was created with DEBUG_PROCESS so that it will exit if this process exits
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

#endif

	}

	BOOST_LOG_TRIVIAL(info) << "Terminating Tor process";

#ifdef _WIN32
	TerminateProcess(pi.hProcess, 0);
#endif

	BOOST_LOG_TRIVIAL(info) << "Tor monitor thread ending";

}



