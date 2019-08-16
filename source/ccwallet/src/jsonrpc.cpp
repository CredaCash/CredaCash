/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * jsonrpc.cpp
*/

#include "ccwallet.h"
#include "jsonrpc.h"
#include "txrpc.h"
#include "txrpc_btc.h"
#include "rpc_errors.hpp"
#include "walletdb.hpp"
#include "totals.hpp"

#include <jsonutil.h>

#include <jsoncpp/json/json.h>

#define TRACE_JSONRPC	(g_params.trace_jsonrpc)

#define INCLUDE_DEPRECATED		1
#define INCLUDE_BITCOIN_BUGS	1

using namespace snarkfront;

/*
	notable bitcoin 17.0 commands that could be added:
		uptime
		getaddressesbylabel "label"
		getaddressinfo "destination"
		getbalance ( "(dummy)" minconf include_watchonly )
		getnewaddress ( "label" "destination_type" )
		listlabels ( "purpose" )
		listreceivedbyaddress ( minconf include_empty include_watchonly destination_filter )
*/

static bool add_comma(const ostringstream& response)
{
	return (response.str().length() > 1);
}

static void copy_result_to_response(bool add_quotes, const string& msg, const char *version, const char *id, const char *error, bool notification, ostringstream& response)
{
	// !!! TODO?: JSON-RPC 2.0: don't send reponse if no id

	if (msg.length() > 0 && (msg[0] == '{' or msg[0] == '['))
		add_quotes = false;

	if (!notification)
		response << (add_comma(response) ? ",{" : "{") << version << "\"result\":" << (add_quotes ? "\"" : "") << msg << (add_quotes ? "\"," : ",") << error << "\"id\":" << id << "}";
}

static void copy_error_to_response(int code, const string& msg, const char *version, const char *id, const char *result, bool notification, ostringstream& response)
{
	if (!notification)
		response << (add_comma(response) ? ",{" : "{") << version << result << "\"error\":{\"code\":" << code << ",\"message\":\"" << msg << "\"},\"id\":" << id << "}";
}

static void parse_amount(const string& json, Json::Value value, bool allow_zero, bigint_t& amount)
{
	const char *not_numeric_err = "JSON value is not a number as expected";
	const char *invalid_amount_err = "Invalid amount";

	amtfloat_t amountf, amountf2;

	try
	{
		amountf = (amtfloat_t)(json.substr(value.getOffsetStart(), value.getOffsetLimit() - value.getOffsetStart()));
	}
	catch (...)
	{
		throw RPC_Exception(RPC_MISC_ERROR, not_numeric_err);
	}

	if (amountf < 0 || (amountf == 0 && !allow_zero))
		throw RPC_Exception(RPC_TYPE_ERROR, invalid_amount_err);

	const uint64_t asset = 0;
	auto rc = amount_from_float(asset, amountf, amount);
	if (rc)
		throw RPC_Exception(RPC_TYPE_ERROR, invalid_amount_err);

	amount_to_float(asset, amount, amountf2);
	if (amountf2 < amountf)
		amount = amount + bigint_t(1UL);	// round up
}

static void try_one_rpc(const string& json, const string& method, Json::Value& params, DbConn *dbconn, TxQuery& txquery, ostringstream& rstream, bool &add_quotes)
{
	//throw RPC_Exception((RPCErrorCode)(-9), "throw");

	const char *not_bool_err = "JSON value is not a boolean as expected";
	const char *not_int_err = "JSON value is not an integer as expected";
	const char *not_array_err = "JSON value is not an array as expected";
	//const char *invalid_param_err = "Invalid parameter";

	const char *tx_not_in_mempool = "Transaction not in mempool";
	//const char *negative_count_err = "Negative count";
	//const char *negative_from_err = "Negative from";

	const char *only_verbose_err = "only verbose=true is supported";

	if (method == "stop")
	{
		start_shutdown();	// !!! needs a response string?
	}
	else if (method == "help")															// implemented
	{
		rstream <<
		"\\n"
		"==CredaCash Commands===\\n"
		"stop - shutdown the wallet (leaves the network node server running)\\n"
		"ping - queries the transaction server and returns the ping time in seconds\\n"
		"cc.time - returns the wallet's current time\\n"
		"cc.poll_destination \\\"destination\\\" ( last_received_max ) - check destination for incoming payments\\n"
		"cc.mint - mint currency (if allowed by blockchain)\\n"
		"\\n"
		"==Bitcoin Compatibility Commands==\\n"
		"getbestblockhash\\n"
		//"getblock \\\"hash\\\" ( verbose )\\n"
		"getblockchaininfo\\n"
		"getblockcount - returns compatibility block count\\n"
		"getblockhash index\\n"
		//"getblockheader \\\"hash\\\" ( verbose )\\n"
		"getchaintips\\n"
		"getdifficulty - reports constant value\\n"
		//"gettxout \\\"txid\\\" n ( includemempool )\\n"
		//"gettxoutproof [\\\"txid\\\",...] ( blockhash )\\n"
		//"gettxoutsetinfo\\n"
		//"verifychain ( checklevel numblocks )\\n"
		//"verifytxoutproof \\\"proof\\\"\\n"
		"getinfo\\n"
		"getmininginfo - mining not supported\\n"
		"estimatefee nblocks - estimates donation for baseline 2-in 2-out transaction\\n"
		"validateaddress \\\"destination\\\"\\n"
		//"abandontransaction \\\"txid\\\"\\n"
		//"backupwallet \\\"destination\\\"\\n"
		//"dumpwallet \\\"filename\\\"\\n"
		//"encryptwallet \\\"passphrase\\\"\\n"
		//"walletpassphrase \\\"passphrase\\\"\\n"
		//"walletlock\\n"
		"getaccount \\\"destination\\\" - only default account currently supported\\n"									// DEPRECATED
		//"getaccountaddress \\\"account\\\"\\n"																		// DEPRECATED
		//"getaddressesbyaccount \\\"account\\\"\\n"																	// DEPRECATED
		"getbalance ( \\\"account\\\" minconf includeWatchonly )\\n"
		"getnewaddress ( \\\"account\\\" )\\n"
		"getreceivedbyaccount \\\"account\\\" ( minconf )\\n"														// DEPRECATED
		"getreceivedbyaddress \\\"destination\\\" ( minconf )\\n"
		"gettransaction \\\"txid\\\" ( includeWatchonly )\\n"
		"getunconfirmedbalance\\n"
		"getwalletinfo\\n"
		//"keypoolrefill ( newsize )\\n"
		"listaccounts ( minconf includeWatchonly )\\n"																// DEPRECATED
		//"listaddressgroupings\\n"
		//"listlockunspent\\n"
		"listreceivedbyaccount ( minconf includeempty includeWatchonly )\\n"											// DEPRECATED
		"listreceivedbyaddress ( minconf includeempty includeWatchonly destination_filter )\\n"
		"listsinceblock ( \\\"blockhash\\\" target-confirmations includeWatchonly )\\n"
		"listtransactions ( \\\"account\\\" count from includeWatchonly )\\n"
		"listunspent ( minconf maxconf [\\\"destination\\\",...] [include_unsafe] ) - for diagnostic purposes only\\n"
		//"move \\\"fromaccount\\\" \\\"toaccount\\\" amount ( minconf \\\"comment\\\" )\\n"							// DEPRECATED
		//"sendfrom \\\"fromaccount\\\" \\\"todestination\\\" amount ( minconf \\\"comment\\\" \\\"comment-to\\\" )\\n"
		//"sendmany \\\"fromaccount\\\" {\\\"destination\\\":amount,...} ( minconf \\\"comment\\\" [\\\"destination\\\",...] )\\n"
		"sendtoaddress \\\"destination\\\" amount ( \\\"comment\\\" \\\"comment-to\\\" subtractfeefromamount )\\n"
		//"setaccount \\\"destination\\\" \\\"account\\\"\\n"																// DEPRECATED
		//"settxfee amount"
		;
	}
	else if (method == "getbestblockhash")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getbestblockhash(dbconn, txquery, rstream);
	}
	else if (method == "getblock")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"hash\\\" ( verbose )");

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		if (params.size() > 1 && params[1].asBool() != true)
			throw RPC_Exception(RPC_MISC_ERROR, only_verbose_err);

		btc_getblock(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "getblockchaininfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getblockchaininfo(dbconn, txquery, rstream);
	}
	else if (method == "getblockcount")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getblockcount(dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "getblockhash")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " index");

		if (!params[0].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_getblockhash(params[0].asUInt64(), dbconn, txquery, rstream);
	}
	else if (method == "getblockheader")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"hash\\\" ( verbose )");

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		if (params.size() > 1 && params[1].asBool() != true)
			throw RPC_Exception(RPC_MISC_ERROR, only_verbose_err);

		btc_getblockheader(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "getchaintips")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getchaintips(dbconn, txquery, rstream);
	}
	else if (method == "getdifficulty")													// implemented trivially
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		// difficulty is always 1.0

		if (g_interactive)
		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr << "CredaCash does not support mining.\n" << endl;
		}

		rstream << "1.0";
		add_quotes = false;
	}
	else if ((method == "getmempoolancestors" || method == "getmempooldescendants"))	// implemented trivially
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"txid\\\" ( verbose )");

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		// not clear how the mempool would be useful, so this wallet always considers it to be empty

		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, tx_not_in_mempool);
	}
	else if (method == "getmempoolentry")												// implemented trivially
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"txid\\\"");

		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, tx_not_in_mempool);
	}
	else if (method == "getmempoolinfo")												// implemented trivially
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		rstream <<
		"{"
		"\"size\":0,"
		"\"bytes\":0,"
		"\"usage\":0,"
		"\"maxmempool\":0,"
		"\"mempoolminfee\":0.00000000"
		"}";
	}
	else if (method == "getrawmempool")													// implemented trivially
	{
		if ( /* params.size() < 0 || */ params.size() > 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( verbose )");

		if (params.size() > 0 && !params[0].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		rstream << "[]";
	}
	else if (method == "gettxout")
	{
		if (params.size() < 2 || params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"txid\\\" n ( includemempool )");

		if (!params[1].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_gettxout(params[0].asString(), params[1].asUInt64(), dbconn, txquery, rstream);
	}
	else if (method == "gettxoutproof")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " [\\\"txid\\\",...] ( blockhash )");

		if (!params[0].isArray())
			throw RPC_Exception(RPC_MISC_ERROR, not_array_err);

		btc_gettxoutproof(params[0], params[1].asString(), dbconn, txquery, rstream);
	}
	else if (method == "gettxoutsetinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_gettxoutsetinfo(dbconn, txquery, rstream);
	}
	else if (method == "verifychain")
	{
		if ( /* params.size() < 0 || */ params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( checklevel numblocks )");

		if (params.size() > 0 && !params[0].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && !params[1].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_verifychain(params.size() > 0 ? params[0].asUInt64() : 3, params.size() > 1 ? params[1].asUInt64() : 6, dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "verifytxoutproof")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"proof\\\"");

		btc_verifytxoutproof(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "getinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getinfo(dbconn, txquery, rstream);
	}
	else if (method == "getconnectioncount")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getconnectioncount(dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "getnettotals")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getnettotals(dbconn, txquery, rstream);
	}
	else if (method == "getnetworkinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getnetworkinfo(dbconn, txquery, rstream);
	}
	else if (method == "getpeerinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getpeerinfo(dbconn, txquery, rstream);
	}
	else if (method == "listbanned")													// implemented trivially
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		rstream << "[]";
	}
	else if (method == "ping")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_ping(dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "setban")														// implemented trivially
	{
		if (params.size() < 2 || params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"ip(/netmask)\\\" \\\"add|remove\\\" (bantime) (absolute)");

		if (params.size() > 2 && !params[2].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 3 && !params[3].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		rstream << "";
	}
	else if (method == "estimatefee")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " nblocks");

		if (!params[0].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_estimatefee(dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "estimatepriority")												// implemented trivially
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " nblocks");

		if (!params[0].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		rstream << "-1";
		add_quotes = false;
	}
	else if (method == "validateaddress")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"destination\\\"");

		btc_validateaddress(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "abandontransaction")
	{
		// FUTURE: automatically abandon tx's based on command line param?

		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"txid\\\"");

		btc_abandontransaction(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "backupwallet")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"destination\\\"");

		btc_backupwallet(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "dumpwallet")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"destination\\\"");

		btc_dumpwallet(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "encryptwallet")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"passphrase\\\"");

		btc_encryptwallet(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "walletpassphrase")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"passphrase\\\"");

		btc_walletpassphrase(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "walletlock")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_walletlock(dbconn, txquery, rstream);
	}
	else if (method == "getaccount" && INCLUDE_DEPRECATED)
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"destination\\\"");

		btc_getaccount(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "getaccountaddress" && INCLUDE_DEPRECATED)
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"account\\\"");

		btc_getaccountaddress(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "getaddressesbyaccount" && INCLUDE_DEPRECATED)
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"account\\\"");

		btc_getaddressesbyaccount(params[0].asString(), dbconn, txquery, rstream);
	}
	else if (method == "getbalance")
	{
		if ( /* params.size() < 0 || */ params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( \\\"account\\\" minconf includeWatchonly )");

		if (params.size() > 1 && !params[1].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_getbalance(params.size() > 0 ? params[0].asString() : "*", params.size() > 2 ? params[2].asBool() : false, dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "getnewaddress")
	{
		if ( /* params.size() < 0 || */ params.size() > 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( \\\"account\\\" )");

		btc_getnewaddress(params.size() > 0 ? params[0].asString() : "", dbconn, txquery, rstream);
	}
	else if (method == "getreceivedbyaccount" && INCLUDE_DEPRECATED)
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"account\\\" ( minconf )");

		if (params.size() > 1 && !params[1].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_getreceivedbyaccount(params[0].asString(), dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "getreceivedbyaddress")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"destination\\\" ( minconf )");

		if (params.size() > 1 && !params[1].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_getreceivedbyaddress(params[0].asString(), dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "gettransaction")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"txid\\\" ( includeWatchonly )");

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_gettransaction(params[0].asString(), params.size() > 1 ? params[1].asBool() : false, dbconn, txquery, rstream);
	}
	else if (method == "getunconfirmedbalance")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getunconfirmedbalance(dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "getwalletinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getwalletinfo(dbconn, txquery, rstream);
	}
	else if (method == "keypoolrefill")													// implemented trivially
	{
		if ( /* params.size() < 0 || */ params.size() > 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( newsize )");

		if (params.size() > 0 && !params[0].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		// keypool does not need prefilling

		rstream << "";
	}
	else if (method == "listaccounts" && INCLUDE_DEPRECATED)
	{
		if ( /* params.size() < 0 || */ params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( minconf includeWatchonly )");

		if (params.size() > 0 && !params[0].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_listaccounts(params.size() > 1 ? params[1].asBool() : false, dbconn, txquery, rstream);
	}
	else if (method == "listaddressgroupings")
	{
		if (params.size() != 0 && !INCLUDE_BITCOIN_BUGS)	// bitcoin 13.1 erroneously doesn't enforce this
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_listaddressgroupings(dbconn, txquery, rstream);
	}
	else if (method == "listlockunspent")												// implemented trivially
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		// specific outputs can't be locked

		rstream << "[]";
	}
	else if (method == "listreceivedbyaccount" && INCLUDE_DEPRECATED)
	{
		if ( /* params.size() < 0 || */ params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( minconf includeempty includeWatchonly )");

		if (params.size() > 0 && !params[0].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_listreceivedbyaccount(params.size() > 1 ? params[1].asBool() : false, params.size() > 2 ? params[2].asBool() : false, dbconn, txquery, rstream);
	}
	else if (method == "listreceivedbyaddress")
	{
		if ( /* params.size() < 0 || */ params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( minconf includeempty includeWatchonly )");

		if (params.size() > 0 && !params[0].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_listreceivedbyaddress(params.size() > 1 ? params[1].asBool() : false, params.size() > 2 ? params[2].asBool() : false, params.size() > 3 ? params[3].asString() : string(), dbconn, txquery, rstream);
	}
	else if (method == "listsinceblock")
	{
		if ( /* params.size() < 0 || */ params.size() > 3 && !INCLUDE_BITCOIN_BUGS)	// bitcoin 13.1 erroneously doesn't enforce this
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( \\\"blockhash\\\" target-confirmations includeWatchonly )");

		if (params.size() > 1 && !params[1].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		//if (params.size() > 1 && params[1].asInt() < 1)	// asInt can throw Json::LogicError
		//	throw RPC_Exception(RPC_INVALID_PARAMETER, invalid_param_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_listsinceblock(params.size() > 0 ? params[0].asString() : string(), params.size() > 2 ? params[2].asBool() : false, dbconn, txquery, rstream);
	}
	else if (method == "listtransactions")
	{
		if ( /* params.size() < 0 || */ params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( \\\"account\\\" count from includeWatchonly )");

		if (params.size() > 1 && !params[1].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		//if (params.size() > 1 && params[1].asInt() < 0)
		//	throw RPC_Exception(RPC_INVALID_PARAMETER, negative_count_err);

		if (params.size() > 2 && !params[2].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		//if (params.size() > 2 && params[2].asInt() < 0)
		//	throw RPC_Exception(RPC_INVALID_PARAMETER, negative_from_err);

		if (params.size() > 3 && !params[3].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_listtransactions(
						params.size() > 0 ? params[0].asString() : string("*"),
						params.size() > 1 ? params[1].asUInt64() : 10,
						params.size() > 2 ? params[2].asUInt64() : 0,
						params.size() > 3 ? params[3].asBool() : false,
						dbconn, txquery, rstream);
	}
	else if (method == "listunspent")
	{
		if ( /* params.size() < 0 || */ params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( minconf maxconf  [\\\"destination\\\",...] [include_unsafe] )");

		if (params.size() > 0 && !params[0].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && !params[1].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isArray())
			throw RPC_Exception(RPC_MISC_ERROR, not_array_err);

		btc_listunspent(
						params.size() > 0 ? params[0].asUInt64() : 1,
						params.size() > 1 ? params[1].asUInt64() : 9999999,
						params.size() > 2 ? params[2] : Json::Value(),
						dbconn, txquery, rstream);
	}
	else if (method == "move" && INCLUDE_DEPRECATED)
	{
		bigint_t amount;

		if (params.size() < 3 || params.size() > 5)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"fromaccount\\\" \\\"toaccount\\\" amount ( minconf \\\"comment\\\" )");

		parse_amount(json, params[2], false, amount);

		if (params.size() > 3 && !params[3].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_move(params[0].asString(), params[1].asString(), amount, params.size() > 4 ? params[4].asString() : string(), dbconn, txquery, rstream, add_quotes);
	}
	else if (method == "sendfrom")
	{
		bigint_t amount;

		if (params.size() < 3 || params.size() > 6)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"fromaccount\\\" \\\"todestination\\\" amount ( minconf \\\"comment\\\" \\\"comment-to\\\" )");

		parse_amount(json, params[2], false, amount);

		if (params.size() > 3 && !params[3].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_sendfrom(params[0].asString(), params[1].asString(), amount,
							params.size() > 4 ? params[4].asString() : string(),
							params.size() > 5 ? params[5].asString() : string(),
							dbconn, txquery, rstream);
	}
	else if (method == "sendmany")
	{
		throw RPC_Exception(RPC_MISC_ERROR, method + " has not yet been implemented");
	}
	else if (method == "sendtoaddress")
	{
		bigint_t amount;

		if (params.size() < 2 || params.size() > 5)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"destination\\\" amount ( \\\"comment\\\" \\\"comment-to\\\" subtractfeefromamount )");

		parse_amount(json, params[1], false, amount);

		if (params.size() > 4 && !params[4].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_sendtoaddress(params[0].asString(), amount,
							params.size() > 2 ? params[2].asString() : string(),
							params.size() > 3 ? params[3].asString() : string(),
							params.size() > 4 ? params[4].asBool() : false,
							dbconn, txquery, rstream);
	}
	else if (method == "setaccount" && INCLUDE_DEPRECATED)
	{
		if (params.size() != 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"destination\\\" \\\"account\\\"");

		btc_setaccount(params[0].asString(), params[1].asString(), dbconn, txquery, rstream);
	}
	else if (method == "settxfee")														// implemented trivially
	{
		bigint_t amount;

		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " amount");

		parse_amount(json, params[0], true, amount);

		rstream << "false";
		add_quotes = false;
	}
	else if (method == "getmininginfo")
	{
		// added to try to get bitcoind-ncurses to work
		btc_getmininginfo(dbconn, txquery, rstream);
	}
	else if (method == "getnetworkhashps")												// implemented trivially
	{
		// added to try to get bitcoind-ncurses to work
		rstream << "1.0";
		add_quotes = false;
	}
	else if (method == "getrawtransaction")
	{
		throw txrpc_not_implemented_error;

		// used to try to get bitcoind-ncurses to work:
		rstream <<
		"01000000018cd20d4bbcb56b70b25d4ca697b75fe260b376e6b94c13d00d9b876d06fbacbc010000"
		"006a47304402204abf6a949d0f4f1101399f2457bd47216a40859c694930dbbfbda9b5c5735f8e02"
		"20265f309354e47eb25b7368d9d7951a8f25368e09a9e90e497894aa7bd8e723a3012102dec2908b"
		"36e2087e96c2678df6ee45537ee875c7872dd81fb0078744858fd89dffffffff0270357d14000000"
		"001976a9147fac6269f55bc9b65c57fb915ab9739c68c5885d88ac1b90fe9f120000001976a91495"
		"ce9dd18e0fa0f8669bb8344aa06ee11af23b7588ac00000000";
		add_quotes = false;
	}
	else if (method == "cc.time")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		rstream << time(NULL);
		add_quotes = false;
	}
	else if (method == "cc.mint")
	{
		if (params.size() == 0)
			cc_mint(dbconn, txquery, rstream);
		else if ((params[0].asString() == "start" || params[0].asString() == "threads") && params.size() <= 2)
		{
			int nthreads = thread::hardware_concurrency();
			nthreads = (nthreads*2 + 2) / 3;

			if (nthreads < 0)
				nthreads = 4;
			if (nthreads > CC_MINT_MAX_THREADS)
				nthreads = CC_MINT_MAX_THREADS;

			if (params.size() > 1)
			{
				if (!params[1].isConvertibleTo(Json::intValue))
					throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

				nthreads = params[1].asInt();

				if (nthreads < 0 || nthreads > CC_MINT_MAX_THREADS)
					throw RPC_Exception(RPC_INVALID_PARAMETER, "nthreads should be from 0 to " STRINGIFY(CC_MINT_MAX_THREADS));
			}

			cc_mint_threads(nthreads, dbconn, txquery, rstream);
		}
		else if (params[0].asString() == "stop" && params.size() <= 1)
			cc_mint_threads(0, dbconn, txquery, rstream);
		else
			throw RPC_Exception(RPC_MISC_ERROR, method);
	}
	else if (method == "cc.poll_destination")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination (last_received_max)");

		if (params.size() > 1 && !params[1].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		//if (params.size() > 1 && params[1].asInt() < 0)
		//	throw RPC_Exception(RPC_INVALID_PARAMETER, negative_from_err);

		cc_poll_destination(params[0].asString(), params.size() > 1 ? params[1].asUInt64() : 0, dbconn, txquery, rstream);
	}
	else if (method == "cc.poll_mint")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		cc_poll_mint(dbconn, txquery, rstream);
	}
	else
	{
		throw RPC_Exception(RPC_METHOD_NOT_FOUND, "Method not found");
	}
}

static bool do_one_rpc(const string& json, Json::Value& root, DbConn *dbconn, TxQuery& txquery, ostringstream& response)
{
	if (0 && TRACE_JSONRPC) BOOST_LOG_TRIVIAL(info) << "do_one_rpc " << json;

	Json::Value value, params;
	string key, id_value;

	bool version1 = true;
	bool notification = false;
	const char *version = "";
	const char *id = "null";
	const char *null_result = "\"result\":null,";
	const char *null_error = "\"error\":null,";

	string method;
	ostringstream rstream;
	bool add_quotes = true;
	bool rc = true;

	key = "jsonrpc";
	if (root.removeMember(key, &value))
	{
		version1 = false;
		version = "\"jsonrpc\":\"2.0\",";	// no matter what the client sent for jsonrpc, this sends back "2.0"
		null_result = "";
		null_error = "";
	}

	key = "id";
	if (root.removeMember(key, &value))
	{
		if (!value.isNull())
		{
			id_value = value.asString();
			if (value.isString())
				id_value = string("\"") + id_value + "\"";	// echo what the client sent
			id = id_value.c_str();
		}
		else if (version1)
			notification = true;
	}
	else if (!version1)
		notification = true;

	key = "method";
	if (!root.removeMember(key, &value) && !value.isNull())
	{
		copy_error_to_response(RPC_INVALID_REQUEST, "Missing method", version, id, null_result, notification, response);
		goto done;
	}

	method = value.asString();

	key = "params";
	if (!root.removeMember(key, &params))
		params = Json::arrayValue;
	else if (!params.isArray())
	{
		copy_error_to_response(RPC_INVALID_REQUEST, "Params must be an array", version, id, null_result, notification, response);
		goto done;
	}

	try
	{
		try_one_rpc(json, method, params, dbconn, txquery, rstream, add_quotes);

		copy_result_to_response(add_quotes, rstream.str(), version, id, null_error, notification, response);

		rc = false;
	}
	catch (const RPC_Exception& e)
	{
		if (0 && TRACE_JSONRPC) BOOST_LOG_TRIVIAL(info) << "do_one_rpc " << json << " RPC_Exception code " << e.code << " " << e.what();
		//cerr << "caught RPC_Exception code " << e.code << " " << e.what() << endl;

		copy_error_to_response(e.code, e.what(), version, id, null_result, notification, response);
	}

done:

	if (TRACE_JSONRPC) BOOST_LOG_TRIVIAL(info) << "do_one_rpc rc " << (int)rc << " json " << json << " response " << (response.str().length() <= 120 ? response.str() : response.str().substr(0,120) + "...");

	return rc;
}

int do_json_rpc(const string& json, DbConn *dbconn, TxQuery& txquery, ostringstream& response)
{
	if (TRACE_JSONRPC) BOOST_LOG_TRIVIAL(trace) << "do_json_rpc: " << json;

	int result = 0;
	Json::CharReaderBuilder builder;
	Json::CharReaderBuilder::strictMode(&builder.settings_);
	Json::Value root;
	unsigned n = 1;

	auto reader = builder.newCharReader();

	bool rc;

	try
	{
		rc = reader->parse(json.c_str(), json.c_str() + json.length(), &root, NULL);
	}
	catch (...)
	{
		rc = false;
	}

	delete reader;

	if (!rc)
	{
		copy_error_to_response(RPC_PARSE_ERROR, "Parse error", "", "null", "", false, response);
		result = -1;
		goto done;
	}

	if (root.isArray())
	{
		n = root.size();
		response << "[";
	}

	for (unsigned i = 0; i < n; ++i)
	{
		rc = do_one_rpc(json, (root.isArray() ? root[i] : root), dbconn, txquery, response);

		if (rc)
		{
			result = 1;
			break;	// !!! stop processing requests after error?
		}
	}

	if (root.isArray())
	{
		if (response.str().length() <= 1)	// JSON-RPC 2.0: don't send empty array
			response.str(string());
		else
			response << "]";
	}

done:

	if (0 && TRACE_JSONRPC) BOOST_LOG_TRIVIAL(trace) << "do_json_rpc result " << result << " response: " << response.str();

	return result;
}
