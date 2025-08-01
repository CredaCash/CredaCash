/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * payspec.h
*/

#pragma once

#include <jsoncpp/json/json.h>

#ifdef CC_DLL_EXPORTS
#define SUPPORT_GENERATE_TEST_INPUTS	1	// build support for json "generate-test-inputs" command (used in burn-tx.py test script)
#else
#define SUPPORT_GENERATE_TEST_INPUTS	0
#endif

CCRESULT hash_passphrase(const string& passphrase, const bigint_t& salt, int millisec, uint64_t memory, uint64_t& iterations, bigint_t& result);

CCRESULT generate_random(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT generate_master_secret_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);
CCRESULT generate_master_secret(const string& fn, unsigned memory, unsigned millisec, const snarkfront::bigint_t& salt, char *output, const uint32_t outsize);

CCRESULT check_ascii_only(const string& fn, string& passphrase, char *output, const uint32_t outsize);

CCRESULT compute_master_secret_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);
CCRESULT compute_master_secret(const string& fn, const string& msspec, const string& passphrase, snarkfront::bigint_t& master_secret, char *output, const uint32_t outsize);

CCRESULT compute_secret(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT compute_address_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT encode_amount_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);
CCRESULT decode_amount_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT compute_amount_encyption_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT payspec_from_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT payspec_to_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT compute_serialnum_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT generate_test_inputs(const string& fn, Json::Value& root, char *output, const uint32_t outsize);
