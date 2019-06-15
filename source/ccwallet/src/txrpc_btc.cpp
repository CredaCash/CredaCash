/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
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
#include "amounts.h"
#include "btc_block.hpp"
#include "txparams.hpp"
#include "txquery.hpp"
#include "walletdb.hpp"

#include <CCmint.h>
#include <transaction.h>
#include <jsonutil.h>
#include <blake2/blake2.h>

#define WALLET_VERSION	1

#define TRACE_TX	(g_params.trace_txrpc)

//#define TEST_STUB_BTC	1

#ifndef TEST_STUB_BTC
#define TEST_STUB_BTC	0	// don't compile stubs
#endif

using namespace snarkfront;

#define stdparams	DbConn *dbconn, TxQuery& txquery, ostringstream& rstream
#define stdparamsaq	stdparams, bool &add_quotes
#define CP			const

const RPC_Exception txrpc_server_error((RPCErrorCode)(-32000), "Server error");
const RPC_Exception txrpc_wallet_error(RPCErrorCode(-32001), "Wallet internal error");
const RPC_Exception txrpc_wallet_db_error(RPCErrorCode(-32001), "Wallet database error");
const RPC_Exception txrpc_not_implemented_error(RPC_INVALID_PARAMS, "Method not implemented");
const RPC_Exception txrpc_invalid_txid_error(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
const RPC_Exception txrpc_accounts_not_implemented_error(RPC_WALLET_INSUFFICIENT_FUNDS, "Accounts not implemented");
const RPC_Exception txrpc_simulated_error(RPC_WALLET_SIMULATED_ERROR, "Simulated (test) error");

const RPC_Exception txrpc_block_height_range_err(RPC_INVALID_PARAMETER, "Block height out of range");
const RPC_Exception txrpc_block_not_found_err(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
const RPC_Exception txrpc_tx_not_in_block(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
const RPC_Exception txrpc_invalid_address(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
const RPC_Exception txrpc_insufficient_funds(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");
const RPC_Exception txrpc_tx_rejected(RPC_VERIFY_REJECTED, "Transaction not allowed or invalid");

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

static void compute_fee(const TxParams& txparams, string& fee)
{
	bigint_t donation;
	txparams.ComputeDonation(2, 2, donation);
	auto amount_fp = tx_amount_encode(donation, true, txparams.donation_bits, txparams.exponent_bits);
	tx_amount_decode(amount_fp, donation, true, txparams.donation_bits, txparams.exponent_bits);
	amount_to_string(0, donation, fee);
}

void btc_getbestblockhash(stdparams)											// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getbestblockhash";

	// returns "block hash" = block level

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rstream << setw(64) << setfill('0') << hex << btc_block << dec;
}

void btc_getblock(CP string& blockhash, stdparams)										// NOT_implemented
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

void btc_getblockchaininfo(stdparams)										// implemented
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
	"\"mediantime\":" << time(NULL) << ","
	"\"verificationprogress\":1000,"
	"\"chainwork\":\"" << string(16,'0') << "1" << string(47,'0') << "\","
	"\"pruned\":false,"
	"\"softforks\":[]"
	"}";
}

void btc_getblockcount(stdparamsaq)											// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getblockcount";

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
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

void btc_getblockhash(int64_t index, stdparams)								// implemented
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

void btc_getblockheader(CP string& blockhash, stdparams)									// NOT_implemented
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

void btc_getchaintips(stdparams)											// implemented
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

void btc_gettxout(CP string& txid, int vout, stdparams)									// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_gettxout " << txid << " vout " << vout;

	throw txrpc_not_implemented_error;
}

void btc_gettxoutproof(CP Json::Value& txids, CP string& blockhash, stdparams)			// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_gettxoutproof blockhash " << blockhash;

	if (TEST_STUB_BTC)
	{
		throw txrpc_tx_not_in_block;
	}

	throw txrpc_not_implemented_error;
}

void btc_gettxoutsetinfo(stdparams)														// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_gettxoutsetinfo";

	throw txrpc_not_implemented_error;
}

void btc_verifychain(int checklevel, int64_t numblocks, stdparamsaq)						// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_verifychain checklevel " << checklevel << " numblocks " << numblocks;

	if (TEST_STUB_BTC)
	{
		rstream << "true";
		add_quotes = false;
	}

	throw txrpc_not_implemented_error;
}

void btc_verifytxoutproof(CP string& proof, stdparams)									// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_verifytxoutproof " << proof;

	throw txrpc_not_implemented_error;
}

void btc_getinfo(stdparams)													// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getinfo";

	TxParams txparams;
	auto param_rc = g_txparams.GetParams(txparams, txquery);

	string balance, fee;

	amtint_t amounti = 0UL;
	auto rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false);
	if (rc) throw txrpc_wallet_db_error;
	amount_to_string(0, amounti, balance);

	if (param_rc)
		fee = "0";
	else
		compute_fee(txparams, fee);

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rstream <<
	"{"
	"\"version\":" << (txparams.server_version >> 32) << ","
	"\"protocolversion\":" << (txparams.protocol_version >> 32) << ","
	"\"walletversion\":" << WALLET_VERSION << ","
	"\"balance\":" << balance << ","
	"\"blocks\":" << btc_block << ","
	"\"timeoffset\":" << txparams.clock_diff << ","
	"\"connections\":" << (param_rc ? 0 : 8) << ","
	"\"difficulty\":1.0,";

	if (!param_rc) // don't say anything about testnet unless we know
		rstream << "\"testnet\":" << truefalse(IsTestnet(txparams.blockchain)) << ",";

	rstream <<
	"\"keypoololdest\":1514764800,"
	"\"keypoolsize\":100,"
	"\"unlocked_until\":7258075199,"
	"\"paytxfee\":" << fee << ","
	"\"relayfee\":" << fee << ","
	"\"errors\":\"\""		// !!!						// (string) any error messages
	"}";
}

void btc_getmininginfo(stdparams)											// implemented mostly trivially
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getmininginfo";

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "CredaCash does not support mining.\n" << endl;
	}

	TxParams txparams;
	auto param_rc = g_txparams.GetParams(txparams, txquery);

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

void btc_getconnectioncount(stdparamsaq)													// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getconnectioncount";

	if (TEST_STUB_BTC)
	{
		rstream << "8";
		add_quotes = false;
	}

	throw txrpc_not_implemented_error;
}

void btc_getnettotals(stdparams)															// NOT_implemented
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

void btc_getnetworkinfo(stdparams)														// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getnetworkinfo";

	throw txrpc_not_implemented_error;
}

void btc_getpeerinfo(stdparams)															// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getpeerinfo";

	throw txrpc_not_implemented_error;
}

void btc_ping(stdparamsaq)													// implemented
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
	// CredaCash breaks compatibility with bitcoin's ping by returning the server ping time instead of a null reponse

	rstream << elapsed / (float)CCTICKS_PER_SEC;
	add_quotes = false;
}

void btc_estimatefee(stdparamsaq)											// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_estimatefee";

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr <<

R"(The value shown is the required donation for a baseline transaction with 2 inputs and 2 outputs. The donation for an
actual transaction will varying depending on the type of transaction, the number of inputs and outputs, and its size.)"

		"\n" << endl;
	}

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	string fee;

	compute_fee(txparams, fee);

	rstream << fee;
	add_quotes = false;
}

void btc_validateaddress(CP string& addr, stdparams)						// implemented
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

void btc_abandontransaction(CP string& txid, stdparams)									// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_abandontransaction " << txid;

	// !!! automatically abandon tx's based on command line param?

	if (TEST_STUB_BTC)
	{
		if (txid.length() < 10)
			throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");

		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");
	}

	throw txrpc_not_implemented_error;
}

void btc_backupwallet(CP string& dest, stdparams)										// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_backupwallet " << dest;

	if (TEST_STUB_BTC)
	{
		if (dest.length() < 10)
			throw RPC_Exception(RPC_WALLET_ERROR, "Error: Wallet backup failed!");

		rstream << "";
	}

	throw txrpc_not_implemented_error;
}

void btc_dumpwallet(CP string& dest, stdparams)											// NOT_implemented
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

void btc_encryptwallet(CP string& pass, stdparams)										// NOT_implemented
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

void btc_walletpassphrase(CP string& pass, stdparams)									// NOT_implemented
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

void btc_walletlock(stdparams)															// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_walletlock";

	if (TEST_STUB_BTC)
	{
		if (rand() & 1)
			throw RPC_Exception(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

		rstream << "";
	}

	throw txrpc_not_implemented_error;
}

void btc_getaccount(CP string& addr, stdparams)								// implemented
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

void btc_getaccountaddress(CP string& acct, stdparams)									// NOT_implemented !!! IMPLEMENT?
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getaccountaddress " << acct;

	if (TEST_STUB_BTC)
	{
		rstream << string(64,'0');
	}

	throw txrpc_not_implemented_error;
}

void btc_getaddressesbyaccount(CP string& acct, stdparams)								// NOT_implemented !!! IMPLEMENT?
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getaddressesbyaccount " << acct;

	if (TEST_STUB_BTC)
	{
		if (acct.length() > 0)
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

void btc_getbalance(CP string& acct, bool incwatch, stdparamsaq)			// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getbalance " << acct << " incwatch " << incwatch;

	// account "" = default account
	// account "*" = sum of all accounts in wallet
	// incwatch true = include balances in watch-only addresses (see 'importaddress')

	if (0 && TEST_STUB_BTC)
	{
		rstream << "123.00000000";
		add_quotes = false;
	}

	// if account does not exist, bitcoin returns 0.0

	amtint_t amounti = 0UL;
	string balance;

	// TODO: implement accounts

	if (acct == "" || acct == "*")	// !!! Note: when accounts are implemented, "" is not the same as "*".
	{
		auto rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, incwatch);

		if (rc) throw txrpc_wallet_error;
	}

	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream << balance;
	add_quotes = false;
}

void btc_getnewaddress(CP string& acct, stdparams)							// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getnewaddress " << acct;

	// creates a destination
	// TODO: if acct does not exist, it is created

	//	acct must equal "" in this release
	if (acct != "")
		throw txrpc_accounts_not_implemented_error;

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	if (txparams.blockchain != g_params.blockchain)
		throw RPC_Exception(RPCErrorCode(-32000), "Transaction server using different blockchain than wallet");

	Secret destination;
	SpendSecretParams params;
	memset(&params, 0, sizeof(params));

	rc = destination.CreateNewSecret(dbconn, SECRET_TYPE_SPENDABLE_DESTINATION, MAIN_PRE_DESTINATION_ID, g_params.blockchain, params);
	if (rc) throw txrpc_wallet_error;

	rc = destination.CreatePollingAddresses(dbconn, g_params.blockchain, params);
	(void)rc;

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr <<

R"(Generating a new unique CredaCash destination. This destination can be used by a single counterparty to send multiple
transactions to this wallet. This value should be kept private between you and your counterparty, since it can also be
used to decypt the amount of every transaction sent to this destination. To maintain privacy, you must generate a new
unique destination for each counterparty who wishes to send you transactions.)"

		"\n" << endl;
	}

	rstream << destination.EncodeDestination();
}

void btc_getreceivedbyaccount(CP string& acct, stdparamsaq)				// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getreceivedbyaccount " << acct;

	// returns the total amount received by addresses with <account>
	// see getreceivedbyaddress

	if (0 && TEST_STUB_BTC)
	{
		rstream << "123.00000000";
		add_quotes = false;
	}

	// !!! TBD: include watch?

	amtint_t amounti;
	string balance;

	// if account does not exist, bitcoin returns 0.0
	// Note: CredaCash extends this function to support acct == "*" which gives the amount received by all addresses

	if (acct == "*")
	{
		auto rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_RECEIVED, false, false);
		if (rc) throw txrpc_wallet_error;
	}
	else if (acct == "")
	{
		auto rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_RECEIVED, false, false);
		if (rc) throw txrpc_wallet_error;
	}

	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream << balance;
	add_quotes = false;
}

void btc_getreceivedbyaddress(CP string& addr, stdparamsaq)				// implemented
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
	if (rc) throw txrpc_invalid_address;

	amtint_t amounti;
	string balance;

	rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_RECEIVED, false, false, secret.id);
	if (rc) throw txrpc_wallet_error;

	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream << balance;
	add_quotes = false;
}

static const char *bip125str = ",\"bip125-replaceable\":\"no\"";

static void _btc_list_tx(Transaction& tx, unsigned index, uint64_t btc_block, bool incwatch, ostringstream& rstream)
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

	if (tx.status == TX_STATUS_CLEARED && btc_block >= tx.btc_block)
	{
		rstream
			<<	"\"confirmations\":" << btc_block - tx.btc_block + 1
			<<	",\"blockhash\":\"" << setw(64) << setfill('0') << hex << tx.btc_block << dec << "\""
			<<	",\"blockindex\":0"
			<<	",\"blocktime\":" << tx.create_time;
	}
	else
	{
		rstream
			<<	"\"confirmations\":0"
			<<	",\"trusted\":" << truefalse(tx.WeSent(incwatch) && tx.type != TX_TYPE_MINT && tx.status != TX_STATUS_ERROR);
	}

	if (tx.type == TX_TYPE_MINT)
		rstream << ",\"generated\":true";

	rstream
		<<	",\"txid\":\"" << tx.GetBtcTxid() << "\""
			",\"walletconflicts\":";
	if (tx.status != TX_STATUS_CONFLICTED)
		rstream << "[]";
	else
		rstream << "[0]";	// !!! TODO: compute conflicts

	rstream
		<<	",\"time\":" << tx.create_time
		<<	",\"timereceived\":" << tx.create_time
		<<	bip125str;

	//@@! TODO: btc wtx.mapValue items:
	// "comment" set by sendtoaddress, sendfrom, sendmany
	// "to" set by sendtoaddress, sendfrom

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

	rstream	<< "\"account\":\"" << tx.output_accounts[index].name << "\"";

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

	rstream
		<<	",\"vout\":0"
		<<	",\"fee\":" << (tx.adj_donations[index] ? "-" : "") << amount
		<<	",\"abandoned\":" << truefalse(tx.status == TX_STATUS_ABANDONED || tx.status == TX_STATUS_ERROR);
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

	rstream	<< "\"account\":\"" << tx.output_accounts[index].name << "\"";

	if (tx.output_destinations[index].id)
		rstream << ",\"address\":\"" << tx.output_destinations[index].EncodeDestination() << "\"";

	if (tx.type != TX_TYPE_MINT)
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

void btc_gettransaction(CP string& txid, bool incwatch, stdparams)			// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_gettransaction " << txid << " incwatch " << incwatch;

	if (0 && TEST_STUB_BTC)
	{
		if (txid.length() < 10)
			throw txrpc_invalid_txid_error;

		rstream << "{}";
	}

	bigint_t address, commitment;
	uint64_t dest_chain;

	auto rc = Transaction::DecodeBtcTxid(txid, dest_chain, address, commitment);
	if (rc) throw txrpc_invalid_txid_error;

	Billet bill;

	rc = dbconn->BilletSelectTxid(&address, &commitment, bill);
	if (rc < 0) throw txrpc_invalid_txid_error;

	if (rc || bill.status < BILL_STATUS_SENT)
	{
		Secret secret;
		auto rc2 = dbconn->SecretSelectSecret(&address, TX_ADDRESS_BYTES, secret);
		if (rc2) throw txrpc_invalid_txid_error;

		secret.PollAddress(dbconn, txquery);
	}
	if (rc)
	{
		rc = dbconn->BilletSelectTxid(&address, &commitment, bill);
		if (rc) throw txrpc_invalid_txid_error;
	}

	Transaction tx;

	auto btc_block = g_btc_block.FinishCurrentBlock();

	rc = tx.BeginAndReadTx(dbconn, bill.create_tx);
	if (rc) throw txrpc_wallet_error;

	unsigned index = tx.nout;
	for (index = 0; index < tx.nout; ++index)
	{
		if (tx.output_bills[index].id == bill.id)
			break;
	}

	bool reread = false;
	for (unsigned i = 0; i < tx.nout; ++i)
	{
		if (tx.output_bills[index].status >= BILL_STATUS_SENT && tx.output_bills[i].status < BILL_STATUS_SENT)
		{
			Secret secret;
			auto rc = dbconn->SecretSelectSecret(&tx.output_bills[i].address, TX_ADDRESS_BYTES, secret);
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

		rc = tx.BeginAndReadTx(dbconn, bill.create_tx);
		if (rc) throw txrpc_wallet_error;
	}

	CCASSERT(index < tx.nout);
	CCASSERT(tx.output_bills[index].id == bill.id);

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

	_btc_list_tx(tx, index, btc_block, incwatch, rstream);

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

void btc_getunconfirmedbalance(stdparamsaq)								// implemented trivially
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_getunconfirmedbalance";

	// In bitcoin, getunconfirmedbalance reports only untrusted amounts. These are not tracked in CredaCash, so report zero

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "The CredaCash wallet does not track untrusted amounts until they have cleared, so this value is always reported as zero.\n" << endl;
	}

	rstream << 0;
	add_quotes = false;
}


void btc_getwalletinfo(stdparams)											// implemented
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
	rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_DESTINATION | TOTAL_TYPE_RB_BALANCE, true, false);
	if (rc) throw txrpc_wallet_error;
	amount_to_string(0, amounti, balance);

	if (param_rc)
		fee = "0";
	else
		compute_fee(txparams, fee);

	g_btc_block.FinishCurrentBlock();

	rstream <<
	"{"
	"\"walletversion\":" << WALLET_VERSION << ","
	"\"balance\":" << balance << ","
	"\"unconfirmed_balance\":0,"
	"\"immature_balance\":0,"
	"\"txcount\":" << txcount.id << ","
	"\"keypoololdest\":1546300800,"
	"\"keypoolsize\":100,"
	"\"unlocked_until\":7258075199,"
	"\"paytxfee\":" << fee << ","
	"\"hdmasterkeyid\":\"" << setw(40) << setfill('0') << hex << masterhash << dec <<"\""
	"}";
}

void btc_listaccounts(bool incwatch, stdparams)							// implemented (for default account only)
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listaccounts incwatch " << incwatch;

	// TODO: implement accounts

	amtint_t amounti = 0UL;
	string balance;

	auto rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_BALANCE, true, incwatch);
	if (rc) throw txrpc_wallet_error;
	amount_to_string(0, amounti, balance);

	g_btc_block.FinishCurrentBlock();

	rstream <<
	"{"
	"\"\":" << balance <<
	"}";
}

void btc_listaddressgroupings(stdparams)												// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listaddressgroupings";

	// no groupings in CredaCash as long as all tx inputs are unpublished

	throw txrpc_not_implemented_error;
}

void btc_listreceivedbyaccount(bool incempty, bool incwatch, stdparams)	// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listreceivedbyaccount incempty " << incempty << " incwatch " << incwatch;

	// DEPRECATED. List balances by account.

	// TODO: implement incempty

	amtint_t amounti;
	string balance;

	auto rc = Total::GetTotalBalance(dbconn, amounti, TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_RECEIVED, false, false);
	if (rc) throw txrpc_wallet_error;

	if (incwatch)
	{
		amtint_t amount2;
		rc = Total::GetTotalBalance(dbconn, amount2, TOTAL_TYPE_DA_ACCOUNT | TOTAL_TYPE_RB_RECEIVED | TOTAL_TYPE_TW_BITS, false, true);
		if (rc) throw txrpc_wallet_error;

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

void btc_listreceivedbyaddress(bool incempty, bool incwatch, string addr, stdparams)					// NOT_implemented !!! TODO: implement, with filtering
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

void btc_listsinceblock(CP string& blockhash, bool incwatch, stdparams)	// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listsinceblock " << blockhash << " incwatch " << incwatch;

	// Get all tx's in blocks after block [blockhash], or all tx's if omitted

	// Note when Implement_CCMint is true, CredaCash does not include unconfirmed mint tx's in the output of
	//	listsinceblock, but does include them in listtransactions.

	if (0 && TEST_STUB_BTC)
	{
		if (blockhash.length() > 0 && blockhash.length() < 10 && false)	// bitcoin 14.2 ignores invalid block hash, maybe because that param is optional?
			throw txrpc_block_not_found_err;

		rstream << "{}";
	}

	uint64_t first_level = 1;
	bigint_t bigval;
	auto rc = parse_int_value("", "", string("x") + blockhash, 64, 0UL, bigval, NULL, 0);
	if (!rc) first_level = BIG64(bigval) + 1;

	auto btc_block = g_btc_block.FinishCurrentBlock();

	if (first_level > btc_block + 1)
		throw txrpc_block_not_found_err;

	//ccsleep(40);	// for testing

	rstream << "{\"transactions\":[";

	uint64_t next_level = 0;
	uint64_t next_id = TX_ID_MINIMUM;
	ostringstream uncleared;
	bool needs_comma[2] = {false, false};
	Transaction tx;

	while (true)
	{
		auto rc = tx.BeginAndReadTxLevel(dbconn, next_level, next_id);
		if (rc < 0 || g_shutdown)
			throw txrpc_wallet_error;

		if (rc && next_id == TX_ID_MINIMUM)
			break;

		if (rc || (tx.btc_block != next_level && next_id != TX_ID_MINIMUM))
		{
			next_level = (next_level ? next_level + 1 : first_level);
			next_id = TX_ID_MINIMUM;

			continue;
		}

		next_level = tx.btc_block;
		next_id = tx.id + 1;

		if (tx.type == TX_TYPE_MINT && tx.btc_block == 0 && Implement_CCMint(tx.output_bills[0].blockchain))
			continue;

		tx.SetAdjustedAmounts(incwatch);

		// uncleared are scanned first, so if one clears during the scan, it won't be skipped (which could happen if uncleared were scanned last)
		// all uncleared are output to a separate stream and appended to the end of the output

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
				_btc_list_tx(tx, index, btc_block, incwatch, stream);
				stream << "}";
			}

			// If we are receiving the output, add it as a "received" entry

			if (tx.output_destinations[index].DestinationFromThisWallet(incwatch) && !tx.output_bills[index].BillIsChange())
			{
				stream << (needs_comma[not_cleared] ? ",{" : "{");
				needs_comma[not_cleared] = true;
				_btc_list_received(tx, index, btc_block, stream);
				stream << ",";
				_btc_list_tx(tx, index, btc_block, incwatch, stream);
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

void btc_listtransactions(CP string& acct, int count, int from, bool incwatch, stdparams)	// implemented
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
	uint64_t limit = ufrom + count;

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
		if (rc || g_shutdown)
			throw txrpc_wallet_error;

		next_id = tx.id - 1;

		if (tx.btc_block > btc_block)
			continue;

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

	while (ufrom < scan_count)
	{
		auto rc = tx.BeginAndReadTx(dbconn, next_id, true);
		if (rc > 0)
			break;
		if (rc || g_shutdown)
			throw txrpc_wallet_error;

		next_id = tx.id + 1;

		if (tx.btc_block > btc_block)
			continue;

		//cerr << "up scan tx.id " << tx.id << " from " << from << " scan_count " << scan_count << " limit " << limit << endl;

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
					_btc_list_tx(tx, index, btc_block, incwatch, rstream);
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
					_btc_list_tx(tx, index, btc_block, incwatch, rstream);
					rstream << "}";
				}
			}
		}
	}

	rstream << "]";
}

void btc_listunspent(int minconf, int maxconf, CP Json::Value& addrs, stdparams)	// implemented partially
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_listunspent minconf " << minconf << " maxconf " << maxconf;

	if (g_interactive)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr <<

R"(This method is for diagnotic purposes only since billets are fungible in CredaCash and the wallet decides which
billets to use when creating a new transaction. Before this method returns a list of the unspent billets stored in
this wallet, the billet serial numbers are queried to ensure they have not already been spent by another copy of this
wallet, or by a transaction that was not properly recorded in this wallet.)"

		"\n" << endl;
	}

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) throw txrpc_server_error;

	auto btc_block = g_btc_block.FinishCurrentBlock();

	uint64_t asset = 0;
	uint64_t next_id = 0;
	bigint_t next_amount = 0UL;
	bigint_t total = 0UL;
	bool needs_comma = false;
	Billet bill;

	rstream << "[";

	while (true)
	{
		auto rc = dbconn->BilletSelectAmountScan(txparams.blockchain, asset, next_amount, next_id, bill);
		if (rc > 0)
			break;
		if (rc || g_shutdown)
			throw txrpc_wallet_error;

		next_amount = bill.amount;
		next_id = bill.id + 1;

		rc = Billet::CheckIfBilletsSpent(dbconn, txquery, &bill, 1);
		if (rc) continue;

		Transaction tx;
		rc = tx.BeginAndReadTx(dbconn, bill.create_tx);
		if (rc) throw txrpc_wallet_db_error;

		if (tx.btc_block > btc_block)
			continue;

		Secret destination;
		rc = dbconn->SecretSelectId(bill.dest_id, destination);
		if (rc) throw txrpc_wallet_db_error;

		total = total + bill.amount;

		string amount;
		amount_to_string(bill.asset, bill.amount, amount);

		rstream << (needs_comma ? ",{" : "{");
		needs_comma = true;

		rstream <<
		 "\"txid\":\"" << tx.GetBtcTxid() << "\""
		",\"vout\":0"
		",\"address\":\"" << destination.EncodeDestination() << "\""
		",\"account\":\"\""
		//",\"scriptPubKey\":\"" << string(64,'0') << "\""
		",\"amount\":" << amount <<
		",\"confirmations\":" << btc_block - tx.btc_block + 1 <<
		",\"spendable\":true"
		",\"solvable\":true"
		",\"safe\":true}";
	}

	rstream << "]";

	BOOST_LOG_TRIVIAL(info) << "btc_listunspent total = " << total;

	if (g_interactive)
	{
		string amount;
		amount_to_string(0, total, amount);
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "Total amount of the unspent billets: " << amount << "\n" << endl;
	}
}

void btc_move(CP string& fromacct, CP string& toacct, CP bigint_t& amount, CP string& comment, stdparamsaq)							// NOT_implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_move from acct " << fromacct << " to acct " << toacct << " amount " << amount << " comment " << comment;

	if (TEST_STUB_BTC)
	{
		rstream << "false";
		add_quotes = false;
	}

	throw txrpc_not_implemented_error;
}

void btc_sendfrom(CP string& fromacct, CP string& toaddr, CP bigint_t& amount, CP string& comment, CP string& comment_to, stdparams)	// NOT_implemented !!! TODO: implement for default account?
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

void btc_sendtoaddress(CP string& addr, CP bigint_t& amount, CP string& comment, CP string& comment_to, bool subfee, stdparams)	// implemented
{
	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_sendtoaddress " << addr << " amount " << amount << " comment " << comment << " subfee " << subfee;

	if (0 && TEST_STUB_BTC)
	{
		if (addr.length() < 10)
			throw txrpc_invalid_address;

		if (amount > bigint_t(10UL))
			throw txrpc_insufficient_funds;

		rstream << string(64,'0');
	}

	uint64_t dest_chain;
	bigint_t destination;
	auto rc = Secret::DecodeDestination(addr, dest_chain, destination);
	if (rc) throw txrpc_invalid_address;

	Transaction tx;

	tx.Transaction::CreateTxPay(dbconn, txquery, addr, dest_chain, destination, amount, comment, comment_to, subfee);

	if (TRACE_TX) BOOST_LOG_TRIVIAL(info) << "btc_sendtoaddress " << addr << " amount " << amount << " result " << tx.GetBtcTxid();

	rstream << tx.GetBtcTxid();
}

void btc_setaccount(CP string& addr, CP string& acct, stdparams)						// NOT_implemented
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
