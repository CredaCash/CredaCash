/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * jsonutil.h
*/

#pragma once

#include <jsoncpp/json/json.h>

//#define JSON_ENDL	;
#define JSON_ENDL	<< endl;

#define SEPARATOR		'0'
#define SEPARATOR_ALT	'O'

extern const unsigned char base58[58];
extern const unsigned char base58int[76];
extern const unsigned char base64[64];
extern const unsigned char base64int[80];

CCRESULT copy_error_to_output(const string& error, char *output, const uint32_t bufsize, CCRESULT rc = -1);
CCRESULT copy_result_to_output(const string& fn, const string& result, char *output, const uint32_t bufsize);
CCRESULT error_unexpected_key(const string& fn, const string& key, char *output, const uint32_t bufsize);
CCRESULT error_missing_key(const string& fn, const string& key, char *output, const uint32_t bufsize);
CCRESULT error_value_overflow(const string& fn, const string& key, unsigned nbits, const bigint_t& maxval, char *output, const uint32_t bufsize);
CCRESULT error_not_numeric(const string& fn, const string& key, char *output, const uint32_t bufsize);
CCRESULT error_not_hex(const string& fn, const string& key, char *output, const uint32_t bufsize);
CCRESULT error_invalid_value(const string& fn, const string& key, char *output, const uint32_t bufsize);
CCRESULT error_input_end(const string& fn, char *output, const uint32_t bufsize);
CCRESULT error_invalid_char(const string& fn, char *output, const uint32_t bufsize);
CCRESULT error_unexpected_char(const string& fn, char *output, const uint32_t bufsize);
CCRESULT error_checksum_mismatch(const string& fn, char *output, const uint32_t bufsize);
CCRESULT error_not_array(const string& fn, const string& key, char *output, const uint32_t bufsize);
CCRESULT error_not_array_objs(const string& fn, const string& key, char *output, const uint32_t bufsize);
CCRESULT error_too_many_objs(const string& fn, const string& key, unsigned limit, char *output, const uint32_t bufsize);
CCRESULT error_num_values(const string& fn, const string& key, unsigned limit, char *output, const uint32_t bufsize);
CCRESULT error_invalid_tx(const string& fn, char *output, const uint32_t bufsize);

const Json::Value * json_find(const Json::Value& root, const char *key);

CCRESULT parse_int_value(const string& fn, const string& key, const string& sval, unsigned nbits, bigint_t maxval, bigint_t& val, char *output, const uint32_t bufsize);

void encode(const unsigned char* table, const unsigned mod, const bigint_t& maxval, bool normalize, int nchars, const bigint_t& val, string &outs);

unsigned decode_char(const unsigned char* table, const unsigned mod, unsigned char c);
CCRESULT decode(const string& fn, const unsigned char* table, const unsigned mod, bool normalize, unsigned nchars, string& instring, bigint_t& val, char *output, const uint32_t bufsize);

void encodestring(const string &sval, string &outs);