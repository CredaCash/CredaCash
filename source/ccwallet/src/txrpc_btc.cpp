/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * txrpc_btc.cpp
*/

#include "ccwallet.h"
#include "txrpc.h"
#include "txrpc_btc.h"
#include "rpc_errors.hpp"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "totals.hpp"
#include "btc_block.hpp"
#include "txparams.hpp"
#include "txquery.hpp"
#include "walletdb.hpp"

#include <CCmint.h>
#include <xtransaction.hpp>
#include <jsonutil.h>
#include <blake2/blake2.h>

#define WALLET_VERSION	1

#define TRACE_TX	(g_params.trace_txrpc)

//#define TEST_STUB_BTC	1

#ifndef TEST_STUB_BTC
#define TEST_STUB_BTC	0	// don't compile stubs
#endif

using namespace snarkfront;

const RPC_Exception txrpc_server_error((RPCErrorCode)(RPC_SERVER_ERROR), "Server error");
const RPC_Exception txrpc_wallet_error(RPCErrorCode(RPC_WALLET_INTERNAL_ERROR), "Wallet internal error");
const RPC_Exception txrpc_wallet_db_error(RPCErrorCode(RPC_WALLET_INTERNAL_ERROR), "Wallet database error");
const RPC_Exception txrpc_not_synced_error((RPCErrorCode)(RPC_SYNC_ERROR), "Server not synchronized with blockchain");
const RPC_Exception txrpc_simulated_error(RPC_WALLET_SIMULATED_ERROR, "Simulated (test) error");
const RPC_Exception txrpc_shutdown_error(RPC_CLIENT_NOT_CONNECTED, "Shutting down");

const RPC_Exception txrpc_not_implemented_error(RPC_INVALID_PARAMS, "Method not implemented");
// ...RPC_Exception txrpc_invalid_txid_error(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");	// bitcoin's error
const RPC_Exception txrpc_invalid_txid_error(RPC_INVALID_PARAMETER, "Invalid transaction id");						// slight departure from bitcoin
const RPC_Exception txrpc_txid_not_found(RPC_INVALID_ADDRESS_OR_KEY, "Non-wallet transaction id");					// slight departure from bitcoin
const RPC_Exception txrpc_accounts_not_implemented_error(RPC_WALLET_INSUFFICIENT_FUNDS, "Accounts not implemented");

const RPC_Exception txrpc_block_height_range_err(RPC_INVALID_PARAMETER, "Block height out of range");
const RPC_Exception txrpc_block_not_found_err(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
const RPC_Exception txrpc_tx_not_in_block(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
// ...RPC_Exception txrpc_invalid_address(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");			// bitcoin's error
const RPC_Exception txrpc_invalid_address(RPC_INVALID_PARAMETER, "Invalid address");				// slight departure from bitcoin
const RPC_Exception txrpc_address_not_found(RPC_INVALID_ADDRESS_OR_KEY, "Non-wallet address");		// slight departure from bitcoin
const RPC_Exception txrpc_insufficient_funds(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");
const RPC_Exception txrpc_tx_rejected(RPC_VERIFY_REJECTED, "Transaction not allowed or invalid");

const RPC_Exception txrpc_tx_timeout(RPC_TRANSACTION_TIMEOUT, "Time limit expired while building transaction");
const RPC_Exception txrpc_tx_mismatch(RPC_TRANSACTION_MISMATCH, "Transaction destination or amount does not match prior transaction with same reference id");
const RPC_Exception txrpc_tx_expired(RPC_TRANSACTION_EXPIRED, "Transaction expired");

const RPC_Exception txrpc_match_not_found(RPC_INVALID_ADDRESS_OR_KEY, "Match number not found");

static int master_secret_hash(DbConn *dbconn, bigint_t& masterhash)
{
	masterhash = 0UL;

	Secret secret;
	auto rc = dbconn->SecretSelectId(MAIN_ROOT_SECRET_ID, secret);
	if (rc) return rc;

	rc = blake2s(&masterhash, 160/8, NULL, 0, &secret.value, secret.ValueBytes());
	CCASSERTZ(rc);

	return 0;
}

void btc_getbestblockhash(RPC_STDPARAMS)											// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getbestblockhash";

	// returns "block hash" = block level

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rstream << setw(64) << setfill('0') << hex << btc_block << dec;
}

void btc_getblock(CP string& blockhash, RPC_STDPARAMS)										// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getblock " << blockhash;

	if (TEST_STUB_BTC)
	{
		if (blockhash.length() < 10)
			throw txrpc_block_not_found_err;

		rstream <<
		"{"
	/*s*/"\"hash\":\"" << blockhash <<"\","
	/*s*/"\"confirmations\":1,"
	/*s*/"\"strippedsize\":123456,"
	/*s*/"\"size\":123456,"
	/*s*/"\"weight\":1646474,"
	/*s*/"\"height\":1234,"
	/*s*/"\"version\":4294967295,"
	/*s*/"\"versionHex\":\"" << string(8,'F') <<"\","
	/*s*/"\"merkleroot\":\"" << string(64,'0') <<"\","
	/*s*/"\"tx\":"
		"["
	/*s*/"\"" << string(64,'0') <<"\","
	/*s*/"\"" << string(64,'0') <<"\""
		"],"
	/*s*/"\"time\":1480641638,"
	/*s*/"\"mediantime\":1480636747,"
	/*s*/"\"nonce\":" << string(10,'1') << ","
	/*s*/"\"bits\":\"1d00ffff\","
	/*s*/"\"difficulty\":1.0,"
	/*s*/"\"chainwork\":\"" << string(64,'1') <<"\","
	/*s*/"\"previousblockhash\":\"" << string(64,'0') <<"\""
		"}";
	}

	throw txrpc_not_implemented_error;
}

void btc_getblockchaininfo(RPC_STDPARAMS)										// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getblockchaininfo";

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rstream <<
	"{"
	"\"chain\":\"" << txparams.blockchain << "\","
	"\"blocks\":" << btc_block << ","
	"\"headers\":" << btc_block << ","
	"\"bestblockhash\":\"" << setw(64) << setfill('0') << hex << btc_block << dec << "\","
	"\"difficulty\":1.0,"
	"\"mediantime\":" << unixtime() << ","
	"\"verificationprogress\":1000,"
	"\"chainwork\":\"" << string(16,'0') << "1" << string(47,'0') << "\","
	"\"pruned\":false,"
	"\"softforks\":[]"
	"}";
}

void btc_getblockcount(RPC_STDPARAMSAQ)											// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getblockcount";

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr <<

R"(For compatibility with bitcoin, the blockcount reported by the wallet is synthetically-generated so that all cleared
transactions will appear to be confirmed by at least N blocks (where N is usually set to at least 6). The generated
blockcount is local to the wallet and will not match the value shown by another wallet.)"

		"\n" << endl;
	}

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rstream << btc_block;
	add_quotes = false;
}

void btc_getblockhash(int64_t index, RPC_STDPARAMS)								// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getblockhash " << index;

	if (0 && TEST_STUB_BTC)
	{
		if (index < 0 || index > 123456)
			throw txrpc_block_height_range_err;

		rstream << string(64,'0');
	}

	auto btc_block = g_btc_block.FinishCurrentBlock();

	if (index < 0 || (uint64_t)index > btc_block)
		throw txrpc_block_height_range_err;

	// returns "block hash" = block level

	rstream << setw(64) << setfill('0') << hex << index << dec;
}

void btc_getblockheader(CP string& blockhash, RPC_STDPARAMS)									// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getblockheader " << blockhash;

	if (TEST_STUB_BTC)
	{
		if (blockhash.length() < 10)
			throw txrpc_block_not_found_err;

		rstream <<
		"{"
	/*s*/"\"hash\":\"" << blockhash <<"\","
	/*s*/"\"confirmations\":1,"
	/*s*/"\"height\":12345,"
	/*s*/"\"version\":4294967295,"
	/*s*/"\"versionHex\":\"" << string(8,'F') <<"\","
	/*s*/"\"merkleroot\":\"" << string(64,'0') <<"\","
	/*s*/"\"time\":1480652159,"
	/*s*/"\"mediantime\":1480648088,"
	/*s*/"\"nonce\":" << string(10,'1') << ","
	/*s*/"\"bits\":\"1d00ffff\","
	/*s*/"\"difficulty\":1.0,"
	/*s*/"\"chainwork\":\"" << string(64,'1') <<"\","
	/*s*/"\"previousblockhash\":\"" << string(64,'0') <<"\","
	/*s*/"\"nextblockhash\":\"" << string(64,'0') <<"\""		// only present if confirmations > 1
		"}";
	}

	throw txrpc_not_implemented_error;
}

void btc_getchaintips(RPC_STDPARAMS)												// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getchaintips";

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rstream <<
	"["
	"{"
	"\"height\":" << btc_block << ","
	"\"hash\":\"" << setw(64) << setfill('0') << hex << btc_block << dec << "\","
	"\"branchlen\":0,"
	"\"status\":\"active\""
	"}"
	"]";
}

void btc_gettxout(CP string& txid, int vout, RPC_STDPARAMS)									// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_gettxout " << txid << " vout " << vout;

	throw txrpc_not_implemented_error;
}

void btc_gettxoutproof(CP Json::Value& txids, CP string& blockhash, RPC_STDPARAMS)			// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_gettxoutproof blockhash " << blockhash;

	if (TEST_STUB_BTC)
	{
		throw txrpc_tx_not_in_block;
	}

	throw txrpc_not_implemented_error;
}

void btc_gettxoutsetinfo(RPC_STDPARAMS)														// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_gettxoutsetinfo";

	throw txrpc_not_implemented_error;
}

void btc_verifychain(int checklevel, int64_t numblocks, RPC_STDPARAMSAQ)						// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_verifychain checklevel " << checklevel << " numblocks " << numblocks;

	if (TEST_STUB_BTC)
	{
		rstream << "true";
		add_quotes = false;
	}

	throw txrpc_not_implemented_error;
}

void btc_verifytxoutproof(CP string& proof, RPC_STDPARAMS)									// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_verifytxoutproof " << proof;

	throw txrpc_not_implemented_error;
}

void btc_getinfo(RPC_STDPARAMS)													// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getinfo";

	TxParams txparams;
	auto param_rc = g_txparams.FetchParams(txparams, txquery, true);

	string balance, fee;

	amtint_t amounti = 0UL;
	Total::GetTotalBalance(dbconn, true, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false);

	amount_to_string(0, amounti, balance);

	if (param_rc)
		fee = "0";
	else
		estimate_donation(txparams, 2, 2, fee);

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rstream <<
	"{"
	"\"version\":" << (txparams.server_version >> 32) << ","
	"\"protocolversion\":" << (txparams.protocol_version >> 32) << ","
	"\"walletversion\":" << WALLET_VERSION << ","
	"\"balance\":" << balance << ","
	"\"blocks\":" << btc_block << ","
	"\"timeoffset\":" << txparams.clock_diff << ","
	"\"connections\":" << (param_rc || txparams.NotConnected() ? 0 : 20) << ","
	"\"difficulty\":1.0,";

	if (!param_rc) // don't say anything about testnet unless we know
		rstream << "\"testnet\":" << truefalse(IsTestnet(txparams.blockchain)) << ",";

	rstream <<
	"\"keypoololdest\":1514764800,"
	"\"keypoolsize\":200,"
	"\"unlocked_until\":7258075199,"
	"\"paytxfee\":" << fee << ","
	"\"relayfee\":" << fee << ","
	"\"errors\":\"\""		// !!!						// (string) any error messages
	"}";
}

void btc_getmininginfo(RPC_STDPARAMS)											// implemented mostly trivially
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getmininginfo";

	TxParams txparams;
	auto param_rc = g_txparams.GetParams(txparams, txquery);

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "CredaCash does not support proof-of-work mining.\n" << endl;
	}

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rstream <<
	"{"
	"\"blocks\":" << btc_block << ","
	"\"currentblocksize\":0,"
	"\"currentblockweight\":0,"
	"\"currentblocktx\":0,"
	"\"difficulty\":1.0,"
	"\"errors\":\"\","
	"\"networkhashps\":1.0,"
	"\"pooledtx\":500,";

	if (!param_rc) // don't say anything about testnet unless we know
		rstream << "\"testnet\":" << truefalse(IsTestnet(txparams.blockchain)) << ",";

	rstream <<
	"\"chain\":\"" << txparams.blockchain << "\""
	"}";
}

void btc_getconnectioncount(RPC_STDPARAMSAQ)													// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getconnectioncount";

	if (TEST_STUB_BTC)
	{
		rstream << "8";
		add_quotes = false;
	}

	throw txrpc_not_implemented_error;
}

void btc_getnettotals(RPC_STDPARAMS)															// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getnettotals";

	if (TEST_STUB_BTC)
	{
		static unsigned count = 123456;
		++count;

		rstream <<
		"{"
	/*s*/"\"totalbytesrecv\":123456,"
	/*s*/"\"totalbytessent\":123456,"
	/*s*/"\"timemillis\":" << count << ","
	/*s*/"\"uploadtarget\":{"
	/*s*/"\"timeframe\":86400,"
	/*s*/"\"target\":0,"
	/*s*/"\"target_reached\":false,"
	/*s*/"\"serve_historical_blocks\":true,"
	/*s*/"\"bytes_left_in_cycle\":0,"
	/*s*/"\"time_left_in_cycle\":0"
		"}"
		"}";
	}

	throw txrpc_not_implemented_error;
}

void btc_getnetworkinfo(RPC_STDPARAMS)														// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getnetworkinfo";

	throw txrpc_not_implemented_error;
}

void btc_getpeerinfo(RPC_STDPARAMS)															// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getpeerinfo";

	throw txrpc_not_implemented_error;
}

void btc_ping(RPC_STDPARAMSAQ)													// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_ping";

	if (0 && TEST_STUB_BTC)
	{
		rstream << "";
	}

	auto t0 = ccticks();

	TxParams txparams;

	auto rc = g_txparams.FetchParams(txparams, txquery, true);	// force fetch
	if (rc) throw txrpc_server_error;

	auto elapsed = ccticks_elapsed(t0, ccticks());

	// bitcoin sends a ping request to each peer, which updates the pingtime returned by getpeerinfo
	// in response to the ping, bitcoin returns a null response
	// CredaCash breaks compatibility with bitcoin's ping by returning the server ping time instead of a null response

	rstream << elapsed / (float)CCTICKS_PER_SEC;
	add_quotes = false;
}

void btc_validateaddress(CP string& addr, RPC_STDPARAMS)							// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_validateaddress " << addr;

	uint64_t dest_chain;
	bigint_t destination;
	auto rc = Secret::DecodeDestination(addr, dest_chain, destination);
	if (rc)
	{
		rstream << "{\"isvalid\":false}";

		return;
	}

	Secret secret;
	auto notfound = dbconn->SecretSelectSecret(&destination, TX_INPUT_BYTES, secret);
	if (notfound < 0) throw txrpc_wallet_db_error;

	bool watchonly = secret.TypeIsWatchOnlyDestination();
	bool ismine = (watchonly || secret.type == SECRET_TYPE_SPENDABLE_DESTINATION);

	bigint_t masterhash;
	rc = master_secret_hash(dbconn, masterhash);
	if (rc < 0) throw txrpc_wallet_error;

	/*
	ret.push_back(Pair("isvalid", isValid));
	ret.push_back(Pair("address", currentAddress));
	ret.push_back(Pair("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end())));
	ret.push_back(Pair("ismine", (mine & ISMINE_SPENDABLE) ? true : false));
	ret.push_back(Pair("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false));
	obj.push_back(Pair("isscript", false));
	if GetPubKey:
		obj.push_back(Pair("pubkey", HexStr(vchPubKey)));
		obj.push_back(Pair("iscompressed", vchPubKey.IsCompressed()));
	if mapped:
		ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest].name));
	if in metadata:
		ret.push_back(Pair("timestamp", it->second.nCreateTime));
	if in metadata:
		ret.push_back(Pair("hdkeypath", it->second.hdKeypath));
		ret.push_back(Pair("hdmasterkeyid", it->second.hdMasterKeyID.GetHex()));
	*/

	rstream <<
	"{"
	"\"isvalid\":true,"
	"\"address\":\"" << addr <<"\","
	//"\"scriptPubKey\":\"" << string(64,'0') <<"\","
	"\"ismine\":" << truefalse(ismine) << ","
	"\"iswatchonly\":" << truefalse(watchonly) << ","
	"\"isscript\":false";
	if (!ismine)
		rstream << "";
	else
	{
		rstream <<
		","
		"\"account\":\"\","
		"\"timestamp\":" << secret.create_time;
	}
	if (!watchonly)
	{
		rstream <<
		","
		//"\"pubkey\":\"" << string(64,'0') << "\","
		//"\"iscompressed\":true,"
		//"\"hdkeypath\":\"m/0'/0'/4'\","				// (string, optional) The HD keypath if the key is HD and available
		"\"hdmasterkeyid\":\"" << setw(40) << setfill('0') << hex << masterhash << dec <<"\"";
	}
	rstream <<
	","
	"\"cc.blockchain\":" << dest_chain << ","
	"\"cc.destination\":\"0x" << hex << destination << dec << "\""
	"}";
}

void btc_abandontransaction(CP string& txid, RPC_STDPARAMS)						// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_abandontransaction " << txid;

	if (0 && TEST_STUB_BTC)
	{
		if (txid.length() < 10)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");

		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");
	}

	bigint_t address, commitment;
	uint64_t id, dest_chain;

	auto rc = Transaction::DecodeBtcTxid(txid, id, dest_chain, address, commitment);
	if (rc > 0) throw txrpc_txid_not_found;
	if (rc) throw txrpc_invalid_txid_error;

	if (!id)
	{
		Billet bill;

		rc = dbconn->BilletSelectTxid(&address, &commitment, bill);
		if (rc > 0) throw txrpc_txid_not_found;
		if (rc) throw txrpc_wallet_db_error;

		id = bill.create_tx;
	}

	Transaction::AbandonTx(dbconn, id, dest_chain);

	//!!! what to return?
}

void btc_backupwallet(CP string& dest, RPC_STDPARAMS)							// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_backupwallet " << dest;

	if (0 && TEST_STUB_BTC)
	{
		if (dest.length() < 10)
			throw RPC_Exception(RPC_WALLET_ERROR, "Error: Wallet backup failed!");

		rstream << "";
	}

	auto rc = dbconn->BackupDb(dest.c_str());

	if (rc)
			throw RPC_Exception(RPC_WALLET_ERROR, "Error: Wallet backup failed!");

	rstream << "";
}

void btc_dumpwallet(CP string& dest, RPC_STDPARAMS)											// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_dumpwallet " << dest;

	if (TEST_STUB_BTC)
	{
		if (dest.length() < 10)
			RPC_Exception(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

		rstream << "";
	}

	throw txrpc_not_implemented_error;
}

void btc_encryptwallet(CP string& pass, RPC_STDPARAMS)										// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_encryptwallet";

	//need to implement: error code: -13; error message: Error: Please enter the wallet passphrase with walletpassphrase first.

	if (TEST_STUB_BTC)
	{
		if (pass.length() < 10)
			throw RPC_Exception(RPC_WALLET_WRONG_ENC_STATE, "running with an encrypted wallet, but encryptwallet was called.");	// in bitcoin, this is checked before the parameters are checked

		rstream << "wallet encrypted";
	}

	throw txrpc_not_implemented_error;
}

void btc_walletpassphrase(CP string& pass, RPC_STDPARAMS)									// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_walletpassphrase";

	if (TEST_STUB_BTC)
	{
		if (pass.length() < 10)
			throw RPC_Exception(RPC_WALLET_PASSPHRASE_INCORRECT, "The wallet passphrase entered was incorrect.");

		rstream << "";
	}

	throw txrpc_not_implemented_error;
}

void btc_walletlock(RPC_STDPARAMS)															// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_walletlock";

	if (TEST_STUB_BTC)
	{
		if (RandTest(2))
			throw RPC_Exception(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

		rstream << "";
	}

	throw txrpc_not_implemented_error;
}

void btc_getaccount(CP string& addr, RPC_STDPARAMS)								// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getaccount " << addr;

	if (0 && TEST_STUB_BTC)
	{
		if (addr.length() < 10)
			throw txrpc_invalid_address;

		rstream << "";
	}

	uint64_t dest_chain;
	bigint_t destination;
	auto rc = Secret::DecodeDestination(addr, dest_chain, destination);
	if (rc) throw txrpc_invalid_address;

	// !!! for now, always return account ""

	rstream << "";
}

void btc_getaccountaddress(CP string& acct, RPC_STDPARAMS)									// NOT_implemented !!! IMPLEMENT?
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getaccountaddress " << acct;

	if (TEST_STUB_BTC)
	{
		rstream << string(64,'0');
	}

	throw txrpc_not_implemented_error;
}

void btc_getaddressesbyaccount(CP string& acct, RPC_STDPARAMS)								// NOT_implemented !!! IMPLEMENT?
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getaddressesbyaccount " << acct;

	if (TEST_STUB_BTC)
	{
		if (acct.length())
		{
			rstream << "[]";
		}
		else
		{
			rstream <<
			"["
		/*s*/"\"" << string(64,'0') <<"\","
		/*s*/"\"" << string(64,'0') <<"\""
			"]";
		}
	}

	throw txrpc_not_implemented_error;
}

void btc_getbalance(CP string& acct, bool incwatch, RPC_STDPARAMSAQ)				// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getbalance " << acct << " incwatch " << incwatch;

	// account "" = default account
	// account "*" = sum of all accounts in wallet
	// incwatch true = include balances in watch-only addresses (see 'importaddress')
	// if account does not exist, bitcoin returns 0.0

	amtint_t amounti = 0UL;
	string balance;

	// TODO: implement accounts

	if (acct == "" || acct == "*")	// !!! Note: when accounts are implemented, "" is not the same as "*".
		Total::GetTotalBalance(dbconn, true, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, incwatch);

	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream << balance;
	add_quotes = false;
}

void btc_getnewaddress(CP string& acct, RPC_STDPARAMS)							// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getnewaddress " << acct;

	// creates a destination
	// TODO: if acct does not exist, it is created

	//	acct must equal "" in this release
	if (acct != "")
		throw txrpc_accounts_not_implemented_error;

	Secret destination;

	destination.CreateNewDestination(dbconn, txquery, g_params.polling_addresses);

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr <<

R"(Generating a new unique CredaCash destination. This destination can be used by a single counterparty to send multiple
transactions to this wallet. This value should be kept private between you and your counterparty, since it can also be
used to decypt the amount of every transaction sent to this destination. To maintain privacy, you must generate a new
unique destination for each counterparty who wishes to send you transactions.)"

		"\n" << endl;
	}

	rstream << destination.EncodeDestination();
}

void btc_getreceivedbyaccount(CP string& acct, RPC_STDPARAMSAQ)					// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getreceivedbyaccount " << acct;

	// returns the total amount received by addresses with <account>
	// see getreceivedbyaddress
	// !!! TBD: include watch?

	amtint_t amounti;
	string balance;

	// if account does not exist, bitcoin returns 0.0
	// Note: CredaCash extends this function to support acct == "*" which gives the amount received by all addresses

	if (acct == "*")
		Total::GetTotalBalance(dbconn, true, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_RECEIVED, false, false);
	else if (acct == "")
		Total::GetTotalBalance(dbconn, true, amounti, TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_RECEIVED, false, false);

	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream << balance;
	add_quotes = false;
}

void btc_getreceivedbyaddress(CP string& addr, RPC_STDPARAMSAQ)					// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getreceivedbyaddress " << addr;

	// returns the total amount received by the address
	// in bitcoin, amounts sent to self count as received
	// in bitcoin, mining fees (like change) do not count as amounts received
	// Note: CredaCash considers mint amounts and witness donations to be amounts received

	if (0 && TEST_STUB_BTC)
	{
		if (addr.length() < 10)
			throw txrpc_invalid_address;

		rstream << "123.00000000";
		add_quotes = false;
	}

	uint64_t dest_chain;
	bigint_t destination;
	auto rc = Secret::DecodeDestination(addr, dest_chain, destination);
	if (rc) throw txrpc_invalid_address;

	Secret secret;
	rc = dbconn->SecretSelectSecret(&destination, TX_INPUT_BYTES, secret);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (rc) throw txrpc_address_not_found;

	amtint_t amounti;
	string balance;

	Total::GetTotalBalance(dbconn, true, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_RECEIVED, false, false, secret.id);

	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream << balance;
	add_quotes = false;
}

static void _btc_list_tx(DbConn *dbconn, Transaction& tx, unsigned index, uint64_t btc_block, bool incwatch, ostringstream& rstream)
{
	/*
    entry.push_back(Pair("confirmations", confirms));
    if (wtx.IsCoinBase())
        entry.push_back(Pair("generated", true));
    if (confirms > 0)
    {
        entry.push_back(Pair("blockhash", wtx.hashBlock.GetHex()));
        entry.push_back(Pair("blockindex", wtx.nIndex));
        entry.push_back(Pair("blocktime", mapBlockIndex[wtx.hashBlock]->GetBlockTime()));
    } else {
        entry.push_back(Pair("trusted", wtx.IsTrusted()));
    }
    uint256 hash = wtx.GetHash();
    entry.push_back(Pair("txid", hash.GetHex()));
    UniValue conflicts(UniValue::VARR);
    BOOST_FOREACH(const uint256& conflict, wtx.GetConflicts())
        conflicts.push_back(conflict.GetHex());
    entry.push_back(Pair("walletconflicts", conflicts));
    entry.push_back(Pair("time", wtx.GetTxTime()));
    entry.push_back(Pair("timereceived", (int64_t)wtx.nTimeReceived));
    entry.push_back(Pair("bip125-replaceable", rbfStatus));
    BOOST_FOREACH(const PAIRTYPE(string,string)& item, wtx.mapValue)
        entry.push_back(Pair(item.first, item.second));
	*/

	if (tx.status == TX_STATUS_CONFLICTED)
	{
		rstream <<
			"\"confirmations\":-201";
	}
	else if (tx.status == TX_STATUS_CLEARED && btc_block >= tx.btc_block)
	{
		rstream <<
			"\"confirmations\":" << btc_block - tx.btc_block + 1 <<
			",\"blockhash\":\"" << setw(64) << setfill('0') << hex << tx.btc_block << dec << "\""
			",\"blockindex\":0"
			",\"blocktime\":" << tx.create_time;
	}
	else
	{
		rstream <<
			"\"confirmations\":0"
			",\"trusted\":" << truefalse(tx.WeSent(incwatch)
									&& (tx.type == CC_TYPE_TXPAY || tx.type == CC_TYPE_MOVE || Xtx::TypeIsXtx(tx.type))
									&& (tx.status >= TX_STATUS_PENDING && tx.status <= TX_STATUS_MOVED));
	}

	if (tx.type == CC_TYPE_MINT)
		rstream << ",\"generated\":true";

	rstream <<
		",\"cc.type\":" << tx.type <<
		",\"cc.type-label\":\"" << tx.TypeString() << "\""
		",\"cc.status\":" << tx.status <<
		",\"cc.status-label\":\"" << tx.StatusString() << "\""
		",\"txid\":\"" << tx.GetBtcTxid() << "\""
		",\"time\":" << tx.create_time <<
		",\"timereceived\":" << tx.create_time <<
		",\"bip125-replaceable\":\"no\""
		",";

	_btc_list_conflicts(dbconn, tx, rstream);


	//@@! TODO: btc wtx.mapValue items:
	// "comment" set by sendtoaddress, sendfrom, sendmany
	// "to" set by sendtoaddress, sendfrom

}

void _btc_list_conflicts(DbConn *dbconn, Transaction& tx, ostringstream& rstream)
{
	set<uint64_t> conflicts;

	rstream << "\"walletconflicts\":[";

	// unlike bitcoin, report conflicts only for conflicted and pending tx's
	for (unsigned i = 0; i < tx.nin && (tx.status == TX_STATUS_CONFLICTED || tx.status == TX_STATUS_PENDING); ++i)
	{
		Billet& bill = tx.input_bills[i];

		uint64_t tx_id = 0;

		while (tx_id <= INT64_MAX)
		{
			auto rc = dbconn->BilletSpendSelectBillet(bill.id, tx_id);
			if (rc < 0) throw txrpc_wallet_db_error;

			if (rc)
				break;

			if (tx_id != tx.id && !conflicts.count(tx_id))
			{
				Transaction tx2;

				rc = tx2.ReadTx(dbconn, tx_id);
				if (rc) throw txrpc_wallet_db_error;

				if (conflicts.empty())
					rstream << "\"";
				else
					rstream << ",\"";

				rstream << tx2.GetBtcTxid() << "\"";

				conflicts.insert(tx_id);
			}

			++tx_id;
		}
	}

	if (tx.status == TX_STATUS_CONFLICTED && conflicts.empty())
		rstream << "0]";	// this wallet doesn't contain the conflicting tx, but report something
	else
		rstream << "]";
}

static void _btc_list_sent(Transaction& tx, unsigned index, ostringstream& rstream)
{
	/*
	if(involvesWatchonly || (::IsMine(*pwalletMain, s.destination) & ISMINE_WATCH_ONLY))
		entry.push_back(Pair("involvesWatchonly", true));
	entry.push_back(Pair("account", strSentAccount));
	MaybePushAddress(entry, s.destination);
	entry.push_back(Pair("category", "send"));
	entry.push_back(Pair("amount", ValueFromAmount(-s.amount)));
	if (pwalletMain->mapAddressBook.count(s.destination))
		entry.push_back(Pair("label", pwalletMain->mapAddressBook[s.destination].name));
	entry.push_back(Pair("vout", s.vout));
	entry.push_back(Pair("fee", ValueFromAmount(-nFee)));
	entry.push_back(Pair("abandoned", wtx.isAbandoned()));
	ret.push_back(entry);
	*/

	rstream	<< "\"account\":\"" << json_escape(tx.output_accounts[index].name) << "\"";

	if (tx.InputsInvolveWatchOnly() || tx.output_destinations[index].TypeIsWatchOnlyDestination())
		rstream << ",\"involvesWatchonly\":true";

	if (tx.output_destinations[index].id)
		rstream << ",\"address\":\"" << tx.output_destinations[index].EncodeDestination() << "\"";

	rstream << ",\"category\":\"send\"";

	string amount;

	if (tx.output_bills[index].asset)
		rstream << ",\"amount\":0";
	else
	{
		amount_to_string(tx.output_bills[index].asset, tx.output_bills[index].amount, amount);
		rstream << ",\"amount\":-" << amount;
	}

	if (tx.output_destinations[index].label[0])
		rstream << ",\"label\":\"" << tx.output_destinations[index].label << "\"";

	amount_to_string(0, tx.adj_donations[index], amount);

	rstream <<
		",\"vout\":0"
		",\"fee\":" << (tx.adj_donations[index] ? "-" : "") << amount <<
		",\"abandoned\":" << truefalse(tx.status == TX_STATUS_ABANDONED || tx.status == TX_STATUS_CONFLICTED || tx.status == TX_STATUS_ERROR);
}

static void _btc_list_received(Transaction& tx, unsigned index, uint64_t btc_block, ostringstream& rstream)
{
	/*
	if(involvesWatchonly || (::IsMine(*pwalletMain, r.destination) & ISMINE_WATCH_ONLY))
		entry.push_back(Pair("involvesWatchonly", true));
	entry.push_back(Pair("account", account));
	MaybePushAddress(entry, r.destination);
	if (wtx.IsCoinBase())
	{
		if (wtx.GetDepthInMainChain() < 1)
			entry.push_back(Pair("category", "orphan"));
		else if (wtx.GetBlocksToMaturity() > 0)
			entry.push_back(Pair("category", "immature"));
		else
			entry.push_back(Pair("category", "generate"));
	}
	else
	{
		entry.push_back(Pair("category", "receive"));
	}
	entry.push_back(Pair("amount", ValueFromAmount(r.amount)));
	if (pwalletMain->mapAddressBook.count(r.destination))
		entry.push_back(Pair("label", account));
	entry.push_back(Pair("vout", r.vout));
	*/

	rstream << "\"account\":\"" << json_escape(tx.output_accounts[index].name) << "\"";

	bool ischange = (tx.output_bills[index].flags & BILL_IS_CHANGE);
	if (ischange)
		rstream << ",\"cc.is_change\":true";

	if (tx.output_destinations[index].id)
		rstream << ",\"address\":\"" << tx.output_destinations[index].EncodeDestination() << "\"";

	if (tx.type != CC_TYPE_MINT)
		rstream << ",\"category\":\"receive\"";
	else if (tx.status == TX_STATUS_CLEARED && btc_block >= tx.btc_block)
		rstream << ",\"category\":\"generate\"";
	else
		rstream << ",\"category\":\"immature\"";

	if (tx.InputsInvolveWatchOnly() || tx.output_destinations[index].TypeIsWatchOnlyDestination())
		rstream << ",\"involvesWatchonly\":true";

	if (tx.output_destinations[index].label[0])
		rstream << ",\"label\":\"" << tx.output_destinations[index].label << "\"";

	if (tx.output_bills[index].asset)
		rstream << ",\"amount\":0";
	else
	{
		string amount;
		amount_to_string(tx.output_bills[index].asset, tx.output_bills[index].amount, amount);
		rstream << ",\"amount\":" << amount;
	}

	rstream << ",\"vout\":0";
}

static uint64_t _btc_list_xtx(DbConn *dbconn, Transaction& tx, uint64_t last_xid, bool incwatch, ostringstream& rstream)
{
	//rstream << ",\"dump\":\"" << tx.DebugString() << "\"";

	rstream	<< "\"account\":\"\"";

	if (tx.InputsInvolveWatchOnly())
		rstream << ",\"involvesWatchonly\":true";

	rstream << ",\"address\":\"exchange\"";
	rstream << ",\"category\":\"exchange\"";
	rstream << ",\"cc.type\":" << tx.type;
	rstream << ",\"cc.type-label\":\"" << tx.TypeString() << "\"";

	string amount;
	amount_to_string(0, tx.donation, amount);

	rstream <<
		",\"vout\":0"
		",\"fee\":" << (tx.donation ? "-" : "") << amount <<
		",\"abandoned\":" << truefalse(tx.status == TX_STATUS_ABANDONED || tx.status == TX_STATUS_CONFLICTED || tx.status == TX_STATUS_ERROR);

	Xmatchreq xreq;

	auto rc = dbconn->ExchangeRequestSelectTxId(tx.id, last_xid, xreq);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (rc) return 0;

	//rstream << ",\"dump\":\"" << xreq.DebugString() << "\"";

	if (xreq.xreqnum)
		rstream << ",\"cc.request-number\":" << xreq.xreqnum;

	auto pledge = xreq.max_amount;
	if (Xtx::TypeIsBuyer(xreq.type) && xreq.pledge != 100)
		pledge = pledge * bigint_t(xreq.pledge) / bigint_t(100UL);	// pledge amounts always rounded down

	amount_to_string(xreq.base_asset, pledge, amount);
	rstream << ",\"cc.pledge\":-" << amount;

	return xreq.id;
}

void btc_gettransaction(CP string& txid, bool incwatch, RPC_STDPARAMS)			// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_gettransaction " << txid << " incwatch " << incwatch;

	if (0 && TEST_STUB_BTC)
	{
		if (txid.length() < 10)
			throw txrpc_invalid_txid_error;

		rstream << "{}";
	}

	bigint_t address, commitment;
	uint64_t id, dest_chain;

	auto rc = Transaction::DecodeBtcTxid(txid, id, dest_chain, address, commitment);
	if (rc > 0) throw txrpc_txid_not_found;
	if (rc) throw txrpc_invalid_txid_error;

	Billet bill;

	if (!id)
	{
		rc = dbconn->BilletSelectTxid(&address, &commitment, bill);
		if (rc > 0) throw txrpc_txid_not_found;
		if (rc) throw txrpc_wallet_db_error;

		id = bill.create_tx;
	}

	Transaction tx;

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rc = tx.BeginAndReadTx(dbconn, id);
	if (rc > 0) throw txrpc_txid_not_found;
	if (rc) throw txrpc_wallet_db_error;

	if (!tx.nout)
	{
		rstream << "{";

		_btc_list_tx(dbconn, tx, -1, btc_block, incwatch, rstream);

		rstream << "}";

		return;
	}

	unsigned index;
	for (index = 0; index < tx.nout && bill.id; ++index)
	{
		if (tx.output_bills[index].id == bill.id)
			break;
	}

	bool reread = false;
	for (unsigned i = 0; i < tx.nout && index < tx.nout; ++i)
	{
		if (tx.output_bills[index].status >= BILL_STATUS_SENT && tx.output_bills[i].status < BILL_STATUS_SENT)
		{
			Secret secret;
			rc = dbconn->SecretSelectSecret(&tx.output_bills[i].address, TX_ADDRESS_BYTES, secret);
			if (!rc)
			{
				secret.PollAddress(dbconn, txquery);

				reread = true;
			}
		}
	}

	if (reread)
	{
		btc_block = g_btc_block.FinishCurrentBlock();

		rc = tx.BeginAndReadTx(dbconn, id);
		if (rc) throw txrpc_wallet_db_error;
	}

	CCASSERT(index < tx.nout);

	/*
    CAmount nCredit = wtx.GetCredit(filter);
    CAmount nDebit = wtx.GetDebit(filter);
    CAmount nNet = nCredit - nDebit;
    CAmount nFee = (wtx.IsFromMe(filter) ? wtx.tx->GetValueOut() - nDebit : 0);

    entry.push_back(Pair("amount", ValueFromAmount(nNet - nFee)));
    if (wtx.IsFromMe(filter))
        entry.push_back(Pair("fee", ValueFromAmount(nFee)));

    WalletTxToJSON(wtx, entry);

    UniValue details(UniValue::VARR);
    ListTransactions(wtx, "*", 0, false, details, filter);
    entry.push_back(Pair("details", details));
	*/

	tx.SetAdjustedAmounts(incwatch);

	string amount;
	amount_to_string(tx.output_bills[index].asset, tx.adj_amounts[index], amount);
	rstream << "{\"amount\":" << amount;

	if (tx.WeSent(incwatch))
	{
		amount_to_string(0, tx.adj_donations[index], amount);
		rstream << ",\"fee\":" << (tx.adj_donations[index] ? "-" : "") << amount;
	}

	rstream << ",";

	_btc_list_tx(dbconn, tx, index, btc_block, incwatch, rstream);

	// If we are debited by the transaction, add the output as a "sent" entry

	rstream << ",\"details\":[";

	if (tx.WeSent(incwatch) && !tx.output_bills[index].BillIsChange())
	{
		rstream << "{";
		_btc_list_sent(tx, index, rstream);
		rstream << "}";
	}

	// If we are receiving the output, add it as a "received" entry

	if (tx.output_destinations[index].DestinationFromThisWallet(incwatch) && !tx.output_bills[index].BillIsChange())
	{
		rstream << (tx.WeSent(incwatch) ? ",{" : "{");
		_btc_list_received(tx, index, btc_block, rstream);
		rstream << "}";
	}

	rstream
		<<	"]"
			",\"hex\":\"00\""
			"}";
}

void btc_getunconfirmedbalance(RPC_STDPARAMSAQ)									// implemented trivially
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getunconfirmedbalance";

	// In bitcoin, getunconfirmedbalance reports only untrusted amounts. These are not tracked in CredaCash, so report zero

	if (IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "The CredaCash wallet does not track untrusted amounts until they have cleared, so this value is always reported as zero.\n" << endl;
	}

	rstream << 0;
	add_quotes = false;
}


void btc_getwalletinfo(RPC_STDPARAMS)											// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getwalletinfo";

	TxParams txparams;
	auto param_rc = g_txparams.GetParams(txparams, txquery);

	bigint_t masterhash;
	auto rc = master_secret_hash(dbconn, masterhash);
	if (rc < 0) throw txrpc_wallet_error;

	Transaction txcount;
	rc = dbconn->TransactionSelectIdDescending(INT64_MAX, txcount);
	if (rc < 0) throw txrpc_wallet_db_error;

	string balance, fee;

	amtint_t amounti = 0UL;
	Total::GetTotalBalance(dbconn, true, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false);

	amount_to_string(0, amounti, balance);

	if (param_rc)
		fee = "0";
	else
		estimate_donation(txparams, 2, 2, fee);

	g_btc_block.FinishCurrentBlock();

	rstream <<
	"{"
	"\"walletversion\":" << WALLET_VERSION << ","
	"\"balance\":" << balance << ","
	"\"unconfirmed_balance\":0,"
	"\"immature_balance\":0,"
	"\"txcount\":" << txcount.id << ","
	"\"keypoololdest\":1546300800,"
	"\"keypoolsize\":200,"
	"\"unlocked_until\":7258075199,"
	"\"paytxfee\":" << fee << ","
	"\"hdmasterkeyid\":\"" << setw(40) << setfill('0') << hex << masterhash << dec <<"\""
	"}";
}

void btc_listaccounts(bool incwatch, RPC_STDPARAMS)								// implemented (for default account only)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listaccounts incwatch " << incwatch;

	// TODO: implement accounts

	amtint_t amounti = 0UL;
	string balance;

	Total::GetTotalBalance(dbconn, true, amounti, TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_BALANCE, true, incwatch);

	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream <<
	"{"
	"\"\":" << balance <<
	"}";
}

void btc_listaddressgroupings(RPC_STDPARAMS)												// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listaddressgroupings";

	// no groupings in CredaCash as long as all tx inputs are unpublished

	throw txrpc_not_implemented_error;
}

void btc_listreceivedbyaccount(bool incempty, bool incwatch, RPC_STDPARAMS)		// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listreceivedbyaccount incempty " << incempty << " incwatch " << incwatch;

	// DEPRECATED. List balances by account.

	// TODO: implement incempty

	amtint_t amounti;
	string balance;

	Total::GetTotalBalance(dbconn, true, amounti, TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_RECEIVED, false, false);

	if (incwatch)
	{
		amtint_t amount2;
		Total::GetTotalBalance(dbconn, true, amount2, TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_RECEIVED | TOTAL_TYPE_TW_BITS, false, true);

		amounti += amount2;
	}

	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream << "[{";
	if (incwatch)
		rstream << "\"involvesWatchonly\" : true,";			// !!! TODO: not always true?
	rstream <<
	"\"account\":\"\","
	"\"amount\":" << balance << ","
	"\"confirmations\":" << BtcBlock::BlockIncrement();
	rstream << "}]";
}

void btc_listreceivedbyaddress(bool incempty, bool incwatch, string addr, RPC_STDPARAMS)	// NOT_implemented !!! TODO: implement, with filtering
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listreceivedbyaddress incempty " << incempty << " incwatch " << incwatch << " addr " << addr;

	if (TEST_STUB_BTC)
	{
		rstream <<
		"["
		"{"
		// !!! check: "involvesWatchonly" : true,		// (bool) Only returned if imported addresses were involved in transaction
	/*s*/"\"address\":\"" << string(64,'0') <<"\","		// (string) The receiving address
	/*s*/"\"account\":\"\","								// (string) DEPRECATED. The account of the receiving address. The default account is "".
	/*s*/"\"amount\":2.00000000,"						// (numeric) The total amount in BTC received by the address
	/*s*/"\"confirmations\":1234,"						// (numeric) The number of confirmations of the most recent transaction included
	/*s*/"\"label\":\"\","								// (string) A comment for the address/transaction, if any
	/*s*/"\"txids\":"
		"["
	/*s*/"\"" << string(64,'0') <<"\""					// (numeric) The ids of transactions received with the address
		"]"
		"},"
		"{"
	/*s*/"\"address\":\"" << string(64,'0') <<"\","
	/*s*/"\"account\":\"\","
	/*s*/"\"amount\":3.00000000,"
	/*s*/"\"confirmations\":1234,"
	/*s*/"\"label\":\"\","
	/*s*/"\"txids\":"
		"["
	/*s*/"\"" << string(64,'0') <<"\","
	/*s*/"\"" << string(64,'0') <<"\""
		"]"
		"}"
		"]";
	}

	throw txrpc_not_implemented_error;
}

void btc_listsinceblock(CP string& blockhash, bool incwatch, RPC_STDPARAMS)		// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listsinceblock " << blockhash << " incwatch " << incwatch; //@retest

	// Get all tx's in blocks after block [blockhash], or all tx's if omitted

	// Note when Implement_CCMint is true, CredaCash does not include unconfirmed mint tx's in the output of
	//	listsinceblock, but does include them in listtransactions.

	if (0 && TEST_STUB_BTC)
	{
		if (blockhash.length() && blockhash.length() < 10 && false)	// bitcoin 14.2 ignores invalid block hash, maybe because that param is optional?
			throw txrpc_block_not_found_err;

		rstream << "{}";
	}

	uint64_t first_level = 1;
	bigint_t bigval;
	auto rc = parse_int_value("", "", string("x") + blockhash, 64, 0UL, bigval, NULL, 0);
	if (!rc) first_level = BIG64(bigval) + 1;

	auto btc_block = g_btc_block.FinishCurrentBlock();

	//cerr << "first_level " << first_level << " btc_block " << btc_block << endl;

	if (first_level > btc_block + 1)
		throw txrpc_block_not_found_err;

	//ccsleep(40);	// for testing

	rstream << "{\"transactions\":[";

	uint64_t last_level = 0;
	uint64_t last_id = TX_ID_MINIMUM;
	ostringstream uncleared;
	bool needs_comma[2] = {false, false};
	Transaction tx;

	while (true)
	{
		auto rc = tx.BeginAndReadTxLevel(dbconn, last_level, last_id);

		if (rc < 0)
			throw txrpc_wallet_db_error;

		if (rc)
			break;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		last_level = tx.btc_block;
		last_id = tx.id;

		if (tx.type == CC_TYPE_MINT && tx.btc_block == 0 && Implement_CCMint(tx.output_bills[0].blockchain))
			continue;

		tx.SetAdjustedAmounts(incwatch);

		// uncleared are output to a separate stream and appended to the end of the output

		bool not_cleared = (tx.btc_block == 0 || tx.btc_block > btc_block);
		ostringstream& stream = (not_cleared ? uncleared : rstream);

		//cerr << "tx id " << tx.id << " block " << tx.btc_block << " btc_block " << btc_block << " not_cleared " << not_cleared << endl;

		for (unsigned index = 0; index < tx.nout; ++index)
		{
			// If we are debited by the transaction, add the output as a "sent" entry

			if (tx.WeSent(incwatch) && !tx.output_bills[index].BillIsChange())
			{
				stream << (needs_comma[not_cleared] ? ",{" : "{");
				needs_comma[not_cleared] = true;
				_btc_list_sent(tx, index, stream);
				stream << ",";
				_btc_list_tx(dbconn, tx, index, btc_block, incwatch, stream);
				stream << "}";
			}

			// If we are receiving the output, add it as a "received" entry

			if (tx.output_destinations[index].DestinationFromThisWallet(incwatch) && !tx.output_bills[index].BillIsChange())
			{
				stream << (needs_comma[not_cleared] ? ",{" : "{");
				needs_comma[not_cleared] = true;
				_btc_list_received(tx, index, btc_block, stream);
				stream << ",";
				_btc_list_tx(dbconn, tx, index, btc_block, incwatch, stream);
				stream << "}";
			}
		}
	}

	if (needs_comma[1])
	{
		if (needs_comma[0])
			rstream << ",";
		rstream << uncleared.str();
	}

	rstream << "],\"lastblock\":\""
		<< setw(64) << setfill('0') << hex << btc_block << dec
		<< "\"}";
}

void btc_listtransactions(CP string& acct, int count, int from, bool incwatch, RPC_STDPARAMS)	// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listtransactions " << acct << " count " << count << " from " << from << " incwatch " << incwatch;

	// Returns up to 'count' most recent transactions skipping the first 'from' transactions for account 'account'.

	bool account_exists = false;
	if (acct == "" || acct == "*")	// !!! for now, since accounts aren't implemented)
		account_exists = true;

	if (count <= 0 || !account_exists)
	{
		rstream << "[]";

		return;
	}

	if (from < 0)
		from = 0;

	uint64_t ufrom = from;
	uint64_t limit = ufrom + count;	// !!!!! is this right?

	uint64_t scan_count = 0;
	uint64_t next_id = INT64_MAX;
	Transaction tx;

	auto btc_block = g_btc_block.FinishCurrentBlock();

	//ccsleep(15);	// for testing

	while (scan_count < limit && next_id >= TX_ID_MINIMUM)
	{
		auto rc = tx.BeginAndReadTxIdDescending(dbconn, next_id);

		if (rc > 0)
			break;
		if (rc)
			throw txrpc_wallet_db_error;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		next_id = tx.id - 1;

		if (tx.btc_block > btc_block)
			continue;

		if (Xtx::TypeIsXreq(tx.type))
		{
				++scan_count;
				continue;
		}

		for (unsigned index = 0; index < tx.nout; ++index)
		{
			// If we are debited by the transaction, add the output as a "sent" entry

			if (tx.WeSent(incwatch) && !tx.output_bills[index].BillIsChange())
				++scan_count;

			// If we are receiving the output, add it as a "received" entry

			if (tx.output_destinations[index].DestinationFromThisWallet(incwatch) && !tx.output_bills[index].BillIsChange())
				++scan_count;
		}

		//cerr << "down scan tx.id " << tx.id << " from " << from << " scan_count " << scan_count << " limit " << limit << endl;
	}

	rstream << "[";

	++next_id;
	bool needs_comma = false;

	//ccsleep(15);	// for testing

	while (ufrom < scan_count && next_id <= INT64_MAX)
	{
		auto rc = tx.BeginAndReadTx(dbconn, next_id, true);

		if (rc > 0)
			break;
		if (rc)
			throw txrpc_wallet_db_error;

		if (g_shutdown)
			throw txrpc_shutdown_error;

		next_id = tx.id + 1;

		if (tx.btc_block > btc_block)
			continue;

		//cerr << "up scan tx.id " << tx.id << " from " << from << " scan_count " << scan_count << " limit " << limit << endl;

		if (Xtx::TypeIsXreq(tx.type))
		{
			--scan_count;

			if (ufrom <= scan_count && scan_count < limit)
			{
				rstream << (needs_comma ? ",{" : "{");
				needs_comma = true;
				_btc_list_xtx(dbconn, tx, 0, incwatch, rstream);
				rstream << ",";
				_btc_list_tx(dbconn, tx, 0, btc_block, incwatch, rstream);
				rstream << "}";
			}

			continue;
		}

		tx.SetAdjustedAmounts(incwatch);

		for (unsigned index = 0; index < tx.nout; ++index)
		{
			// If we are debited by the transaction, add the output as a "sent" entry

			if (tx.WeSent(incwatch) && !tx.output_bills[index].BillIsChange())
			{
				--scan_count;

				if (ufrom <= scan_count && scan_count < limit)
				{
					rstream << (needs_comma ? ",{" : "{");
					needs_comma = true;
					_btc_list_sent(tx, index, rstream);
					rstream << ",";
					_btc_list_tx(dbconn, tx, index, btc_block, incwatch, rstream);
					rstream << "}";
				}
			}

			// If we are receiving the output, add it as a "received" entry

			if (tx.output_destinations[index].DestinationFromThisWallet(incwatch) && !tx.output_bills[index].BillIsChange())
			{
				--scan_count;

				if (ufrom <= scan_count && scan_count < limit)
				{
					rstream << (needs_comma ? ",{" : "{");
					needs_comma = true;
					_btc_list_received(tx, index, btc_block, rstream);
					rstream << ",";
					_btc_list_tx(dbconn, tx, index, btc_block, incwatch, rstream);
					rstream << "}";
				}
			}
		}
	}

	rstream << "]";
}

void btc_move(CP string& fromacct, CP string& toacct, CP bigint_t& amount, CP string& comment, RPC_STDPARAMSAQ)	// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_move from acct " << fromacct << " to acct " << toacct << " amount " << amount << " comment " << comment;

	if (TEST_STUB_BTC)
	{
		rstream << "false";
		add_quotes = false;
	}

	throw txrpc_not_implemented_error;
}

void btc_sendfrom(CP string& fromacct, CP string& toaddr, CP bigint_t& amount, CP string& comment, CP string& comment_to, RPC_STDPARAMS)	// NOT_implemented !!! TODO: implement for default account?
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_sendfrom from acct " << fromacct << " to addr " << toaddr << " amount " << amount << " comment " << comment << " comment_to " << comment_to;

	if (TEST_STUB_BTC)
	{
		if (toaddr.length() < 10)
			throw txrpc_invalid_address;

		if (amount > bigint_t(10UL))
			throw txrpc_insufficient_funds;

		rstream << string(64,'0');
	}

	throw txrpc_not_implemented_error;
}

void btc_setaccount(CP string& addr, CP string& acct, RPC_STDPARAMS)						// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_setaccount addr " << addr << " acct " << acct;

	/*
		reassigns all billets received at the given address to the new account
		this deducts the amounts received from the address's former account balance, and adds them to the new account balance
		amounts sent are not affected, and therefore the former account may end up with a negative balance
	*/

	if (TEST_STUB_BTC)
	{
		if (addr.length() < 10)
			throw txrpc_invalid_address;

		if (addr.length() < 12)
			throw RPC_Exception(RPC_WALLET_ERROR, "setaccount can only be used with own address");

		rstream << "";
	}

	throw txrpc_not_implemented_error;
}
