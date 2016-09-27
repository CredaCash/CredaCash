/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * txquery.h
*/

#pragma once

#include <jsoncpp/json/json.h>

CCRESULT tx_query_from_json(const string& fn, Json::Value& root, char *output, const uint32_t bufsize);
