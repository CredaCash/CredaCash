/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * encode.cpp
*/

#include "cclib.h"
#include "encode.h"
#include "jsonutil.h"

#define TRACE	0

#define LENGTH_DIFF_OFFSET		9

void base64_test(const uint8_t* encode_table, const uint8_t* decode_table, bool no_padding)
{
	for (unsigned i = 0; i < 20000; ++i)
	{
		unsigned nbytes = (rand() % 40) + 1;

		vector<char> din, dout;
		string str, enc;

		for (unsigned j = 0; j < nbytes; ++j)
		{
			unsigned c = rand() & 255;

			din.push_back(c);

			str.push_back(g_hex_digits[c >> 4]);
			str.push_back(g_hex_digits[c & 15]);
		}

		cerr << str << endl;

		base64_encode(encode_table, din.data(), nbytes, enc, no_padding);

		cerr << enc << endl;

		auto rc = base64_decode(decode_table, enc, dout);
		CCASSERTZ(rc);
		CCASSERT(dout.size() == nbytes);
		CCASSERTZ(memcmp(dout.data(), din.data(), nbytes));
	}
}

void base64_encode(const uint8_t* table, const void* sval, unsigned slen, string& outs, bool no_padding)
{
	CCASSERT(cc_table_mod(table) >= 64);

	outs.clear();

	for (unsigned i = 0; i < slen; i += 3)
	{
		unsigned group = 0;
		int nbits = 0;

		for (unsigned j = 0; j < 3; ++j)
		{
			group <<= 8;
			if (i + j < slen)
			{
				group |= *((uint8_t*)sval + i + j);
				nbits += 8;

				if (TRACE) cerr << "base64_encode i " << i << " j " << j << " byte " << (int)*((uint8_t*)sval + i + j) << " group " << group << " nbits " << nbits << endl;
			}
		}

		for (unsigned j = 0; nbits > 0; ++j)
		{
			outs.push_back(cc_stringify_byte(table, (group >> ((3-j) * 6)) & 63));
			nbits -= 6;

			if (TRACE) cerr << "base64_encode symbol " << outs.back() << " nbits " << nbits << endl;
		}
	}

	while (!no_padding && (outs.length() & 3))
		outs.push_back('=');
}

int base64_decode(const uint8_t* table, const string& str, vector<char> &data)
{
	data.clear();

	auto slen = str.length();

	while (slen && str[slen-1] == '=')
		--slen;

	unsigned acc = 0;
	int nbits = 0;

	for (unsigned i = 0; i < slen; ++i)
	{
		auto c = str[i];

		auto val = cc_destringify_char(table, c);

		if (val == 255)
			return -1;

		acc = (acc << 6) | val;
		nbits += 6;

		if (TRACE) cerr << "base64_decode i " << i << " symbol " << c << " val " << val << " acc " << acc << " nbits " << nbits << endl;

		if (nbits >= 8)
		{
			nbits -= 8;
			data.push_back(acc >> nbits);

			acc &= (1U << nbits) - 1;

			if (TRACE) cerr << "base64_decode byte " << (int)(uint8_t)data.back() << " acc " << acc << " nbits " << nbits << endl;
		}
	}

	return 0;
}

unsigned cc_table_mod(const uint8_t* table)
{
	unsigned mod = table[0];

	if (!mod) mod = 256;

	return mod;
}

// computed expected size of string that would be output by cc_alpha_decode
// call this with a *sym table
unsigned cc_table_expected_strlen(const uint8_t* table, unsigned binlength)
{
	unsigned resize = (table[1] << 8) + table[2];
	if (!resize) resize = 1 << 16;

	return binlength * (1 << 16) / resize;
}

// call this with a *sym table
unsigned cc_stringify_byte(const uint8_t* table, uint8_t c)
{
	if (c >= cc_table_mod(table))
	{
		cerr << "cc_stringify_byte out-of-range char " << c << " >= " << cc_table_mod(table) << endl;

		CCASSERT(c < cc_table_mod(table));
	}

	return table[c + 3];
}

// call this with a *bin table
unsigned cc_destringify_char(const uint8_t* table, uint8_t c)
{
	//cerr << "cc_destringify_char " << c << " " << table[1] << " " << table[2] << endl;
	//cerr << "cc_destringify_char " << (int)c << " " << (int)table[1] << " " << (int)table[2] << endl;

	if (c < table[1] || c > table[2])
	{
		if (0+TRACE) cerr << "cc_destringify_char " << c << " out of range " << (int)table[1] << " to " << (int)table[2] << endl;

		return 255;
	}

	unsigned i = table[c - table[1] + 3];

	if (0 && TRACE) cerr << "cc_destringify_char " << c << " " << table[1] << " " << table[2] << endl;
	if (0 && TRACE) cerr << "cc_destringify_char " << (int)c << " " << (int)table[1] << " " << (int)table[2] << endl;
	if (0 && TRACE) cerr << "cc_destringify_char " << i << endl;

	return i;
}

// shifts out all least significant zero bits
// returns shift amount
static unsigned compute_shift(const unsigned mod, bigint_t& maxval, bigint_t& val)
{
	unsigned shift = 0;

	while (true)
	{
		if (val.asUnsignedLong() & 1)
			break;

		bigint_shift_down(val, 1);
		bigint_shift_down(maxval, 1);

		if (++shift == mod - 1)
			break;
	}

	//cerr << "compute_shift " << shift << endl;

	return shift;
}

#if 0 // unused
// destroys val
static bigint_t bitreverse(bigint_t& val, const unsigned nbits)
{
	bigint_t v = 0UL;

	for (unsigned i = 0; i < nbits; ++i)
	{
		bigint_shift_up(val, 1);

		if (val.asUnsignedLong() & 1)
			v = v + bigint_t(1UL);

		bigint_shift_down(val, 1);
	}

	return v;
}
#endif

// call this with a *sym table
void cc_stringify(const uint8_t* table, const bigint_t& maxval, bool normalize, int nchars, const bigint_t& val, string &outs)
{
	// if normalize is true, the encoding is prefixed by a single char that represents the bit shift left needed to decode the value
	// if nchars > 0, the encoding has as fixed width of nchars
	// if nchars == 0, the encoding has the fixed width that would be required to encode maxval
	// if nchars < 0, the encoding is variable length and ends when the remainder is zero

	bigint_t v = val;
	auto mod = cc_table_mod(table);

	//if (nbits)
	//	v = bitreverse(v, nbits);

	bigint_t mval = maxval;
	if (mval == 0UL)
		subBigInt(bigint_t(0UL), bigint_t(1UL), mval, false);

	//cerr << "encode mval " << hex << mval << dec << endl;

	if (normalize)
	{
		auto shift = compute_shift(mod, mval, v);
		outs.push_back(cc_stringify_byte(table, shift));
	}

	int nc = 0;
	while (mval)
	{
		outs.push_back(cc_stringify_byte(table, v % mod));
		v = v / mod;
		mval = mval / mod;
		nc++;

		//cerr << "encode " << hex << val << " " << v << " " << mval << dec << " " << nc << " " << nchars << " " << outs << endl;

		if (nc == nchars || (nchars < 0 && v == 0UL))
			break;
	}
}

// removes first field from instring and places decoded value into val
// either nchars must be set, or the field must be terminated with CC_ENCODE_SEPARATOR or CC_ENCODE_SEPARATOR_ALT
// call this with a *bin table
CCRESULT cc_destringify(const string& fn, const uint8_t* table, bool normalize, unsigned nchars, string &instring, bigint_t &val, char *output, const uint32_t outsize)
{
	val = 0UL;
	unsigned shift = 0;
	auto mod = cc_table_mod(table);

	if (normalize)
	{
		if (instring.length() < 1)
			return error_input_end(fn, output, outsize);

		shift = cc_destringify_char(table, instring[0]);

		if (shift == 255)
			return error_invalid_char(fn, output, outsize);

		//cerr << "decode shift " << shift << endl;
	}

	if (!nchars)
	{
		nchars = instring.find(CC_ENCODE_SEPARATOR);
		auto nchars2 = instring.find(CC_ENCODE_SEPARATOR_ALT);
		if (nchars2 < nchars)
			nchars = nchars2;
		if (nchars >= instring.length())
			return error_input_end(fn, output, outsize);
	}

	nchars -= normalize;

	//cerr << "decode " << nchars << " " << instring << endl;

	if (instring.length() < nchars + normalize)
		return error_input_end(fn, output, outsize);

	string sval = instring.substr(normalize, nchars);
	instring = instring.substr(nchars + normalize);

	//cerr << "decode " << sval << endl;

	while (sval.size())
	{
		auto c = cc_destringify_char(table, sval.back());
		sval.pop_back();

		if (c == 255)
			return error_invalid_char(fn, output, outsize);

		mulBigInt(val, bigint_t((unsigned long)(mod)), val, false);
		addBigInt(val, bigint_t((unsigned long)(c)), val, false);
	}

	bigint_shift_up(val, shift);

	return 0;
}

#if 0	// for testing
#define CC_ENC_SHIFT	16
#endif

typedef uint64_t encint_t;

#define CC_ENC_SHIFT	(sizeof(encint_t)*8 - 8 - 7 - 1)
#define CC_ENC_LOWER	(((encint_t)1 << CC_ENC_SHIFT) - 1)
#define CC_ENC_UPPER	((encint_t)(-1) ^ CC_ENC_LOWER)
#define CC_ENC_MAX		((encint_t)1 << CC_ENC_SHIFT << 8)

#define range_check(v)	CCASSERT((v) < (CC_ENC_MAX << 7))
#define DFMT			hex

// Converts symbols to binary data, using range encoding
// This implementation is lossless when going from symbols -> binary -> symbols
// Binary -> symbols can be encoded in blocks of 32 bytes (256 bits) at a time using cc_stringify and cc_destringify
// call this with a *bin table

CCRESULT cc_alpha_encode(const uint8_t* table, const void* data, const unsigned nchars, vector<char> &outv, bool bclear)
{
	CCASSERT(CC_ENC_MAX);

	auto mod = cc_table_mod(table);

	if (TRACE) cerr << "cc_alpha_encode mod " << mod << " nchars " << nchars << " string " << string((char*)data, nchars) << endl;

	if (bclear)
		outv.clear();

	if (!nchars)
		return 0;

	auto sym = (const uint8_t *)data;
	unsigned bufpos = 0;
	encint_t hval = CC_ENC_MAX - 1;
	encint_t lval = 0;
	encint_t eofm = 0;
	bool done = false;

	if (mod == 256)
	{
		for (unsigned i = 0; i < nchars; ++i)
			outv.push_back(sym[i]);

		return 0;
	}

	range_check(hval);

	while (!done)
	{
		unsigned c = mod/2; // picking the midpoint for phantom input symbols may result in shorter output

		if (bufpos < nchars)
			c = cc_destringify_char(table, sym[bufpos++]);

		if (c == 255)
			return -1;

		auto denom = hval - lval + 1;

		hval = lval + ((c + 1) * denom + mod - 1) / mod - 1;
		lval = lval + (c * denom + mod - 1) / mod;

		if (TRACE) cerr << "cc_alpha_encode symbol in " << DFMT << c << " lval " << lval << " hval " << hval << " eofm " << eofm << dec << endl;

		range_check(hval);

		while ((((hval ^ lval) & CC_ENC_UPPER) == 0) || hval < lval + mod - 1)
		{
			auto b = lval >> CC_ENC_SHIFT;

			outv.push_back(b);

			hval = ((hval & CC_ENC_LOWER) << 8) | 255;
			lval = ((lval & CC_ENC_LOWER) << 8);

			if (TRACE) cerr << "cc_alpha_encode byte out " << DFMT << (int)b << " lval " << lval << " hval " << hval << " eofm " << eofm << dec << endl;

			range_check(hval);

			if (bufpos == nchars)
			{
				eofm = (eofm << 8) | 255;

				if (((eofm >> CC_ENC_SHIFT) & 255) == 255)
				{
					done = true;	// all bits of next output char would be from the eofm, so we're done

					break;
				}
			}
		}
	}

	if (TRACE) cerr << "cc_alpha_encode " << string((char*)data, nchars) << " --> " << buf2hex(outv.data(), outv.size()) << endl;

	return 0;
}

// Converts binary data to symbols
// call this with a *sym table

void cc_alpha_decode(const uint8_t* table, const void* data, const unsigned nbytes, const unsigned nchars, string &outs, bool bclear)
{
	auto mod = cc_table_mod(table);

	if (TRACE) cerr << "cc_alpha_decode mod " << mod << " nchars " << nchars << " nbytes " << nbytes << " data " << buf2hex(data, nbytes) << endl;

	if (bclear)
		outs.clear();

	if (!nbytes || !nchars)
		return;

	auto buf = (const uint8_t *)data;
	unsigned bufpos = 0;
	auto outs_base = outs.length();
	encint_t dval = 0;
	encint_t hval = 0;
	encint_t lval = 0;

	if (mod == 256)
	{
		auto n = nchars < nbytes ? nchars : nbytes;

		for (unsigned i = 0; i < n; ++i)
			outs.push_back(buf[i]);

		return;
	}

	while (outs.length() < outs_base + nchars)
	{
		while ((((hval ^ lval) & CC_ENC_UPPER) == 0) || hval < lval + mod - 1)
		{
			unsigned b = 128; // picking the midpoint for phantom input bytes may result in shorter output strings

			if (bufpos < nbytes)
				b = buf[bufpos];

			++bufpos;

			hval = ((hval & CC_ENC_LOWER) << 8) | 255;
			dval = ((dval & CC_ENC_LOWER) << 8) | b;
			lval = ((lval & CC_ENC_LOWER) << 8);

			if (TRACE) cerr << "cc_alpha_decode byte in " << DFMT << (int)b << " lval " << lval << " dval " << dval << " hval " << hval << dec << endl;

			range_check(hval);
		}

		if (dval < lval || dval > hval || hval < lval)
			cerr << "   **** cc_alpha_decode range error lval " << DFMT << lval << " dval " << dval << " hval " << hval << dec << endl;

		// c = int(((dval - lval) * mod) / (hval - lval + 1)) -- which is always < mod

		auto denom = hval - lval + 1;
		auto c = ((dval - lval) * mod) / denom;

		if (c >= mod)
			cerr << "   **** cc_alpha_decode range error char " << DFMT << c << " >= mod " << mod << " lval " << lval << " dval " << dval << " hval " << hval << dec << endl;

		// new hval = old lval + int(((c + 1) * denom + mod - 1) / mod) - 1
		hval = lval + ((c + 1) * denom + mod - 1) / mod - 1;

		// new lval = old lval + int((c * denom + mod - 1) / mod)
		lval = lval + (c * denom + mod - 1) / mod;

		outs.push_back(cc_stringify_byte(table, c));

		if (TRACE) cerr << "cc_alpha_decode symbol out " << DFMT << (int)c << " lval " << lval << " dval " << dval << " hval " << hval << dec << endl;

		range_check(hval);
	}

	if (TRACE) cerr << "cc_alpha_decode result " << outs << " <-- " << buf2hex(data, nbytes) << endl;
}

// call this with a *bin table
bool cc_string_uses_symbols(const uint8_t* table, const void* data, const unsigned nchars)
{
	if (cc_table_mod(table) == 256)
		return true;

	for (unsigned i = 0; i < nchars; ++i)
	{
		unsigned sym = *((uint8_t*)data + i);

		auto c = cc_destringify_char(table, sym);

		//cerr << "symbol " << sym << " = " << sym << " --> binary " << (int)c << endl;

		if (c == 255)
			return false;
	}

	return true;
}

// encodes data, then truncates the result to make it as short as possible
CCRESULT cc_alpha_encode_shortest(const uint8_t* encode_table, const uint8_t* decode_table, const void* data, const unsigned nchars, vector<char> &outv, bool bclear)
{
	if (TRACE) cerr << "cc_alpha_encode_shortest " << string((char*)data, nchars) << endl;

	auto start_size = outv.size();

	auto rc = cc_alpha_encode(encode_table, data, nchars, outv, bclear);
	if (rc)
		return rc;

	auto shortest_size = outv.size() - start_size;

	for (auto test_size = shortest_size; test_size > 0; --test_size)
	{
		int expected_len = cc_table_expected_strlen(decode_table, test_size);
		int len_diff = expected_len - nchars;
		len_diff += LENGTH_DIFF_OFFSET;
		if (TRACE) cerr << "cc_alpha_encode_shortest test_size " << test_size << " len_diff " << len_diff << endl;
		if (len_diff < 0 || len_diff > 15)
			continue;

		string decoded;

		cc_alpha_decode(decode_table, outv.data() + start_size, test_size, nchars, decoded);

		if (decoded.length() != nchars)
			break;

		if (memcmp(decoded.data(), data, nchars))
			break;

		shortest_size = test_size;
	}

	outv.resize(start_size + shortest_size);

	if (TRACE) cerr << "cc_alpha_encode_shortest input size " << nchars << " binary size " << shortest_size << endl;

	return 0;
}

static const uint8_t* best_encode_table[] = {base10bin, base16bin, base32bin, base32zbin, base34bin, base38bin, base58bin, base66bin, base95bin, base224bin, base256bin};
static const uint8_t* best_decode_table[] = {base10sym, base16sym, base32sym, base32zsym, base34sym, base38sym, base58sym, base66sym, base95sym, base224bin, base256sym};

// encodes data, selecting the table that gives the shortest result
CCRESULT cc_alpha_encode_best(const void* data, const unsigned nchars, vector<char> &outv)
{
	if (TRACE) cerr << "cc_alpha_encode_best " << string((char*)data, nchars) << endl;

	if (!nchars)
	{
		outv.clear();

		return 0;
	}

	auto ntables = sizeof(best_encode_table)/sizeof(uint8_t*);

	for (unsigned i = 0; i < ntables; ++i)
	{
		outv.clear();
		outv.push_back(i << 4);

		auto rc = cc_alpha_encode_shortest(best_encode_table[i], best_decode_table[i], data, nchars, outv, false);
		if (rc)
			continue;

		auto mod = cc_table_mod(best_decode_table[i]);
		int expected_len = cc_table_expected_strlen(best_decode_table[i], outv.size() - 1);
		int len_diff = expected_len - nchars;

		if (TRACE) cerr << "cc_alpha_encode_best table " << i << " mod " << mod << " binary size " << outv.size() << " expected_len " << expected_len << " nchars " << nchars << " len_diff " << len_diff << endl;

		len_diff += LENGTH_DIFF_OFFSET;	// TODO? put this into a function?

		if (len_diff < 0 || len_diff > 15)
		{
			//cerr << "cc_alpha_encode_best error table " << i << " len_diff " << len_diff << " out of range" << endl;

			continue;
		}

		outv[0] |= len_diff;

		if (TRACE) cerr << "cc_alpha_encode_best table " << ((uint8_t)outv[0] >> 4) << " input size " << nchars << " output size " << outv.size() << endl;

		return 0;
	}

	outv.clear();

	return -1;
}

// work around for backward compatibility problem introduced in v2.0.1 release
// this problem scrambled XPay.foreign_block_id when decoding older blocks
static bool use_old_table_mapping_default = true;

void cc_alpha_set_default_decode_tables(uint64_t timestamp)
{
	if (timestamp > 1726100000)
		use_old_table_mapping_default = false;
}

CCRESULT cc_alpha_decode_best(const void* data, const unsigned nbytes, string &outs, int use_old_table_mapping)
{
	if (TRACE) cerr << "cc_alpha_decode_best " << buf2hex(data, nbytes) << endl;

	if (!nbytes)
	{
		outs.clear();

		return 0;
	}

	auto *bufp = (const uint8_t *)data;

	auto ntables = sizeof(best_encode_table)/sizeof(uint8_t*);
	unsigned table = bufp[0] >> 4;

	if (use_old_table_mapping < 0) use_old_table_mapping = use_old_table_mapping_default;
	if (table == 9 && use_old_table_mapping) table = 10;

	if (table > ntables - 1)
		return -1;

	int len_diff = (bufp[0] & 15) - LENGTH_DIFF_OFFSET;
	int expected_len = cc_table_expected_strlen(best_decode_table[table], nbytes - 1);
	int nchars = expected_len - len_diff;

	if (TRACE) cerr << "cc_alpha_decode_best table " << table << " binary size " << nbytes << " expected_len " << expected_len << " len_diff " << len_diff << " nchars " << nchars << endl;

	if (nchars < 0)
		return 0;

	cc_alpha_decode(best_decode_table[table], bufp + 1, nbytes - 1, nchars, outs);

	//cout << "cc_alpha_decode_best table " << table << " result " << outs << endl;

	return 0;
}

static void test_one_stringify(const char* table, const uint8_t* encode_table, const uint8_t* decode_table)
{
	if (cc_table_mod(encode_table) >= 255)
		return;

	bigint_t val1, val2;
	val1.randomize();

	if (RandTest(2)) BIGWORD(val1, 0) = 0;
	if (RandTest(2)) BIGWORD(val1, 1) = 0;
	if (RandTest(2)) BIGWORD(val1, 2) = 0;
	if (RandTest(2)) BIGWORD(val1, 3) = 0;

	string encoded;
	string fn = "test";
	char output[128] = {0};
	uint32_t outsize = sizeof(output);
	bool normalize = RandTest(2);

	cc_stringify(decode_table, 0UL, normalize, -1, val1, encoded);

	auto decoded = encoded;

	auto rc = cc_destringify(fn, encode_table, normalize, decoded.length(), decoded, val2, output, outsize);
	if (rc)
	{
		cerr << "stringify_test " << table << " normalize " << normalize << " encoded " << encoded << " decode error: " << output << endl;
		exit(-1);
	}
	else if (val1 != val2)
	{
		cerr << "stringify_test " << table << " normalize " << normalize << " encoded " << encoded << " mismatch " << hex << val1 << " != " << val2 << dec << endl;
		exit(-1);
	}
	else
		if (TRACE) cerr << "--stringify_test OK " << table << " normalize " << normalize << " encoded " << encoded << " val " << hex << val1 << dec << endl;
}

static void test_one_alpha(const char* table, const uint8_t* encode_table, const uint8_t* decode_table)
{
	auto mod = cc_table_mod(encode_table);

	static unsigned seed = 0 -1;
	//srand(++seed);
	//if (TRACE) cerr << "seed " << seed << " mod " << mod << endl;

	vector<char> rbuf(50), encoded;
	string random, decoded;

	auto buf = rbuf.data();
	auto bufsize = rbuf.size();
	//CCRandom(buf, bufsize);

	unsigned nbytes = rand() % (bufsize + 1);
	if (nbytes > 30 && RandTest(2))
		nbytes = (nbytes & 3) + 1;
	//nbytes = rand() & 7;

	for (unsigned i = 0; i < bufsize; ++i)
		buf[i] = rand();

	if (RandTest(2))
	{
		unsigned fill = rand();
		if (RandTest(2)) fill = -(rand() & 1);
		auto n = rand() % (bufsize + 1);
		if (RandTest(2))
			memset(buf, fill, n);
		else
			memset(&buf[n], fill, bufsize - n);
	}

	unsigned len = rand() % 50;
	if (len > 30 && RandTest(2))
		len = (len & 3) + 1;

	for (unsigned i = 0; i < len; ++i)
		random.push_back(cc_stringify_byte(decode_table, rand() % mod));

	if (RandTest(2))
	{
		unsigned fill = rand() % mod;
		if (RandTest(2)) fill = RandTest(2) * (mod - 1);
		fill = cc_stringify_byte(decode_table, fill);
		auto n = rand() % (len + 1);
		if (RandTest(2))
			memset((char*)random.data(), fill, n);
		else
			memset((char*)random.data() + n, fill, len - n);
	}

	auto check = cc_string_uses_symbols(encode_table, random.data(), random.length());
	CCASSERT(check);

	auto rc = cc_alpha_encode(encode_table, random.data(), random.length(), encoded);
	if (rc)
	{
		cerr << "alpha_test " << table << " seed " << seed << " decode invalid input error " << random << endl;
		exit(-1);
	}

	cc_alpha_decode(decode_table, encoded.data(), encoded.size(), random.length(), decoded);

	if (decoded.length() != random.length())
	{
		if (TRACE) cerr << "alpha_test " << table << " seed " << seed << " warning decoded size mismatch " << decoded.length() << " != " << random.length() << endl;
		//exit(-1);
	}

	if (decoded.length() < random.length() || memcmp(random.data(), decoded.data(), random.length()))
	{
		cerr << "alpha_test " << table << " seed " << seed << " decoded " << decoded << " != " << random << endl;
		exit(-1);
	}

	if (0+TRACE) cerr << "--alpha_test OK " << table << " random " << random << endl;
}

static int max_best_test_length_diff = INT_MIN;
static int min_best_test_length_diff = INT_MAX;

static void test_best_alpha(const char* table, const uint8_t* encode_table, const uint8_t* decode_table)
{
	auto mod = cc_table_mod(encode_table);

	static unsigned seed = 0 -1;
	//srand(++seed);
	//if (0+TRACE) cerr << "seed " << seed << " mod " << mod << endl;

	string random, decoded;
	vector<char> encoded;

	unsigned len = rand() % 100;

	for (unsigned i = 0; i < len; ++i)
		random.push_back(cc_stringify_byte(decode_table, rand() % mod));

	auto check = cc_string_uses_symbols(encode_table, random.data(), random.length());
	CCASSERT(check);

	auto rc = cc_alpha_encode_best(random.data(), random.length(), encoded);
	if (rc)
	{
		cerr << "best_alpha_test cc_alpha_encode_best failed seed " << seed << endl;

		exit(-1);
	}

	rc = cc_alpha_decode_best(encoded.data(), encoded.size(), decoded);
	if (rc)
	{
		cerr << "best_alpha_test cc_alpha_decode_best failed seed " << seed << endl;

		exit(-1);
	}

	if (decoded != random)
	{
		cerr << "best_alpha_test seed " << seed << " decoded mismatch " << decoded << " != " << random << endl;

		exit(-1);
	}

	if (encoded.size())
	{
		int len_diff = encoded[0] & 15;
		max_best_test_length_diff = max(max_best_test_length_diff, len_diff);
		min_best_test_length_diff = min(min_best_test_length_diff, len_diff);
	}
}

static void test_one(const char* table, const uint8_t* encode_table, const uint8_t* decode_table)
{
	test_one_stringify(table, encode_table, decode_table);

	test_one_alpha(table, encode_table, decode_table);

	test_best_alpha(table, encode_table, decode_table);
}

static void static_test()
{
	const int nstrings = 3;
	const string strings[nstrings] = {
		"205800",
		"71b9cd0864c66880fd4fb16ac2f0102c949d0df73f58e8f05516e410af1ccf9c",
		"qp05fd87402sh5j9596wd5cq072sjucc050ynyjjdl"
	};

	for (int i = 0; i < nstrings; ++i)
	{
		vector<char> outv;
		auto rc = cc_alpha_encode_best(strings[i].data(), strings[i].size(), outv);
		cerr << "static_test string " << i << " rc " << rc << " input size " << strings[i].size() <<
			" result size " << outv.size() << " table " << ((uint8_t)outv[0] >> 4) << endl;
	}
}

void encode_test()
{
	cerr << "encode_test" << endl;

	static_test();
	//return;

	for (unsigned i = 0; i < 10000+1*200000; ++i)
	{
		if (!(i % 10000)) cerr << i << endl;

		test_one("base256",		base256bin,			base256sym);
		test_one("base224",		base224bin,			base224sym);
		test_one("base95",		base95bin,			base95sym);
		test_one("base87",		base87bin,			base87sym);
		test_one("base85",		base85bin,			base85sym);
		test_one("base66",		base66bin,			base66sym);
		test_one("base64",		base64bin,			base64sym);
		test_one("base64url",	base64urlbin,		base64urlsym);
		test_one("base58",		base58bin,			base58sym);
		test_one("base57",		base57bin,			base57sym);
		test_one("base38",		base38bin,			base38sym);
		test_one("base38",		base38combobin,		base38sym);
		test_one("base38uc",	base38ucbin,		base38ucsym);
		test_one("base38uc",	base38combobin,		base38ucsym);
		test_one("base36",		base36bin,			base36sym);
		test_one("base36",		base36combobin,		base36sym);
		test_one("base36uc",	base36ucbin,		base36ucsym);
		test_one("base36uc",	base36combobin,		base36ucsym);
		test_one("base34",		base34bin,			base34sym);
		test_one("base32",		base32bin,			base32sym);
		test_one("base32z",		base32zbin,			base32zsym);
		test_one("base26",		base26bin,			base26sym);
		test_one("base26",		base26combobin,		base26sym);
		test_one("base26uc",	base26ucbin,		base26ucsym);
		test_one("base26uc",	base26combobin,		base26ucsym);
		test_one("base17",		base17bin,			base17sym);
		test_one("base17",		base17combobin,		base17sym);
		test_one("base17uc",	base17ucbin,		base17ucsym);
		test_one("base17uc",	base17combobin,		base17ucsym);
		test_one("base16",		base16bin,			base16sym);
		test_one("base16",		base16combobin,		base16sym);
		test_one("base16uc",	base16ucbin,		base16ucsym);
		test_one("base16uc",	base16combobin,		base16ucsym);
		test_one("base10",		base10bin,			base10sym);
		test_one("base8",		base8bin,			base8sym);
	}

	cerr << "encode_test max_best_test_length_diff " << max_best_test_length_diff << endl;
	cerr << "encode_test min_best_test_length_diff " << min_best_test_length_diff << endl;

	cerr << "encode_test done" << endl;
}
