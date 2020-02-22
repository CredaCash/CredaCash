/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * CCutil.cpp
*/

#include "CCdef.h"
#include "CCutil.h"

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

// convert null terminated string to an unsigned integer
// returns -1 if not a valid unsigned integer

unsigned buf2int(const void* bufp)
{
	const uint8_t* p = (uint8_t*)bufp;

	if (!p)
		return -1;

	bool prefix = true;
	bool postfix = false;

	unsigned v = 0;

	while (*p)
	{
		if (*p == ' ')
		{
			if (!prefix)
				postfix = true;
		}
		else
		{
			if (postfix)
				return -1;

			prefix = false;

			if (*p < '0' || *p > '9')
				return -1;

			if (v >= UINT_MAX/10 - 9)
				return -1;

			v = v * 10 + *p - '0';
		}

		++p;
	}

	if (prefix)
		return -1;

	return v;
}

void test_buf2int()
{
	const char* buf;
	buf = "0"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 0"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = "0 "; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = "1"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 2"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = "3 "; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 4 "; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = "1 2"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 1 2"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 1 2 "; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 123/"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 123: "; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " /123"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " :123 "; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 12/3"; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
	buf = " 12:3 "; cerr << "buf2int(" << buf << ") = " << buf2int(buf) << endl;
}

void copy_to_bufl(unsigned line, const void* data, const size_t nbytes, uint32_t& bufpos, void *buffer, const uint32_t bufsize, const bool bhex)
{
	auto buf = (char*)buffer;

	if (bufpos + nbytes + bhex > bufsize)
	{
		//cerr << "copy_to_buf buffer overflow error line " << line << " bufsize " << bufsize << " bufpos " << bufpos << " nbytes " << nbytes << endl;

		bufpos = bufsize + 1;
		return;
	}

	if (!bhex)
	{
		//cerr << "copy_to_buf line " << line /* << " bufsize " << bufsize */ << " bufpos " << bufpos << " nbytes " << nbytes << " data " << buf2hex(data, nbytes) << endl;

		memcpy(buf + bufpos, data, nbytes);
		bufpos += nbytes;
	}
	else for (unsigned i = 0; i < nbytes; ++i)
	{
		unsigned byte = *((const char *)data + i);

		copy_to_bufl(line, &g_hex_digits[byte & 15], 1, bufpos, buf, bufsize);
		copy_to_bufl(line, &g_hex_digits[(byte >> 4) & 15], 1, bufpos, buf, bufsize);
		buf[bufpos] = 0;
	}
}

void copy_from_bufl(unsigned line, void* data, const size_t datasize, const size_t nbytes, uint32_t& bufpos, const void *buffer, const uint32_t bufsize, const bool bhex)
{
	if (bhex)
	{
		bufpos = -1;
		return;		// not supported
	}

	auto buf = (const char *)buffer;

	if (bufpos + nbytes + bhex > bufsize)
	{
		//cerr << "copy_from_buf buffer overflow error line " << line << " bufsize " << bufsize << " bufpos " << bufpos << " nbytes " << nbytes << endl;

		bufpos = bufsize + 1;
		return;
	}

	memcpy(data, buf + bufpos, nbytes);

	if (datasize > nbytes)
		memset((char*)data + nbytes, 0, datasize - nbytes);

	//cerr << "copy_from_buf line " << line /* << " bufsize " << bufsize */ << " bufpos " << bufpos << " nbytes " << nbytes << " data " << buf2hex(data, nbytes) << endl;

	bufpos += nbytes;
}
