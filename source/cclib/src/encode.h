/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * encode.h
*/

#pragma once

#include "CCapi.h"
#include "encodings.h"

#define CC_ENCODE_SEPARATOR		'0'
#define CC_ENCODE_SEPARATOR_ALT	'O'

void base64_encode(const uint8_t* table, const string& sval, string& outs, bool no_padding = false);
void base64_test(const uint8_t* encode_table, bool no_padding = false);

unsigned cc_table_mod(const uint8_t* table);
unsigned cc_table_expected_strlen(const uint8_t* table, unsigned binlength);
uint8_t cc_stringify_byte(const uint8_t* table, unsigned c);
uint8_t cc_destringify_char(const uint8_t* table, unsigned c);

void cc_stringify(const uint8_t* table, const snarkfront::bigint_t& maxval, bool normalize, int nchars, const snarkfront::bigint_t& val, string &outs);
CCRESULT cc_destringify(const string& fn, const uint8_t* table, bool normalize, unsigned nchars, string &instring, snarkfront::bigint_t &val, char *output, const uint32_t outsize);

CCRESULT cc_alpha_encode(const uint8_t* table, const void* data, const unsigned nchars, vector<uint8_t> &outv, bool bclear = true);
void cc_alpha_decode(const uint8_t* table, const void* data, const unsigned nbytes, string &outs, const unsigned nchars = 0, bool bclear = true);

CCRESULT cc_alpha_encode_shortest(const uint8_t* encode_table, const uint8_t* decode_table, const void* data, const unsigned nchars, vector<uint8_t> &outv, bool bclear = true);
CCRESULT cc_alpha_encode_best(const void* data, const unsigned nchars, vector<uint8_t> &outv);
CCRESULT cc_alpha_decode_best(const void* data, const unsigned nbytes, string &outs);

void encode_test();
