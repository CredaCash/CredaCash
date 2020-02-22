/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * transaction.h
*/

#pragma once

#include "CCapi.h"
#include "CCbigint.hpp"

struct TxPay;
struct TxOut;
struct TxIn;
struct TxInPath;
struct SpendSecret;
struct SpendSecretParams;
struct AddressParams;
class CCObject;

void tx_init(TxPay& tx);
CCRESULT txpay_create_finish(const string& fn, TxPay& tx, char *output, const uint32_t outsize);
CCRESULT txpay_to_wire(const string& fn, const TxPay& tx, unsigned err_check, char *output, const uint32_t outsize, char *binbuf, const uint32_t binsize);

uint64_t txpay_param_level_from_wire(const CCObject *obj);

CCRESULT tx_from_wire(TxPay& tx, char *binbuf, const uint32_t binsize);

CCRESULT tx_dump(const TxPay& tx, char *output, const uint32_t outsize, const char *prefix = "");

void tx_dump_stream(ostream& os, const TxPay& tx, const char *prefix = "");
void txout_dump_stream(ostream& os, const TxOut& txout, const char *prefix = "");
void txin_dump_stream(ostream& os, const TxIn& txin, const TxInPath *path = NULL, const char *prefix = "");
void tx_dump_spend_secret_params_stream(ostream& os, const SpendSecretParams& params, const char *prefix = "");
void tx_dump_address_params_stream(ostream& os, const AddressParams& params, const char *prefix = "");
void tx_dump_spend_secret_stream(ostream& os, const SpendSecret& secret, unsigned index, const char *prefix = "");

void tx_amount_factors_init();
void tx_get_amount_factor(snarkfront::bigint_t& factor, unsigned exponent);
unsigned tx_amount_decode_exponent(uint64_t amount, unsigned exponent_bits);

void tx_amount_decode(uint64_t amount, snarkfront::bigint_t& result, bool is_donation, unsigned amount_bits, unsigned exponent_bits);
uint64_t tx_amount_encode(const snarkfront::bigint_t& amount, bool is_donation, unsigned amount_bits, unsigned exponent_bits, unsigned min_exponent = 0, unsigned max_exponent = -1, unsigned rounding = -1);

void tx_set_commit_iv(TxPay& tx);

void tx_commit_tree_hash_leaf(const snarkfront::bigint_t& commitment, const uint64_t leafindex, snarkfront::bigint_t& hash);
void tx_commit_tree_hash_node(const snarkfront::bigint_t& val1, const snarkfront::bigint_t& val2, snarkfront::bigint_t& hash, bool skip_final_knapsack);

CCRESULT tx_reset_work(const string& fn, uint64_t timestamp, char *binbuf, const uint32_t binsize);
CCRESULT tx_check_timestamp(uint64_t timestamp, unsigned allowance);
CCRESULT tx_set_work(const string& fn, unsigned proof_start, unsigned proof_count, uint64_t iter_count, uint64_t proof_difficulty, char *binbuf, const uint32_t binsize);
CCRESULT tx_set_work_internal(char *binbuf, const void *txhash, unsigned proof_start, unsigned proof_count, uint64_t iter_count, uint64_t proof_difficulty);
