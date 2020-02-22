/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * txquery.h
*/

#pragma once

#include "CCapi.h"

#include <CCbigint.hpp>

#include <jsoncpp/json/json.h>

CCRESULT tx_query_from_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize);

CCRESULT tx_query_parameters_create(const string& fn, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_address_create(const string& fn, uint64_t blockchain, const snarkfront::bigint_t& address, const uint64_t commitstart, const uint16_t maxret, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_serialnum_create(const string& fn, uint64_t blockchain, const snarkfront::bigint_t *serialnums, unsigned nserials, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_inputs_create(const string& fn, uint64_t blockchain, const uint64_t *commitnum, const unsigned ncommits, char *binbuf, const uint32_t binsize);
