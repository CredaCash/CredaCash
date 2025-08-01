/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * txquery.h
*/

#pragma once

#include "CCapi.h"

#include <CCbigint.hpp>
#include <CCobjdefs.h>

#include <jsoncpp/json/json.h>

#define TX_QUERY_XREQS_FLAG_ONLY_PENDING_MATCHED		2
#define TX_QUERY_XREQS_FLAG_INCLUDE_PENDING_MATCHED		1

CCRESULT tx_query_from_json(const string& fn, Json::Value& root, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize);

CCRESULT tx_query_parameters_create(const string& fn, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_address_create(const string& fn, uint64_t blockchain, const snarkfront::bigint_t& address, const uint64_t commitstart, const uint16_t maxret, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_serialnum_create(const string& fn, uint64_t blockchain, const snarkfront::bigint_t *serialnums, unsigned nserials, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_inputs_create(const string& fn, uint64_t blockchain, const uint64_t *commitnum, const unsigned ncommits, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_xreqs_create(const string& fn, unsigned xcx_type, const snarkfront::bigint_t& min_amount, const snarkfront::bigint_t& max_amount, const double& min_rate, const uint64_t base_asset, const uint64_t quote_asset, const string& foreign_asset, const uint16_t maxret, const uint16_t offset, unsigned flags, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_xmatch_objid_create(const string& fn, uint64_t blockchain, const ccoid_t& objid, const uint16_t maxret, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_xmatch_xreqnum_create(const string& fn, uint64_t blockchain, uint64_t xreqnum, uint64_t xmatchnum_start, const uint16_t maxret, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_xmatch_xmatchnum_create(const string& fn, uint64_t blockchain, uint64_t xmatchnum, char *binbuf, const uint32_t binsize);
CCRESULT tx_query_xmining_info_create(const string& fn, char *binbuf, const uint32_t binsize);
