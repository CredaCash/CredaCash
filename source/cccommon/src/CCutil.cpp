/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCutil.cpp
*/

#include "CCutil.h"

#include <limits>

using namespace std;

FastSpinLock g_cout_lock;
const char* g_hex_digits = "0123456789abcdef";

string s2hex(const string& str)
{
	string o;
	for (auto c : str)
	{
		o += g_hex_digits[(c >> 4) & 15];
		o += g_hex_digits[c & 15];
		o += ' ';
	}

	return o;
}

string buf2hex(const void *buf, unsigned nbytes, char separator)
{
	auto p = (unsigned char *)buf;

	string o;
	for (unsigned i = 0; i < nbytes; ++i, ++p)
	{
		if (i && separator)
			o += separator;
		o += g_hex_digits[(*p >> 4) & 15];
		o += g_hex_digits[*p & 15];
	}

	return o;
}

wstring s2w(const string& str)
{
	return wstring(str.begin(),  str.end());
}

string w2s(const wstring& wstr)
{
	return string(wstr.begin(),  wstr.end());
}

const char* yesno(int val)
{
	return val ? "yes" : "no";
}

const string& stringorempty(const string& str)
{
	static const string empty("\"\"");

	return str.empty() ? empty : str;
}

// convert null terminated string to an integer
// returns -1 if not a valid integer
// string pointer is updated to point to next char after null terminator

unsigned buf2int(const uint8_t*& bufp)
{
	if (!bufp || !*bufp)
		return -1;

	unsigned v = 0;

	while (*bufp)
	{
		if (*bufp < '0' || *bufp > '9')
			return -1;

		if (v >= UINT_MAX/10 - 9)
			return -1;

		v = v * 10 + *bufp - '0';

		++bufp;
	}

	++bufp;

	return v;
}

void copy_to_buf(const void* data, const size_t nbytes, uint32_t& bufpos, void *output, const uint32_t bufsize, const bool bhex)
{
	auto buf = (char *)output;

	if (bufpos + nbytes + bhex > bufsize)
	{
		bufpos = bufsize + 1;
		return;
	}

	if (!bhex)
	{
		memcpy(buf + bufpos, data, nbytes);
		bufpos += nbytes;
	}
	else for (unsigned i = 0; i < nbytes; ++i)
	{
		unsigned byte = *((const char *)data + i);

		copy_to_buf(&g_hex_digits[byte & 15], 1, bufpos, buf, bufsize);
		copy_to_buf(&g_hex_digits[(byte >> 4) & 15], 1, bufpos, buf, bufsize);
		buf[bufpos] = 0;
	}
}

void copy_from_buf(void* data, const size_t nbytes, uint32_t& bufpos, const void *input, const uint32_t bufsize, const bool bhex)
{
	if (bhex)
	{
		bufpos = -1;
		return;		// not supported
	}

	auto buf = (const char *)input;

	if (bufpos + nbytes + bhex > bufsize)
	{
		bufpos = bufsize + 1;
		return;
	}

	memcpy(data, buf + bufpos, nbytes);
	bufpos += nbytes;
}
