/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * util.h
*/

#pragma once

typedef unsigned ccthreadid_t;

ccthreadid_t ccgetthreadid();
void ccsleep(int sec);
int init_globals();
int init_app_dir();
int create_directory(const wstring& path);
int open_file(const wstring& path, int flags, int mode = 0);
int delete_file(const wstring& path);
