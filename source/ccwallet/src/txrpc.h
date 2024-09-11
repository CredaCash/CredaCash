/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * txrpc.h
*/

#pragma once

class DbConn;
class TxQuery;
class TxParams;

void estimate_donation(const TxParams& txparams, unsigned nout, unsigned nin, string& donation);

#define RPC_STDPARAMS	DbConn *dbconn, TxQuery& txquery, ostringstream& rstream
#define RPC_STDPARAMSAQ	RPC_STDPARAMS, bool &add_quotes
#define CP const

void cc_mint_threads(int nthreads, RPC_STDPARAMS);
void cc_mint_threads_shutdown();

void cc_mint(RPC_STDPARAMS);

void cc_unique_id_generate(CP string& prefix, unsigned random_bits, unsigned checksum_chars, RPC_STDPARAMS);
void cc_donation_estimate(unsigned type, unsigned nin, unsigned nout, RPC_STDPARAMSAQ);

void cc_send(bool async, CP string& ref_id, CP string& dest, CP snarkfront::bigint_t& amount, CP string& comment, CP string& comment_to, bool subfee, RPC_STDPARAMS);

void cc_transaction_cancel(CP string& txid, RPC_STDPARAMS);

void cc_list_change_destinations(RPC_STDPARAMS);

void cc_destination_poll(CP string& destination, unsigned polling_addresses, uint64_t last_receive_max, RPC_STDPARAMS);
void cc_mint_poll(RPC_STDPARAMS);

void cc_billets_poll_unspent(RPC_STDPARAMS);
void cc_billets_release_allocated(bool reset_balance, RPC_STDPARAMS);
void cc_billets_list_unspent(unsigned statuses, CP snarkfront::bigint_t& min_amount, RPC_STDPARAMS);

void cc_dump_secrets(unsigned type, uint64_t parent, uint64_t start, uint64_t count, RPC_STDPARAMS);
void cc_dump_transactions(uint64_t start, uint64_t count, bool show_billets, RPC_STDPARAMS);
void cc_dump_billets(uint64_t start, uint64_t count, bool show_spends, RPC_STDPARAMS);
void cc_dump_tx_build(RPC_STDPARAMS);
void cc_dump_exchange_requests(uint64_t start, uint64_t count, bool show_transactions, RPC_STDPARAMS);
void cc_dump_exchange_matches(uint64_t start, uint64_t count, bool show_requests, bool show_transactions, RPC_STDPARAMS);

void cc_exchange_query_mining_info(RPC_STDPARAMS);
void cc_exchange_query_requests(CP unsigned xcx_type, CP snarkfront::bigint_t& min_amount, CP snarkfront::bigint_t& max_amount, CP double& min_rate, CP double& base_costs, CP double& quote_costs, CP uint64_t base_asset, CP uint64_t quote_asset, CP string& foreign_asset, CP unsigned maxret, CP unsigned offset, CP unsigned flags, RPC_STDPARAMS);

void cc_exchange_requests_pending_totals(CP uint64_t base_asset, CP uint64_t quote_asset, CP string& foreign_asset, RPC_STDPARAMS);
void cc_exchange_request_info(const string& key, const string& strval, uint64_t intval, RPC_STDPARAMS);
void cc_exchange_match_info(uint64_t matchnum, RPC_STDPARAMS);

void cc_crosschain_request_create(int mode, CP string& ref_id, CP unsigned xcx_type, CP snarkfront::bigint_t& min_amount, CP snarkfront::bigint_t& max_amount, CP double& rate, CP double& costs, CP uint64_t quote_asset, CP string& foreign_asset, CP string& foreign_address, uint64_t expiration, CP double& wait_discount, RPC_STDPARAMS);
void cc_crosschain_match_action_list(double minutes, bool override_reminder_times, RPC_STDPARAMS);
void cc_crosschain_match_mark_paid(uint64_t matchnum, CP string& foreign_txid, double reminder_minutes, double minimum_advance_minutes, RPC_STDPARAMS);
void cc_crosschain_payment_claim(bool async, CP string& ref_id, uint64_t matchnum, double amount, CP string& foreign_block_id, string& foreign_txid, double reminder_minutes, double minimum_advance_minutes, RPC_STDPARAMS);

void cc_broadcast(CP string& ref_id, CP string& data, RPC_STDPARAMS);
