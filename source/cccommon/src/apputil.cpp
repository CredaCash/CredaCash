/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * apputil.cpp
*/

#include "CCdef.h"
#include "CCboost.hpp"
#include "apputil.h"

void set_trace_level(int level)
{
	boost::log::core::get()->set_filter(boost::log::trivial::severity > (((int)(fatal)) - level));
}

void expand_number(string& s, const uint64_t v)
{
	auto pos = s.find('#');
	if (pos == string::npos)
		return;

	s.replace(pos, 1, to_string(v));
}

void expand_number_wide(wstring& s, const uint64_t v)
{
	auto pos = s.find('#');
	if (pos == string::npos)
		return;

	s.replace(pos, 1, to_wstring(v));
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

static void get_app_data_dir_static(wstring& path, const string& appname)
{
	if (path.empty())
	{

#ifdef _WIN32

		path.resize(MAX_PATH);
		if (S_OK != SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, &path[0]))
			return;

		path.resize(wcslen(path.c_str()));
		path += WIDE(PATH_DELIMITER);

#else

		auto home = getenv("HOME");
		if (!home)
			return;

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
		}

		path += WIDE(PATH_DELIMITER);
		path += s2w(appname);
	}

	if (create_directory(path))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path);
		exit(-1);
		throw exception();
	}

	wstring path2 = path + s2w(TOR_SUBDIR);
	if (create_directory(path2))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path2);
		exit(-1);
		throw exception();
	}

	path2 = path + s2w(TOR_HOSTNAMES_SUBDIR);
	if (create_directory(path2))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path2);
		exit(-1);
		throw exception();
	}
}

void get_app_data_dir(wstring& path, const string& appname)
{
	get_app_data_dir_static(path, appname);

	if (path.empty())
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to get application data directory";
		exit(-1);
		throw exception();
	}

	BOOST_LOG_TRIVIAL(debug) << "application data directory = " << w2s(path);
}

void get_proof_key_dir(wstring& path, const wstring& appdir)
{
	if (path.empty())
	{
		path = appdir;
		path += WIDE(PATH_DELIMITER);
		path += WIDE(ZK_KEY_DIR);
	}

	BOOST_LOG_TRIVIAL(debug) << "zero knowledge proof key directory = " << w2s(path);

	if (path == L"env")
		path.clear();	// SetKeyFilePath() will look up the environment variable
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
