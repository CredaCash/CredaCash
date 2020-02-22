/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * encode.cpp
*/

#include "cclib.h"
#include "encode.h"

#define TRACE	0

void base64_test(const unsigned char* encode_table, const unsigned char* decode_table, bool no_padding)
{
	for (unsigned i = 0; i < 2000; ++i)
	{
		unsigned len = (rand() & 31) + 1;
		string s;

		for (unsigned j = 0; j < len; ++j)
		{
			char c = 0;

			while (c < 32)
				c = rand() & 255;

			s.push_back(c);
		}

		cerr << s << endl;

		string e;
		base64_encode(encode_table, s, e, no_padding);

		cerr << e << endl;
	}
}

void base64_encode(const unsigned char* table, const string& sval, string& outs, bool no_padding)
{
	outs.clear();

	for (unsigned i = 0; i < sval.length(); i += 3)
	{
		unsigned group = 0;
		int nbits = 0;

		for (unsigned j = 0; j < 3; ++j)
		{
			group <<= 8;
			if (i + j < sval.length())
			{
				group |= sval[i + j];
				nbits += 8;
			}
		}

		for (unsigned j = 0; nbits > 0; ++j)
		{
			outs.push_back(table[(group >> ((3-j) * 6)) & 63]);
			nbits -= 6;
		}
	}

	while (!no_padding && (outs.length() & 3))
		outs.push_back('=');
}

unsigned decode_char(const unsigned char* table, const unsigned mod, unsigned char c)
{
	//cerr << "decode_char " << c << " " << table[0] << " " << table[1] << endl;
	//cerr << "decode_char " << (int)c << " " << (int)table[0] << " " << (int)table[1] << endl;

	if (c < table[0] || c > table[1])
		return -1;

	unsigned i = table[c - table[0] + 2];

	if (TRACE) cerr << "decode_char " << c << " " << table[0] << " " << table[1] << endl;
	if (TRACE) cerr << "decode_char " << (int)c << " " << (int)table[0] << " " << (int)table[1] << endl;
	if (TRACE) cerr << "decode_char " << i << " " << mod << endl;

	if (i >= mod)
		return -1;

	return i;
}
