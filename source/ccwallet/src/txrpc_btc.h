/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * txrpc_btc.h
*/

#pragma once

#include <CCbigint.hpp>
#include <amounts.h>
#include <jsoncpp/json/json.h>

void btc_getbestblockhash(RPC_STDPARAMS);
void btc_getblock(CP string& blockhash, RPC_STDPARAMS);
void btc_getblockchaininfo(RPC_STDPARAMS);
void btc_getblockcount(RPC_STDPARAMSAQ);
void btc_getblockhash(int64_t index, RPC_STDPARAMS);
void btc_getblockheader(CP string& blockhash, RPC_STDPARAMS);
void btc_getchaintips(RPC_STDPARAMS);
void btc_gettxout(CP string& txid, int vout, RPC_STDPARAMS);
void btc_gettxoutproof(CP Json::Value& txids, CP string& blockhash, RPC_STDPARAMS);
void btc_gettxoutsetinfo(RPC_STDPARAMS);
void btc_verifychain(int checklevel, int64_t numblocks, RPC_STDPARAMSAQ);
void btc_verifytxoutproof(CP string& proof, RPC_STDPARAMS);
void btc_getinfo(RPC_STDPARAMS);
void btc_getmininginfo(RPC_STDPARAMS);
void btc_getconnectioncount(RPC_STDPARAMSAQ);
void btc_getnettotals(RPC_STDPARAMS);
void btc_getnetworkinfo(RPC_STDPARAMS);
void btc_getpeerinfo(RPC_STDPARAMS);
void btc_ping(RPC_STDPARAMSAQ);
void btc_validateaddress(CP string& addr, RPC_STDPARAMS);
void btc_abandontransaction(CP string& txid, RPC_STDPARAMS);
void btc_backupwallet(CP string& dest, RPC_STDPARAMS);
void btc_dumpwallet(CP string& dest, RPC_STDPARAMS);
void btc_encryptwallet(CP string& pass, RPC_STDPARAMS);
void btc_walletpassphrase(CP string& pass, RPC_STDPARAMS);
void btc_walletlock(RPC_STDPARAMS);
void btc_getaccount(CP string& addr, RPC_STDPARAMS);
void btc_getaccountaddress(CP string& acct, RPC_STDPARAMS);
void btc_getaddressesbyaccount(CP string& acct, RPC_STDPARAMS);
void btc_getbalance(CP string& acct, bool incwatch, RPC_STDPARAMSAQ);
void btc_getnewaddress(CP string& acct, RPC_STDPARAMS);
void btc_getreceivedbyaccount(CP string& acct, RPC_STDPARAMSAQ);
void btc_getreceivedbyaddress(CP string& addr, RPC_STDPARAMSAQ);
void btc_gettransaction(CP string& txid, bool incwatch, RPC_STDPARAMS);
void btc_getunconfirmedbalance(RPC_STDPARAMSAQ);
void btc_getwalletinfo(RPC_STDPARAMS);
void btc_listaccounts(bool incwatch, RPC_STDPARAMS);
void btc_listaddressgroupings(RPC_STDPARAMS);
void btc_listreceivedbyaccount(bool incempty, bool incwatch, RPC_STDPARAMS);
void btc_listreceivedbyaddress(bool incempty, bool incwatch, string addr, RPC_STDPARAMS);
void btc_listsinceblock(CP string& blockhash, bool incwatch, RPC_STDPARAMS);
void btc_listtransactions(CP string& acct, int count, int from, bool incwatch, RPC_STDPARAMS);
void btc_move(CP string& fromacct, CP string& toacct, CP snarkfront::bigint_t& amount, CP string& comment, RPC_STDPARAMSAQ);
void btc_sendfrom(CP string& fromacct, CP string& toaddr, CP snarkfront::bigint_t& amount, CP string& comment, CP string& comment_to, RPC_STDPARAMS);
void btc_setaccount(CP string& addr, CP string& acct, RPC_STDPARAMS);
