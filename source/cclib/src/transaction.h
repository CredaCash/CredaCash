/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * transaction.h
*/

#pragma once

#include "CCapi.h"

CCRESULT tx_from_wire(struct TxPay& tx, const char *output, const uint32_t bufsize);
CCRESULT tx_dump(const struct TxPay& tx, char *output, const uint32_t bufsize);

CCRESULT tx_reset_work(const char *tx, uint64_t timestamp);
CCRESULT tx_check_timestamp(uint64_t timestamp, unsigned allowance);
CCRESULT tx_set_work(const char *tx, const void *txhash, unsigned proof_start, unsigned proof_count, uint64_t iter_count, uint64_t proof_difficulty);

CCRESULT tx_dump_stream(ostream &os, const struct TxPay& tx);

CCRESULT tx_commit_tree_hash_leaf(const snarkfront::bigint_t& commitment, const uint64_t& leafindex, snarkfront::bigint_t& hash);
CCRESULT tx_commit_tree_hash_node(const snarkfront::bigint_t& val1, const snarkfront::bigint_t& val2, snarkfront::bigint_t& hash);
