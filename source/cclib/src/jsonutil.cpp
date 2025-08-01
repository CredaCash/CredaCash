/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * jsonutil.cpp
*/

/*
	Valid Number Format
	-------------------

	1. Optional "-" which indicates a negative number.
		Note that a leading "+" sign is not currently accepted, although this easily could be changed.
	2. Optional "0x", "0X", "x" or "X", which indicates hex format.
	For hex format:
		3. A sequence of one or more [0-9], [a-f] or [A-F] characters.
		4. Optional: "L" terminating character, which is does not change the value.
	For non-hex format:
		3. A sequence of one or more [0-9] characters, optionally with a single "." decimal point anywhere in that sequence.
			Note that unlike numeric literals in some other languages, a leading "0" is not interpreted as an octal format value.
		4. "e" or "E", optionally followed by "+" or "-", followed by a sequence of one or more [0-9] characters.
		 or
		4. Optional: "L" terminating character, which is does not change the value.

	The result is a 256-bit bigint_t.  If negative, the value is sign extended through the full 256 bits.

	Limits
	------
	The value of the result must be integral (the value following the decimal point, after adjusting for the exponent, must be zero).
	The absolute value of the mantissa (everything before the exponent, with the decimal point stripped out) must be < 2^256.
	The value of the result must be -MAXVALN <= result <= MAXVALP.
	If NBITS == 0, MAXVALN and MAXVALP are set equal to the input MAXVAL which is an unsigned 256-bit value.
	If NBITS > 0, then MAXVALN is set to (2^NBITS) and MAXVALP is set to (2^NBITS)-1, where NBITS must be <= 256.
		Note: when parsing signed numbers, the sign bit should not be included in NBITS.
		for example, to parse a 64-bit signed value, set NBITS to 63.
*/

#include "cclib.h"
#include "jsonutil.h"
#include "encode.h"
#include "CCparams.h"

//#define TEST_INVALID_BINARY_TX_VALS		1	// test allowing tx's with invalid values to be converted to wire format--the resulting wire tx should be invalid

#ifndef TEST_INVALID_BINARY_TX_VALS
#define TEST_INVALID_BINARY_TX_VALS		0	// don't test
#endif

#define TRACE	0

string json_escape(const string& str)
{
	auto s = str;

	for (unsigned i = 0; i < s.length(); ++i)
	{
		auto c = s[i];

		if (c == '"')	s.replace(i++, 1, "\\\"");
		if (c == '\\')	s.replace(i++, 1, "\\\\");
		if (c == '\b')	s.replace(i++, 1, "\\b");
		if (c == '\f')	s.replace(i++, 1, "\\f");
		if (c == '\n')	s.replace(i++, 1, "\\n");
		if (c == '\r')	s.replace(i++, 1, "\\r");
		if (c == '\t')	s.replace(i++, 1, "\\t");
	}

	return s;
}

CCRESULT copy_error_to_output(const string& fn, const string& error, char *output, const uint32_t outsize, CCRESULT rc)
{
	if (output && outsize)
	{
		size_t eol = fn.copy(output, outsize - 1);

		if (eol)
		{
			eol += string(" ").copy(output + eol, outsize - eol - 1);
		}

		eol += error.copy(output + eol, outsize - eol - 1);

		output[eol] = 0;
	}

	return rc;
}

CCRESULT copy_result_to_output(const string& fn, const string& result, char *output, const uint32_t outsize)
{
	if (result.size() < outsize)
		return copy_error_to_output(string(), result, output, outsize, 0);
	else
		return error_buffer_overflow(fn, output, outsize, result.size() + 1);
}

CCRESULT error_buffer_overflow(const string& fn, char *output, const uint32_t outsize, const unsigned need)
{
	if (need)
		return copy_error_to_output(fn, string("error: output buffer overflow (need ") + to_string(need) + " bytes)", output, outsize, 1);
	else
		return copy_error_to_output(fn, "error: output buffer overflow", output, outsize, 1);
}

CCRESULT error_requires_binary_buffer(const string& fn, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, "error: binary buffer required", output, outsize);
}

CCRESULT error_unexpected(const string& fn, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, "error", output, outsize);
}

CCRESULT error_unexpected_key(const string& fn, const string& key, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: unexpected value \"") + key + "\"", output, outsize);
}

CCRESULT error_missing_key(const string& fn, const string& key, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: missing required value \"") + key + "\"", output, outsize);
}

CCRESULT error_value_overflow(const string& fn, const string& key, unsigned nbits, const bigint_t& maxval, char *output, const uint32_t outsize)
{
	if (!nbits && (BIGWORD(maxval, 1) || BIGWORD(maxval, 2) || BIGWORD(maxval, 3)))
		return copy_error_to_output(fn, string("error: value of \"") + key + "\" larger than prime modulus", output, outsize);
	else if (nbits)
		return copy_error_to_output(fn, string("error: value of \"" + key + "\" larger than ") + to_string(nbits) + " bits", output, outsize);
	else
		return copy_error_to_output(fn, string("error: value of \"" + key + "\" larger than ") + to_string(BIG64(maxval)), output, outsize);
}

CCRESULT error_invalid_numeric_char(const string& fn, const string& key, char c, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: invalid numeric character '") + c + "' in value of \"" + key + "\"", output, outsize);
}

CCRESULT error_not_hex(const string& fn, const string& key, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: non-hex character in value of \"") + key + "\"", output, outsize);
}

CCRESULT error_invalid_value(const string& fn, const string& key, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: invalid value for key \"") + key + "\"", output, outsize);
}

CCRESULT error_input_end(const string& fn, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, "error: input ends unexpectedly", output, outsize);
}

CCRESULT error_invalid_char(const string& fn, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, "error: invalid character", output, outsize);
}

CCRESULT error_unexpected_char(const string& fn, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, "error: unexpected character", output, outsize);
}

CCRESULT error_checksum_mismatch(const string& fn, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, "error: checksum mismatch", output, outsize);
}

CCRESULT error_not_array(const string& fn, const string& key, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: the \"") + key + "\" element must contain an array of values", output, outsize);
}

CCRESULT error_not_array_objs(const string& fn, const string& key, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: the \"") + key + "\" element must contain an array of objects", output, outsize);
}

CCRESULT error_too_many_objs(const string& fn, const string& key, unsigned limit, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: the \"" + key + "\" element is limited to ") + to_string(limit) + " objects", output, outsize);
}

CCRESULT error_num_values(const string& fn, const string& key, unsigned limit, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, string("error: the \"" + key + "\" element must contain ") + to_string(limit) + " values", output, outsize);
}

CCRESULT error_invalid_tx_type(const string& fn, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, "error: unrecognized transaction type", output, outsize);
}

CCRESULT error_invalid_binary_tx(const string& fn, char *output, const uint32_t outsize)
{
	return copy_error_to_output(fn, "error: invalid binary transaction", output, outsize);
}

CCRESULT error_invalid_binary_tx_value(const string& fn, const string& msg, char *output, const uint32_t outsize)
{
	copy_error_to_output(fn, string("error: invalid option for binary transaction; ") + msg, output, outsize);

	if (TEST_INVALID_BINARY_TX_VALS)
		return 0;

	return -1;
}

const Json::Value * json_find(const Json::Value& root, const char *key)
{
	return root.find(key, key + strlen(key));
}

CCRESULT parse_objid(const string& fn, const string& key, const string& sval, ccoid_t& objid, char *output, const uint32_t outsize)
{
	memset(&objid, 0, sizeof(objid));

	if (sval.length() != 2*sizeof(objid))
		return error_invalid_value(fn, key, output, outsize);

	for (unsigned i = 0; i < sval.length(); i += 2)
	{
		auto c1 = cc_destringify_char(base16combobin, sval[i]);
		auto c2 = cc_destringify_char(base16combobin, sval[i+1]);

		if (c1 > 15 || c2 > 15)
			return error_not_hex(fn, key, output, outsize);

		((char*)&objid)[i/2] = (c1 << 4) | c2;
	}

	return 0;
}

static CCRESULT parse_hex_value(const string& fn, const string& key, const string& sval, int start, int end, unsigned nbits, const bigint_t& maxval, bigint_t& val, char *output, const uint32_t outsize)
{
	if (end < start)
		return error_not_hex(fn, key, output, outsize);

	bigint_t premax = maxval / 16;

	for (int i = start; i <= end; ++i)
	{
		auto c = cc_destringify_char(base16combobin, sval[i]);

		if (c > 15)
			return error_not_hex(fn, key, output, outsize);

		if (TRACE) cerr << "parse_hex_value val " << hex << val << " premax " << premax << " maxval " << maxval << dec << endl;

		if (premax && val > premax)
			return error_value_overflow(fn, key, nbits, maxval, output, outsize);

		mulBigInt(val, bigint_t(16UL), val, false);	// don't want modulo prime so input can be full 256 bits

		if (maxval)
		{
			bigint_t premaxc;
			subBigInt(maxval, bigint_t((unsigned long)(c)), premaxc, false);

			if (TRACE) cerr << "parse_hex_value val " << hex << val << " premaxc " << premaxc << " maxval " << maxval << dec << endl;

			if (val > premaxc)
				return error_value_overflow(fn, key, nbits, maxval, output, outsize);
		}

		addBigInt(val, bigint_t((unsigned long)(c)), val, false);

		if (TRACE) cerr << "parse_hex_value val " << hex << val << " maxval " << maxval << dec << endl;

		if (maxval && val > maxval)
			return error_value_overflow(fn, key, nbits, maxval, output, outsize);
	}

	if (TRACE) cerr << "parse_hex_value val " << hex << val << " maxval " << maxval << dec << endl;

	return 0;
}

static CCRESULT parse_dec_value(const string& fn, const string& key, const string& sval, int start, int end, unsigned nbits, const bigint_t& maxval, bool isexp, bigint_t& val, char *output, const uint32_t outsize)
{
	if (end < start)
		return error_invalid_value(fn, key, output, outsize);

	bigint_t m1, premax;
	subBigInt(bigint_t(0UL), bigint_t(1UL), m1, false);
	premax = m1 / 10;

	int32_t frac = 0;
	int32_t exp = 0;

	for (int i = start; ; ++i)
	{
		if (i > end && !exp)
		{
			if (frac < 2)
				break;

			exp -= frac - 1;
		}

		char c;

		if (exp)
			c = '0';
		else
			c = sval[i];

		if (c == '.' && !frac && !isexp)
		{
			frac = 1;

			continue;
		}
		else if ((c == 'e' || c == 'E') && i > start && !isexp)
		{
			bigint_t bigval;
			auto rc = parse_int_value(fn, key, sval.substr(i+1), 24, 0UL, bigval, output, outsize, true);
			if (rc)
				return rc;

			exp = BIG64(bigval);

			if (frac > 1)
				exp -= frac - 1;

			if (!exp)
				break;

			continue;
		}
		else if (c < '0' || c > '9')
			return error_invalid_numeric_char(fn, key, c, output, outsize);
		else
			c -= '0';

		if (frac)
			++frac;

		if (exp >= 0)
		{
			if (TRACE) cerr << "parse_dec_value frac " << frac << " exp " << exp << " val " << val << " premax " << premax << " 0x" << hex << premax << dec << endl;

			if (val > premax)
				return error_value_overflow(fn, key, 256, m1, output, outsize);

			mulBigInt(val, bigint_t(10UL), val, false);		// don't want modulo prime so input can be full 256 bits

			if (maxval)
			{
				bigint_t premaxc;
				subBigInt(bigint_t(0UL), bigint_t((unsigned long)(c) + 1), premaxc, false);

				if (TRACE) cerr << "parse_dec_value frac " << frac << " exp " << exp << " val " << val << " premaxc " << premaxc << " 0x" << hex << premaxc << dec << endl;

				if (val > premaxc)
					return error_value_overflow(fn, key, 256, m1, output, outsize);
			}

			addBigInt(val, bigint_t((unsigned long)(c)), val, false);

			if (TRACE) cerr << "parse_dec_value frac " << frac << " exp " << exp << " val " << val << " maxval " << maxval << " 0x" << hex << maxval << dec << endl;

			if (exp > 0 && !--exp)
				break;
		}
		else
		{
			bigint_t newval = val / 10;
			bigint_t rem;
			mulBigInt(newval, bigint_t(10UL), rem, false);
			subBigInt(val, rem, rem, false);
			if (rem)
				return error_invalid_value(fn, key, output, outsize);

			val = newval;

			if (!++exp)
				break;
		}
	}

	if (TRACE) cerr << "parse_dec_value end val " << val << " maxval " << maxval << " 0x" << hex << maxval << dec << endl;

	if (maxval && val > maxval)
		return error_value_overflow(fn, key, nbits, maxval, output, outsize);

	return 0;
}

static CCRESULT parse_signed_value(const string& fn, const string& key, const string& sval, unsigned nbits, bigint_t& maxval, bool isexp, unsigned &negative, bigint_t& val, char *output, const uint32_t outsize)
{
	if (TRACE) cerr << "parse_signed_value isexp " << isexp << " nbits " << nbits << " val " << sval << " maxval " << maxval << " 0x" << hex << maxval << dec << endl;

	val = 0UL;
	negative = 0;
	unsigned has_sign = 0;

	if (maxval)
	{
		CCASSERT(!nbits);
	}
	else
	{
		CCASSERT(nbits);

		for (unsigned i = 0; i < nbits; ++i)
		{
			bigint_shift_up(maxval, 1);
			addBigInt(maxval, bigint_t(1UL), maxval, false);	// don't want modulo prime so input can be full 256 bits
			if (TRACE) cerr << "parse_signed_value i " << i << " maxval " << maxval << " " << BIGWORD(maxval, 3) << " " << BIGWORD(maxval, 2) << " " << BIGWORD(maxval, 1) << " " << BIGWORD(maxval, 0) << endl;
		}
	}

	if (sval.empty())
		return error_invalid_value(fn, key, output, outsize);

	if (TRACE) cerr << "parse_signed_value nbits " << nbits << " maxval " << maxval << " 0x" << hex << maxval << dec << endl;

	auto prechar = sval[0];

	if (prechar == '-' || (prechar == '+' && isexp))
	{
		if (sval.length() < 2)
			return error_invalid_value(fn, key, output, outsize);

		has_sign = 1;

		if (prechar == '-')
		{
			negative = 1;

			if (nbits)
			{
				addBigInt(maxval, bigint_t(1UL), maxval, false);
				if (!maxval)
					subBigInt(maxval, bigint_t(1UL), maxval, false);
			}
		}
	}

	if (TRACE) cerr << "parse_signed_value nbits " << nbits << " maxval " << maxval << " 0x" << hex << maxval << dec << endl;

	int end = sval.length() - 1;
	if (sval[end] == 'L' && !isexp)
		--end;

	auto prefix = sval.substr(has_sign, 1);

	if ((prefix == "x" || prefix == "X") && !isexp)
		return parse_hex_value(fn, key, sval, has_sign + 1, end, nbits, maxval, val, output, outsize);

	if (sval.length() >= has_sign + 2)
		prefix = sval.substr(has_sign, 2);

	if ((prefix == "0x" || prefix == "0X") && !isexp)
		return parse_hex_value(fn, key, sval, has_sign + 2, end, nbits, maxval, val, output, outsize);

	return parse_dec_value(fn, key, sval, has_sign, end, nbits, maxval, isexp, val, output, outsize);
}

CCRESULT parse_int_value(const string& fn, const string& key, const string& sval, unsigned nbits, bigint_t maxval, bigint_t& val, char *output, const uint32_t outsize, bool isexp)
{
	unsigned negative;

	if (TRACE) cerr << "parse_int_value isexp " << isexp << " nbits " << nbits << " val " << sval << " maxval " << maxval << " 0x" << hex << maxval << dec << endl;

	auto rc = parse_signed_value(fn, key, sval, nbits, maxval, isexp, negative, val, output, outsize);
	if (rc)
		return rc;

	if (TRACE) cerr << "parse_int_value negative " << negative << " val " << val << endl;

	if (negative && val)
	{
		if (maxval == TX_FIELD_MAX)
			val = bigint_t(0UL) - val;					// negate in the prime field
		else
			subBigInt(bigint_t(0UL), val, val, false);	// don't want modulo prime value
	}

	if (TRACE) cerr << "parse_int_value val " << val << " maxval " << maxval << " 0x" << hex << maxval << dec << endl;

	return 0;
}

CCRESULT parse_float_value(const char* json, Json::Value value, amtfloat_t& amountf)
{
	amountf = -1e99;

	try
	{
		unsigned start = value.getOffsetStart();
		unsigned end = value.getOffsetLimit();

		if (json[start] == '"')
			++start;

		if (end > start && json[end - 1] == '"')
			--end;

		auto str = string(&json[start], end - start);

		//cerr << "parse_float_value '" << str << "'" << endl;

		amountf = (amtfloat_t)(str);

		return 0;
	}
	catch (...)
	{
		return -1;
	}
}