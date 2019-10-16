/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * CCutil.h
*/

#pragma once

#include "SpinLock.hpp"

#include <string>
#include <cstdint>

extern FastSpinLock g_cout_lock;
extern const char* g_hex_digits;

// note: RandTest(0) = 0, else RandTest(x <= 1) = 1, else RandTest(x) = !(rand() % x)
#define RandTest(x) ((x) != 0 && ((x) <= 1 || !(rand() % ((x) > 1 ? (x) : 1))))

#define STRINGIFY_QUOTER(x) #x
#define STRINGIFY(x) STRINGIFY_QUOTER(x)

std::wstring s2w(const std::string& str);
std::string w2s(const std::wstring& wstr);
std::string s2hex(const std::string& str);
std::string buf2hex(const void *buf, unsigned nbytes, char separator = ' ');
const char* yesno(int val);
const char* truefalse(int val);
const char* plusminus(bool plus);
const std::string& stringorempty(const std::string& str);

unsigned buf2int(const void* bufp);

void copy_to_bufl(unsigned line, const void* data, const size_t nbytes, uint32_t& bufpos, void *buffer, const uint32_t bufsize, const bool bhex = false);
void copy_from_bufl(unsigned line, void* data, const size_t datasize, const size_t nbytes, uint32_t& bufpos, const void *buffer, const uint32_t bufsize, const bool bhex = false);

#define copy_to_bufp(...)       copy_to_bufl(__LINE__, __VA_ARGS__)
#define copy_to_buf(a, ...)     copy_to_bufl(__LINE__, &(a), __VA_ARGS__)
#define copy_from_buf(a, ...) copy_from_bufl(__LINE__, &(a), sizeof(a), __VA_ARGS__)
