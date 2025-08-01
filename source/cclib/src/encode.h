/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * encode.h
*/

#pragma once

#include "CCapi.h"
#include "encodings.h"

#define CC_ENCODE_SEPARATOR		'0'
#define CC_ENCODE_SEPARATOR_ALT	'O'

void base64_encode(const uint8_t* table, const void* sval, unsigned slen, string& outs, bool no_padding = false);
int base64_decode(const uint8_t* table, const string& val, vector<char> &data);
void base64_test(const uint8_t* encode_table, const uint8_t* decode_table, bool no_padding = false);
inline void base64_encode_string(const uint8_t* table, const string& sval, string& outs, bool no_padding = false)
{
	base64_encode(table, sval.data(), sval.length(), outs, no_padding);
}

unsigned cc_table_mod(const uint8_t* table);
unsigned cc_table_expected_strlen(const uint8_t* table, unsigned binlength);
unsigned cc_stringify_byte(const uint8_t* table, uint8_t c);
unsigned cc_destringify_char(const uint8_t* table, uint8_t c);

void cc_stringify(const uint8_t* table, const snarkfront::bigint_t& maxval, bool normalize, int nchars, const snarkfront::bigint_t& val, string &outs);
CCRESULT cc_destringify(const string& fn, const uint8_t* table, bool normalize, unsigned nchars, string &instring, snarkfront::bigint_t &val, char *output, const uint32_t outsize);

CCRESULT cc_alpha_encode(const uint8_t* table, const void* data, const unsigned nchars, vector<char> &outv, bool bclear = true);
void cc_alpha_decode(const uint8_t* table, const void* data, const unsigned nbytes, const unsigned nchars, string &outs, bool bclear = true);

CCRESULT cc_alpha_encode_shortest(const uint8_t* encode_table, const uint8_t* decode_table, const void* data, const unsigned nchars, vector<char> &outv, bool bclear = true);
CCRESULT cc_alpha_encode_best(const void* data, const unsigned nchars, vector<char> &outv);
CCRESULT cc_alpha_decode_best(const void* data, const unsigned nbytes, string &outs, int use_old_table_mapping = -1);

void cc_alpha_set_default_decode_tables(uint64_t timestamp);

void encode_test();
