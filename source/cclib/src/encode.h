/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * encode.h
*/

#pragma once

#include "encodings.h"

void base64_test(const unsigned char* encode_table, const unsigned char* decode_table, bool no_padding = false);

void base64_encode(const unsigned char* table, const string& sval, string& outs, bool no_padding = false);

unsigned decode_char(const unsigned char* table, const unsigned mod, unsigned char c);
