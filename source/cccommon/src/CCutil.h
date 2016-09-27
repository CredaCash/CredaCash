/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCutil.h
*/

#pragma once

#include "SpinLock.hpp"

#include <string>
#include <cstdint>

extern FastSpinLock g_cout_lock;
extern const char* g_hex_digits;

#define STRINGIFY_QUOTER(x) #x
#define STRINGIFY(x) STRINGIFY_QUOTER(x)

std::wstring s2w(const std::string& str);
std::string w2s(const std::wstring& wstr);
std::string s2hex(const std::string& str);
std::string buf2hex(const void *buf, unsigned nbytes, char separator = ' ');
const char* yesno(int val);
const std::string& stringorempty(const std::string& str);

unsigned buf2int(const std::uint8_t*& bufp);

void copy_to_buf(const void* data, const size_t nbytes, uint32_t& bufpos, void *output, const uint32_t bufsize, const bool bhex = false);
void copy_from_buf(void* data, const size_t nbytes, uint32_t& bufpos, const void *input, const uint32_t bufsize, const bool bhex = false);
