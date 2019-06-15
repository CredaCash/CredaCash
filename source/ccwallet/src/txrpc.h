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
void cc_poll_destination(string destination, uint64_t last_receive_max, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
