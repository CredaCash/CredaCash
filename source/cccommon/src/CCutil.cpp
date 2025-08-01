/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCutil.cpp
*/

#include "CCdef.h"
#include "CCutil.h"

mutex g_cerr_lock;
volatile bool g_cerr_needs_newline;

const char* g_hex_digits = "0123456789abcdef";

string s2hex(const string& str, char separator)
{
	return buf2hex(str.data(), str.length(), separator);
}

string buf2hex(const void *buf, unsigned nbytes, char separator)
{
	auto p = (const unsigned char *)buf;

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
	return wstring(str.begin(), str.end());
}

string w2s(const wstring& wstr)
{
	return string(wstr.begin(), wstr.end());
}

const char* yesno(int val)
{
	return val ? "yes" : "no";
}

const char* truefalse(int val)
{
	return val ? "true" : "false";
}

const char* plusminus(bool plus)
{
	return plus ? "+" : "-";
}

const string& stringorempty(const string& str)
{
	static const string empty("\"\"");

	return str.empty() ? empty : str;
}

void check_cerr_newline()
{
	if (g_cerr_needs_newline)
	{
		cerr << endl;
		g_cerr_needs_newline = false;
	}
}

// convert null or EOL terminated string to an unsigned or uint64_t
// returns -1 if not a valid unsigned or uint64_t

unsigned buf2uint(const void* bufp)
{
	auto v = buf2uint64(bufp);

	auto u = (unsigned)v;

	if (u != v)
		return -1;

	return u;
}

uint64_t buf2uint64(const void* bufp)
{
	const uint8_t* p = (uint8_t*)bufp;

	if (!p)
		return -1;

	bool started = false;
	bool done = false;

	uint64_t v = 0;

	while (true)
	{
		auto c = *p++;

		if (c == 0 || c == '\n' || c == '\r')
		{
			if (!started)
				return -1;

			return v;
		}

		if (c == ' ')
		{
			if (started)
				done = true;

			continue;
		}

		if (done)
			return -1;

		started = true;

		if (c < '0' || c > '9')
			return -1;

		auto n = v * 10 + c - '0';

		if (n < v)
			return -1;	// overflow

		v = n;
	}
}

void test_buf2int()
{
	const char* buf;
	buf = "0"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 0"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = "0 "; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = "1"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 2"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = "3 "; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 4 "; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = "1 2"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 1 2"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 1 2 "; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 123/"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 123: "; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " /123"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " :123 "; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 12/3"; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
	buf = " 12:3 "; cerr << "buf2uint(" << buf << ") = " << buf2uint(buf) << endl;
}

void copy_to_bufl(unsigned line, const void* data, const size_t nbytes, uint32_t &bufpos, void *buffer, const uint32_t bufsize, const bool bhex)
{
	auto buf = (char*)buffer;

	if (!bhex)
	{
		//cerr << "copy_to_buf buffer line " << line << " bufsize " << bufsize << " bufpos " << bufpos << " nbytes " << nbytes << endl;

		auto pos = bufpos;
		bufpos += nbytes;

		if (bufpos <= bufsize)
		{
			memcpy(buf + pos, data, nbytes);
			//cerr << "copy_to_buf " << buf2hex(data, nbytes) << endl;
		}
	}
	else for (unsigned i = 0; i < nbytes; ++i)
	{
		unsigned byte = *((const char *)data + i);

		copy_to_bufl(line, &g_hex_digits[byte & 15], 1, bufpos, buf, bufsize);
		copy_to_bufl(line, &g_hex_digits[(byte >> 4) & 15], 1, bufpos, buf, bufsize);
		if (bufpos < bufsize)
			buf[bufpos] = 0;
	}
}

void copy_from_bufl(unsigned line, void* data, const size_t datasize, const size_t nbytes, uint32_t &bufpos, const void *buffer, const uint32_t bufsize, const bool bhex)
{
	if (bhex)
	{
		bufpos = -1;
		return;		// not supported
	}

	//cerr << "copy_from_buf buffer line " << line << " bufsize " << bufsize << " bufpos " << bufpos << " nbytes " << nbytes << endl;

	auto buf = (const char *)buffer;
	auto pos = bufpos;
	bufpos += nbytes;

	if (bufpos <= bufsize)
	{
		memcpy(data, buf + pos, nbytes);
		memset((char*)data + nbytes, 0, datasize - nbytes);
		//cerr << "copy_from_buf " << buf2hex(data, nbytes) << endl;
	}
	else
		memset((char*)data, 0, datasize);
}
