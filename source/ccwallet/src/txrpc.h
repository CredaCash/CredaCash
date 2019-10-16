/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * txrpc.h
*/

#pragma once

class DbConn;
class TxQuery;

void cc_mint_threads(int nthreads, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void cc_mint_threads_shutdown();

void cc_mint(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);

void cc_unique_id_generate(const string& prefix, unsigned random_bits, unsigned checksum_chars, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);

void cc_send(bool async, const string& ref_id, const string& addr, const snarkfront::bigint_t& amount, const string& comment, const string& comment_to, bool subfee, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);

void cc_poll_destination(const string& destination, unsigned polling_addresses, uint64_t last_receive_max, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void cc_poll_mint(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);

void cc_billets_poll_unspent(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void cc_billets_release_allocated(bool reset_balance, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);

void cc_dump_transactions(uint64_t start, uint64_t count, bool include_billets, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void cc_dump_billets(uint64_t start, uint64_t count, bool show_spends, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void cc_dump_tx_build(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
