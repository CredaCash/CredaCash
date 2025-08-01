/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * jsoninternal.h
*/

#pragma once

#include "CCapi.h"

CCRESULT json_tx_create(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT json_tx_verify(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT json_tx_to_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT json_tx_dump(const string& fn, Json::Value& root, char *output, const uint32_t outsize);

CCRESULT json_tx_to_wire(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize);

CCRESULT json_tx_from_wire(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize);

CCRESULT json_work_reset(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize);

CCRESULT json_work_add(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize);

CCRESULT json_test_parse_number(const string& fn, Json::Value& root, char *output, const uint32_t outsize);