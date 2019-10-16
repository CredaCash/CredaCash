/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * txrpc_btc.h
*/

#pragma once

#include "amounts.h"

#include <CCbigint.hpp>
#include <jsoncpp/json/json.h>

class DbConn;
class TxQuery;

void btc_getbestblockhash(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getblock(const string& blockhash, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getblockchaininfo(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getblockcount(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_getblockhash(int64_t index, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getblockheader(const string& blockhash, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getchaintips(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_gettxout(const string& txid, int vout, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_gettxoutproof(const Json::Value& txids, const string& blockhash, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_gettxoutsetinfo(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_verifychain(int checklevel, int64_t numblocks, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_verifytxoutproof(const string& proof, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getinfo(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getmininginfo(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getconnectioncount(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_getnettotals(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getnetworkinfo(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getpeerinfo(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_ping(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_estimatefee(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_validateaddress(const string& addr, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_abandontransaction(const string& txid, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_backupwallet(const string& dest, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_dumpwallet(const string& dest, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_encryptwallet(const string& pass, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_walletpassphrase(const string& pass, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_walletlock(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getaccount(const string& addr, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getaccountaddress(const string& acct, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getaddressesbyaccount(const string& acct, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getbalance(const string& acct, bool incwatch, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_getnewaddress(const string& acct, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getreceivedbyaccount(const string& acct, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_getreceivedbyaddress(const string& addr, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_gettransaction(const string& txid, bool incwatch, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_getunconfirmedbalance(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_getwalletinfo(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_listaccounts(bool incwatch, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_listaddressgroupings(DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_listreceivedbyaccount(bool incempty, bool incwatch, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_listreceivedbyaddress(bool incempty, bool incwatch, string addr, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_listsinceblock(const string& blockhash, bool incwatch, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_listtransactions(const string& acct, int count, int from, bool incwatch, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_listunspent(int minconf, int maxconf, const Json::Value& addrs, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_move(const string& fromacct, const string& toacct, const snarkfront::bigint_t& amount, const string& comment, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes);
void btc_sendfrom(const string& fromacct, const string& toaddr, const snarkfront::bigint_t& amount, const string& comment, const string& comment_to, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
void btc_setaccount(const string& addr, const string& acct, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream);
