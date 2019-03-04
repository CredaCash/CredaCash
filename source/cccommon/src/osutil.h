/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * osutil.h
*/

#pragma once

#include <string>
#include <boost/program_options/variables_map.hpp>

void set_trace_level(int level);
void set_handlers();
void finish_handlers();
void start_shutdown();
void wait_for_shutdown(unsigned millisec = -1);
void ccsleep(int sec);
std::wstring get_process_dir();
std::wstring get_app_data_dir(const boost::program_options::variables_map& config_options, const std::wstring& appname);
int create_directory(const std::wstring& path);
int open_file(const std::wstring& path, int flags, int mode = 0);
int delete_file(const std::wstring& path);
