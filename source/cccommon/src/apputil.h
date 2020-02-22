/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * apputil.h
*/

#pragma once

#include <string>

void set_trace_level(int level);
void expand_number(std::string& s, const uint64_t v);
void expand_number_wide(std::wstring& s, const uint64_t v);
std::wstring get_process_dir();
void get_app_data_dir(std::wstring& path, const std::string& appname);
void get_proof_key_dir(std::wstring& path, const std::wstring& appdir);
int create_directory(const std::wstring& path);
int open_file(const std::wstring& path, int flags, int mode = 0);
int delete_file(const std::wstring& path);
