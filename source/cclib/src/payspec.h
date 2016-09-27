/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * payspec.h
*/

#pragma once

#include <jsoncpp/json/json.h>

#define SUPPORT_GENERATE_TEST_INPUTS	1	// build support for json "generate-test-inputs" command (used in burn-tx.py test script)

CCRESULT generate_master_secret(const string& fn, Json::Value& root, char *output, const uint32_t bufsize);

CCRESULT master_secret_to_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize);

CCRESULT hash_spend_secret(const string& fn, Json::Value& root, char *output, const uint32_t bufsize);

CCRESULT compute_address(const string& fn, Json::Value& root, char *output, const uint32_t bufsize);

CCRESULT payspec_from_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize);

CCRESULT payspec_to_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize);

CCRESULT generate_test_inputs(const string& fn, Json::Value& root, char *output, const uint32_t bufsize);
