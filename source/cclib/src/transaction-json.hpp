/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * transaction-json.hpp
*/

#pragma once

CCRESULT tx_params_from_json(const string& fn, Json::Value& root, bool allow_multi_secrets, SpendSecretParams& params, char *output, const uint32_t outsize);
CCRESULT tx_secrets_from_json(const string& fn, Json::Value& root, bool allow_multi_secrets, SpendSecretParams& params, SpendSecrets& secrets, bool no_precheck, char *output, const uint32_t outsize);
CCRESULT tx_input_common_from_json(const string& fn, Json::Value& root, TxIn& txin, char *output, const uint32_t outsize);
