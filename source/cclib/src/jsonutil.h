/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * jsonutil.h
*/

#pragma once

#include "CCapi.h"
#include "CCbigint.hpp"

#include "encodings.h"

#include <CCobjdefs.h>

#include <jsoncpp/json/json.h>

//#define TEST_ADD_JSON_LINE_BREAKS		1

#ifdef TEST_ADD_JSON_LINE_BREAKS
#define JSON_ENDL	<< "\n";
#else
#define JSON_ENDL	;
#endif

string json_escape(const string& str);

CCRESULT copy_error_to_output(const string& fn, const string& error, char *output, const uint32_t outsize, CCRESULT rc = -1);
CCRESULT copy_result_to_output(const string& fn, const string& result, char *output, const uint32_t outsize);
CCRESULT error_buffer_overflow(const string& fn, char *output, const uint32_t outsize, const unsigned need = 0);
CCRESULT error_requires_binary_buffer(const string& fn, char *output, const uint32_t outsize);
CCRESULT error_unexpected(const string& fn, char *output, const uint32_t outsize);
CCRESULT error_unexpected_key(const string& fn, const string& key, char *output, const uint32_t outsize);
CCRESULT error_missing_key(const string& fn, const string& key, char *output, const uint32_t outsize);
CCRESULT error_value_overflow(const string& fn, const string& key, unsigned nbits, const snarkfront::bigint_t& maxval, char *output, const uint32_t outsize);
CCRESULT error_invalid_numeric_char(const string& fn, const string& key, char c, char *output, const uint32_t outsize);
CCRESULT error_not_hex(const string& fn, const string& key, char *output, const uint32_t outsize);
CCRESULT error_invalid_value(const string& fn, const string& key, char *output, const uint32_t outsize);
CCRESULT error_input_end(const string& fn, char *output, const uint32_t outsize);
CCRESULT error_invalid_char(const string& fn, char *output, const uint32_t outsize);
CCRESULT error_unexpected_char(const string& fn, char *output, const uint32_t outsize);
CCRESULT error_checksum_mismatch(const string& fn, char *output, const uint32_t outsize);
CCRESULT error_not_array(const string& fn, const string& key, char *output, const uint32_t outsize);
CCRESULT error_not_array_objs(const string& fn, const string& key, char *output, const uint32_t outsize);
CCRESULT error_too_many_objs(const string& fn, const string& key, unsigned limit, char *output, const uint32_t outsize);
CCRESULT error_num_values(const string& fn, const string& key, unsigned limit, char *output, const uint32_t outsize);
CCRESULT error_invalid_tx_type(const string& fn, char *output, const uint32_t outsize);
CCRESULT error_invalid_binary_tx(const string& fn, char *output, const uint32_t outsize);
CCRESULT error_invalid_binary_tx_value(const string& fn, const string& msg, char *output, const uint32_t outsize);

const Json::Value * json_find(const Json::Value& root, const char *key);

CCRESULT parse_objid(const string& fn, const string& key, const string& sval, ccoid_t& objid, char *output, const uint32_t outsize);
CCRESULT parse_int_value(const string& fn, const string& key, const string& sval, unsigned nbits, snarkfront::bigint_t maxval, snarkfront::bigint_t& val, char *output, const uint32_t outsize, bool isexp = false);
