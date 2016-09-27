/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * jsonutil.cpp
*/

#include "CCdef.h"
#include "jsonutil.h"
#include "CCproof.h"

CCRESULT copy_error_to_output(const string& error, char *output, const uint32_t bufsize, CCRESULT rc)
{
	size_t eol = error.copy(output, bufsize - 1);
	output[eol] = 0;

	return rc;
}

CCRESULT copy_result_to_output(const string& fn, const string& result, char *output, const uint32_t bufsize)
{
	if (result.size() < bufsize)
		return copy_error_to_output(result, output, bufsize, 0);
	else
		return copy_error_to_output(fn + " error: output buffer overflow (need " + to_string(result.size() + 1) + " bytes)", output, bufsize);
}

CCRESULT error_unexpected_key(const string& fn, const string& key, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: unexpected value \"" + key + "\"", output, bufsize);
}

CCRESULT error_missing_key(const string& fn, const string& key, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: missing required value \"" + key + "\"", output, bufsize);
}

CCRESULT error_value_overflow(const string& fn, const string& key, unsigned nbits, const bigint_t& maxval, char *output, const uint32_t bufsize)
{
	if (!nbits && (BIGWORD(maxval, 1) || BIGWORD(maxval, 2) || BIGWORD(maxval, 3)))
		return copy_error_to_output(fn + " error: value of \"" + key + "\" larger than prime modulus", output, bufsize);
	else if (nbits)
		return copy_error_to_output(fn + " error: value of \"" + key + "\" larger than " + to_string(nbits) + " bits", output, bufsize);
	else
		return copy_error_to_output(fn + " error: value of \"" + key + "\" larger than " + to_string(BIG64(maxval)), output, bufsize);
}

CCRESULT error_not_numeric(const string& fn, const string& key, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: non-numeric character in value of \"" + key + "\"", output, bufsize);
}

CCRESULT error_not_hex(const string& fn, const string& key, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: non-hex character in value of \"" + key + "\"", output, bufsize);
}

CCRESULT error_invalid_value(const string& fn, const string& key, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: invalid value for key \"" + key + "\"", output, bufsize);
}

CCRESULT error_input_end(const string& fn, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: input ends unexpectedly", output, bufsize);
}

CCRESULT error_invalid_char(const string& fn, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: invalid character", output, bufsize);
}

CCRESULT error_unexpected_char(const string& fn, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: unexpected character", output, bufsize);
}

CCRESULT error_checksum_mismatch(const string& fn, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: checksum mismatch", output, bufsize);
}

CCRESULT error_not_array(const string& fn, const string& key, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: the \"" + key + "\" element must contain an array of values", output, bufsize);
}

CCRESULT error_not_array_objs(const string& fn, const string& key, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: the \"" + key + "\" element must contain an array of objects", output, bufsize);
}

CCRESULT error_too_many_objs(const string& fn, const string& key, unsigned limit, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: the \"" + key + "\" element is limited to " + to_string(limit) + " objects", output, bufsize);
}

CCRESULT error_num_values(const string& fn, const string& key, unsigned limit, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + " error: the \"" + key + "\" element must contain " + to_string(limit) + " values", output, bufsize);
}

CCRESULT error_invalid_tx(const string& fn, char *output, const uint32_t bufsize)
{
	return copy_error_to_output(fn + ": unrecognized transaction type", output, bufsize);
}

const Json::Value * json_find(const Json::Value& root, const char *key)
{
	return root.find(key, key + strlen(key));
}

static CCRESULT parse_hex_value(const string& fn, const string& key, const string& sval, int start, int end, unsigned nbits, const bigint_t& maxval, bigint_t& val, char *output, const uint32_t bufsize)
{
	if (end < start)
		return error_not_hex(fn, key, output, bufsize);

	for (int i = start; i <= end; ++i)
	{
		auto c = sval[i];

		if ((c < '0' || c > '9') && (c < 'A' || c > 'F') && (c < 'a' || c > 'f'))
			return error_not_hex(fn, key, output, bufsize);

		if (c >= 'a')
			c -= 'a' - 10;
		else if (c >= 'A')
			c -= 'A' - 10;
		else
			c -= '0';

		bigint_t newval;
		mulBigInt(val, bigint_t(16UL), newval, false);	// don't want mod Prime so pubkey can be full 256 bits
		addBigInt(newval, bigint_t((unsigned long)(c)), newval, false);

		//cerr << "parse_hex_value val " << val << " newval " << newval << " maxval " << maxval << endl;

		if (maxval && (val > newval || newval > maxval))
			return error_value_overflow(fn, key, nbits, maxval, output, bufsize);

		val = newval;
	}

	return 0;
}

static CCRESULT parse_dec_value(const string& fn, const string& key, const string& sval, int start, int end, unsigned nbits, const bigint_t& maxval, bigint_t& val, char *output, const uint32_t bufsize)
{
	if (end < start)
		return error_not_numeric(fn, key, output, bufsize);

	for (int i = start; i <= end; ++i)
	{
		auto c = sval[i];

		if (c < '0' || c > '9')
			return error_not_numeric(fn, key, output, bufsize);

		bigint_t newval;
		mulBigInt(val, bigint_t(10UL), newval, false);	// don't want mod Prime so pubkey can be full 256 bits
		addBigInt(newval, bigint_t((unsigned long)(c - '0')), newval, false);

		//cerr << "parse_dec_value val " << val << " newval " << newval << " maxval " << maxval << endl;

		if (maxval && (val > newval || newval > maxval))
			return error_value_overflow(fn, key, nbits, maxval, output, bufsize);

		val = newval;
	}

	return 0;
}

static CCRESULT parse_signed_value(const string& fn, const string& key, const string& sval, unsigned nbits, bigint_t& maxval, bigint_t& val, char *output, const uint32_t bufsize, unsigned &negative)
{
	CCASSERT(!(nbits && maxval));	// both cannot be set

	val = 0UL;
	negative = 0;

	if (sval.empty())
		return error_not_numeric(fn, key, output, bufsize);

	auto prefix = sval.substr(0, 1);

	if (prefix == "-")
	{
		if (sval.length() < 2)
			return error_not_numeric(fn, key, output, bufsize);

		negative = 1;
		if (nbits)
			nbits--;
		if (maxval)
			maxval = maxval / 2;
	}

	if (nbits)
	{
		CCASSERT(!maxval);

		for (unsigned i = 0; i < nbits; ++i)
		{
			maxval = maxval * bigint_t(2UL) + bigint_t(1UL);
			//cerr << "i " << i << " maxval " << maxval << " " << BIGWORD(maxval, 3) << " " << BIGWORD(maxval, 2) << " " << BIGWORD(maxval, 1) << " " << BIGWORD(maxval, 0) << endl;
		}
	}

	//cerr << hex << "parse_signed_value nbits " << nbits << " maxval " << maxval << dec << endl;

	int end = sval.length();
	end--;
	if (sval[end] == 'L')
		end--;

	prefix = sval.substr(negative, 1);

	if (prefix == "x" || prefix == "X")
		return parse_hex_value(fn, key, sval, negative + 1, end, nbits, maxval, val, output, bufsize);

	if (sval.length() >= negative + 2)
		prefix = sval.substr(negative, 2);

	if (prefix == "0x" || prefix == "0X")
		return parse_hex_value(fn, key, sval, negative + 2, end, nbits, maxval, val, output, bufsize);

	return parse_dec_value(fn, key, sval, negative, end, nbits, maxval, val, output, bufsize);
}

CCRESULT parse_int_value(const string& fn, const string& key, const string& sval, unsigned nbits, bigint_t maxval, bigint_t& val, char *output, const uint32_t bufsize)
{
	unsigned negative;

	//cerr << hex << "parse_int_value nbits " << nbits << " maxval " << maxval << dec << endl;

	auto rc = parse_signed_value(fn, key, sval, nbits, maxval, val, output, bufsize, negative);
	if (rc)
		return rc;

	//cerr << hex << "parse_int_value negative " << negative << " val " << val << dec << endl;

	if (negative)
	{
		if (!nbits)
			val = bigint_t(0UL) - val;
		else
		{
			maxval = maxval * bigint_t(2UL) + bigint_t(2UL);

			val = maxval - val;
		}
	}

	//cerr << hex << "parse_int_value val " << val << " maxval " << maxval << dec << endl;

	return 0;
}

#if 0 // unused
// destroys val
static bigint_t bitreverse(bigint_t& val, const unsigned nbits)
{
	bigint_t v = 0UL;

	for (unsigned i = 0; i < nbits; ++i)
	{
		v = v * bigint_t(2UL);

		if (val.asUnsignedLong() & 1)
			v = v + bigint_t(1UL);

		val = val / 2;
	}

	return v;
}
#endif

// shifts out all least significant lower bits
// returns shift amount
static unsigned compute_shift(const unsigned mod, bigint_t& maxval, bigint_t& val)
{
	unsigned shift = 0;

	while (true)
	{
		if (val.asUnsignedLong() & 1)
			break;

		val = val / 2;
		maxval = maxval / 2;

		if (++shift == mod - 1)
			break;
	}

	//cerr << "compute_shift " << shift << endl;

	return shift;
}

void encode(const unsigned char* table, const unsigned mod, const bigint_t& maxval, bool normalize, int nchars, const bigint_t& val, string &outs)
{
	bigint_t v = val;

	//if (nbits)
	//	v = bitreverse(v, nbits);

	bigint_t mval = maxval;
	if (mval == 0UL)
		subBigInt(mval, bigint_t(1UL), mval, false);

	//cerr << "encode mval " << hex << mval << dec << endl;

	if (normalize)
	{
		auto shift = compute_shift(mod, mval, v);
		outs.push_back(table[shift]);
	}

	int nc = 0;
	while (mval)
	{
		outs.push_back(table[v % mod]);
		v = v / mod;
		mval = mval / mod;
		nc++;

		//cerr << "encode " << hex << val << " " << v << " " << mval << dec << " " << nc << " " << nchars << " " << outs << endl;

		if (nc == nchars || (nchars < 0 && v == 0UL))
			break;
	}
}

unsigned decode_char(const unsigned char* table, const unsigned mod, unsigned char c)
{
	//cerr << "decode_char " << c << " " << table[0] << " " << table[1] << endl;
	//cerr << "decode_char " << (int)c << " " << (int)table[0] << " " << (int)table[1] << endl;

	if (c < table[0] || c > table[1])
		return 255;

	unsigned i = table[c - table[0] + 2];

	if (0*i)
	{
		cerr << "decode_char " << c << " " << table[0] << " " << table[1] << endl;
		cerr << "decode_char " << (int)c << " " << (int)table[0] << " " << (int)table[1] << endl;
		cerr << "decode_char " << i << " " << mod << endl;
	}

	if (i >= mod)
		return 255;

	return i;
}

CCRESULT decode(const string& fn, const unsigned char* table, const unsigned mod, bool normalize, unsigned nchars, string& instring, bigint_t& val, char *output, const uint32_t bufsize)
{
	val = 0UL;
	unsigned shift = 0;

	if (normalize)
	{
		if (instring.length() < 1)
			return error_input_end(fn, output, bufsize);

		shift = decode_char(table, mod, instring[0]);
		if (shift == 255)
			return error_invalid_char(fn, output, bufsize);

		//cerr << "decode shift " << shift << endl;
	}

	if (!nchars)
	{
		nchars = instring.find(SEPARATOR);
		auto nchars2 = instring.find(SEPARATOR_ALT);
		if (nchars2 < nchars)
			nchars = nchars2;
		if (nchars >= instring.length())
			return error_input_end(fn, output, bufsize);

		nchars -= normalize;
	}

	//cerr << "decode " << nchars << " " << instring << endl;

	if (instring.length() < nchars + normalize)
		return error_input_end(fn, output, bufsize);

	string sval = instring.substr(normalize, nchars);
	instring = instring.substr(nchars + normalize);

	//cerr << "decode " << sval << endl;

	while (sval.size())
	{
		auto c = decode_char(table, mod, sval.back());
		sval.pop_back();

		if (c == 255)
			return error_invalid_char(fn, output, bufsize);

		mulBigInt(val, bigint_t((unsigned long)(mod)), val, false);
		addBigInt(val, bigint_t((unsigned long)(c)), val, false);
	}

	for (unsigned i = 0; i < shift; ++i)
		mulBigInt(val, bigint_t(2UL), val, false);

	return 0;
}

#if 0 // for future use
void encodestring(const string &sval, string &outs)
{

}
#endif



