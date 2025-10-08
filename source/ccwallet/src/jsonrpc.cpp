/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
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
#include "transactions.hpp"

#include <CCparams.h>
#include <CCobjdefs.h>
#include <jsonutil.h>
#include <xtransaction.hpp>
#include <xtransaction-xreq.hpp>
#include <txquery.h>

#include <jsoncpp/json/json.h>

#define TRACE_JSONRPC	(g_params.trace_jsonrpc)

//#define TEST_INCLUDE_QUICK_TEST	1

#define INCLUDE_DEPRECATED		1
#define INCLUDE_BITCOIN_BUGS	1

#define STDARGS		dbconn, txquery, rstream
#define STDARGSAQ	STDARGS, add_quotes

#ifndef TEST_INCLUDE_QUICK_TEST
#define TEST_INCLUDE_QUICK_TEST		0	// don't test
#endif

using namespace snarkfront;

volatile bool g_disable_malloc_logging = false;

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

static const char *not_num_err = "JSON value is not a number as expected";
static const char *not_bool_err = "JSON value is not a boolean as expected";
static const char *not_int_err = "JSON value is not an integer as expected";
static const char *not_array_err = "JSON value is not an array as expected";

//static const char *invalid_param_err = "Invalid parameter";
static const char *invalid_amount_err = "Invalid amount";

static const char *tx_not_in_mempool = "Transaction not in mempool";
//static const char *negative_count_err = "Negative count";
//static const char *negative_from_err = "Negative from";

static const char *only_verbose_err = "only verbose=true is supported";

static bool add_comma(const ostringstream& response)
{
	return (response.str().length() > 1);
}

static void copy_result_to_response(bool add_quotes, const string& msg, const char *version, const char *id, const char *error, bool notification, ostringstream& response)
{
	// !!! TODO?: JSON-RPC 2.0: don't send response if no id

	if (msg.length() && (msg[0] == '{' or msg[0] == '['))
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
	amtfloat_t amountf, amountf2;

	auto rc = parse_float_value(json.data(), value, amountf);
	if (rc)
		throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

	if (amountf < 0 || (amountf == 0 && !allow_zero))
		throw RPC_Exception(RPC_TYPE_ERROR, invalid_amount_err);

	const uint64_t asset = 0;
	rc = amount_from_float(asset, amountf, amount);
	if (rc)
		throw RPC_Exception(RPC_TYPE_ERROR, invalid_amount_err);

	amount_to_float(asset, amount, amountf2);
	if (amountf2 < amountf)
		addBigInt(amount, bigint_t(1UL), amount, false);	// round up
}

static bool is_power_of_10(bigint_t& amount)
{
	if (!amount)
		return true;

	bigint_t check(1UL);

	for (unsigned i = 0; i < 77; ++i)
	{
		for (unsigned j = 1; j < 10; ++j)
		{
			switch (j)
			{
			default:
				continue;
			case 1:
			case 2:
			case 3:
			case 5:
			case 7:
				break;
			}

			bigint_t checkm;

			mulBigInt(check, bigint_t(j), checkm, false);

			//cerr << i << " " << j << " " << checkm << endl;

			if (amount == checkm)
				return true;

			if (amount < checkm)
				return false;
		}

		mulBigInt(check, bigint_t(10UL), check, false);
	}

	return false;
}

static double adjust_exchange_costs(double amount)
{
	if (!amount)
		return 0;

	const unsigned nbases = 17;
	const unsigned bases[nbases] = {100, 115, 130, 150, 175, 200, 230, 265, 300, 350, 400, 460, 525, 600, 700, 800, 900};

	//amount = DBL_MAX;	// for testing

	for (int i = -308; i < 308; ++i)
	{
		double div = 1;
		double mult = 1;

		for (int j = 0; j < -i; ++j)
			div *= 10;

		for (int j = 0; j < i; ++j)
			mult *= 10;

		for (unsigned j = 0; j < nbases; ++j)
		{
			auto round = bases[j] * mult / div;

			//cout << i << " " << div << " " << mult << " " << j << " " << round << endl;

			if (amount <= round)
				return round;
		}
	}

	return INFINITY;
}

static void crosschain_parse_foreign_asset(string &foreign_asset, uint64_t &quote_asset)
{
	boost::algorithm::to_lower(foreign_asset);

	if (foreign_asset == XREQ_SYMBOL_BTC)
		quote_asset = XREQ_BLOCKCHAIN_BTC;
	else if (foreign_asset == XREQ_SYMBOL_BCH)
		quote_asset = XREQ_BLOCKCHAIN_BCH;
	else
		throw RPC_Exception(RPC_INVALID_PARAMETER, "cryptoasset must be \\\"" XREQ_SYMBOL_BTC "\\\" or \\\"" XREQ_SYMBOL_BCH "\\\"");

	foreign_asset.clear();
}

// parse: type min_amount max_amount rate costs cryptoasset
static void crosschain_parse(bool is_query, const string& json, Json::Value& params, unsigned poffset, unsigned& xcx_type, bigint_t& min_amount, bigint_t& max_amount, double& rate, double& costs, uint64_t& quote_asset, string& foreign_asset)
{
	auto cmd = params[poffset].asString();

	if (cmd == "sb" || cmd == "simple_buy")
		 xcx_type = CC_TYPE_XCX_SIMPLE_BUY;
	else if (cmd == "ss" || cmd == "simple_sell")
		xcx_type = CC_TYPE_XCX_SIMPLE_SELL;
	else if (cmd == "mt" || cmd == "mining_trade" || cmd == "st" || cmd == "simple_trade")
		xcx_type = CC_TYPE_XCX_MINING_TRADE;
	else if (cmd == "nb" || cmd == "naked_buy")
		 xcx_type = CC_TYPE_XCX_NAKED_BUY;
	else if (cmd == "ns" || cmd == "naked_sell")
		xcx_type = CC_TYPE_XCX_NAKED_SELL;
	else
		throw RPC_Exception(RPC_INVALID_PARAMETER, "request must be \\\"simple_buy\\\", \\\"simple_sell\\\", \\\"naked_buy\\\", \\\"naked_sell\\\", or \\\"mining_trade\\\"");

	if (is_query && xcx_type == CC_TYPE_XCX_MINING_TRADE)
		throw RPC_Exception(RPC_INVALID_PARAMETER, "mining_trade type is not valid in a query operation");

	parse_amount(json, params[poffset + 1], true, min_amount);
	parse_amount(json, params[poffset + 2], true, max_amount);

	bigint_t min_min, max_max;
	amount_from_float(0, (amtfloat_t)STRINGIFY(XCX_REQ_MIN), min_min);
	amount_from_float(0, (amtfloat_t)STRINGIFY(XCX_REQ_MAX), max_max);

	//cerr << min_amount << " " << min_min << " " << min_min - min_amount << endl;

	if (!is_query)
	{
		if (min_amount < min_min || max_amount < min_min)
			throw RPC_Exception(RPC_INVALID_PARAMETER, "min_amount and max_amount must be >= " STRINGIFY(XCX_REQ_MIN));

		if (min_amount > max_max || max_amount > max_max)
			throw RPC_Exception(RPC_INVALID_PARAMETER, "min_amount and max_amount must be <= " STRINGIFY(XCX_REQ_MAX));

		if (xcx_type == CC_TYPE_XCX_MINING_TRADE && min_amount != max_amount)
			throw RPC_Exception(RPC_INVALID_PARAMETER, "min_amount must equal max_amount for trade request");

		if (!is_power_of_10(min_amount) || !is_power_of_10(max_amount))
			throw RPC_Exception(RPC_INVALID_PARAMETER, "min_amount and max_amount must be 1, 2, 3, 5 or 7 multiplied by a power of 10");
	}

	if (min_amount > max_amount && (!is_query || max_amount))
		throw RPC_Exception(RPC_INVALID_PARAMETER, "min_amount must be <= max_amount");

	if (!params[poffset + 3].isNumeric())
		throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

	rate = params[poffset + 3].asDouble();

	if (rate < 0)
		throw RPC_Exception(RPC_INVALID_PARAMETER, "exchange rate must be positive");

	if (!params[poffset + 4].isNumeric())
		throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

	auto nomcosts = params[poffset + 4].asDouble();

	if (xcx_type == CC_TYPE_XCX_MINING_TRADE && nomcosts)
		throw RPC_Exception(RPC_INVALID_PARAMETER, "costs must be zero for trade request");

	costs = adjust_exchange_costs(nomcosts);

	//cerr << "costs-1 " << costs - 1 << " nomcosts-1 " << nomcosts - 1 << " diff " << costs - nomcosts << endl;

	if (!isfinite(costs))
		throw RPC_Exception(RPC_INVALID_PARAMETER, "costs overflow");

	foreign_asset = params[poffset + 5].asString();

	crosschain_parse_foreign_asset(foreign_asset, quote_asset);

	if (xcx_type == CC_TYPE_XCX_MINING_TRADE && quote_asset != XREQ_BLOCKCHAIN_BCH)
		throw RPC_Exception(RPC_INVALID_PARAMETER, "cryptoasset must be bch for a trade request");

	if (costs != nomcosts && IsInteractive())
	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Note: for better privacy, the costs have been rounded up to " << costs << "\n" << endl;
	}

	if (0 && TRACE_JSONRPC)
	{
		ostringstream s;
		s.precision(numeric_limits<double>::max_digits10);
		s << "xcx_type " << xcx_type;
		s << " min_amount " << min_amount;
		s << " max_amount " << max_amount;
		s << " rate " << rate;
		s << " costs " << costs;
		s << " quote_asset " << quote_asset;
		s << " foreign_asset " << foreign_asset;

		BOOST_LOG_TRIVIAL(debug) << "crosschain_parse returning " << s.str();
	}
}

static void try_one_rpc(const string& json, const string& method, Json::Value& params, RPC_STDPARAMSAQ)
{
	//throw RPC_Exception((RPCErrorCode)(-9), "throw");

	if (method == "stop")
	{
		start_shutdown();	// !!! needs a response string?
	}
	else if (method == "help")															// implemented
	//for (unsigned i = 0; i < 900; ++i)
	{
		rstream <<
		"\\n"
		"==CredaCash Commands===\\n"
		"stop - shutdown the wallet (leaves the network node server running)\\n"
		"ping - queries the transaction server and returns the ping time in seconds\\n"
		"cc.time - returns the wallet's current time (localtime)\\n"
		"cc.pause seconds - useful in automated scripts\\n"
		"cc.remark (comment) - useful to document scripts\\n"
		"#(comment) - useful to disable lines in script\\n"
		"cc.mint - mint currency (if allowed by blockchain)\\n"
		"cc.unique_id_generate ( prefix random_bits checksum_chars ) - generate a unique id\\n"
		"cc.donation_estimate tx_type ( n_inputs n_outputs ) - estimate transaction donation\\n"
		"cc.send reference_id destination asset amount\\n"
		"cc.send_async reference_id destination asset amount\\n"
		"cc.transaction_cancel txid - cancel transaction\\n"
		"cc.transactions_list ( start end limit ) - list transactions\\n"
		"cc.destination_poll destination ( polling_addresses last_received_max ) - check destination for incoming payments\\n"
		"cc.list_change_destinations - Diagnostic - list destinations used for change\\n"
		"cc.billets_list_unspent ( statuses min_amount ) - Diagnostic - list unspent billets\\n"
		"cc.billets_poll_unspent - Diagnostic - poll and update unspent billets\\n"
		"cc.billets_release_allocated ( reset_balance ) - Diagnostic - release all billets allocated to transactions\\n"
		"cc.crosschain_query_requests \\\"simple_buy|simple_sell|naked_buy|naked_sell\\\" min_amount max_amount rate costs cryptoasset ( count offset include_pending_matched ) - query crosschain exchange requests\\n"
		"cc.crosschain_query_pending_matches \\\"simple_buy|simple_sell|naked_buy|naked_sell\\\" min_amount max_amount rate costs cryptoasset ( count offset ) - query crosschain exchange pending matches\\n"
		"cc.crosschain_request_create reference_id \\\"simple_buy|simple_sell|naked_buy|naked_sell|mining_trade\\\" min_amount max_amount rate costs cryptoasset ( unique_foreign_address expiration wait_discount ) - create a CredaCash exchange request\\n"
		"cc.crosschain_request_create_async reference_id \\\"simple_buy|simple_sell|naked_buy|naked_sell|mining_trade\\\" min_amount max_amount rate costs cryptoasset ( unique_foreign_address expiration wait_discount )\\n"
		"cc.crosschain_request_create_local reference_id \\\"simple_buy|simple_sell|naked_buy|naked_sell|mining_trade\\\" min_amount max_amount rate costs cryptoasset ( unique_foreign_address expiration wait_discount )\\n"
		"cc.broadcast reference_id data\\n"
		"cc.exchange_requests_pending_totals cryptoasset - get this wallet's total amount of pending exchange requests for the cryptoasset\\n"
		"cc.exchange_query_mining_info - query crosschain exchange mining information\\n"
		//"cc.exchange_request_info key id - return info on a crosschain request; key must = \"reqnum\"\\n"
		//"cc.exchange_request_list_matches - not yet implemented\\n"
		"cc.exchange_match_info matchnum - return info on a crosschain match\\n"
		"cc.crosschain_match_action_list ( minutes_until_deadline override_reminder_times ) - list crosschain matches requiring action\\n"
		"cc.crosschain_match_mark_paid match_number ( foreign_txid reminder_minutes minimum_advance_minutes ) - mark match paid with reminder to submit payment advice, or resets reminder time only if foreign_txid is not set\\n"
		"cc.crosschain_payment_claim reference_id match_number ( foreign_block_id foreign_payment_identifier amount reminder_minutes minimum_advance_minutes ) - submit payment advice to settle crosschain transaction\\n"
		"cc.crosschain_payment_claim_async reference_id match_number ( foreign_block_id foreign_payment_identifier amount reminder_minutes minimum_advance_minutes )\\n"
		"cc.dump_secrets type ( parent start count ) - Diagnostic - dump secrets in internal format\\n"
		"cc.dump_transactions ( start count show_billets ) - Diagnostic - dump transactions in internal format\\n"
		"cc.dump_billets ( start count show_spends ) - Diagnostic - dump billets in internal format\\n"
		"cc.dump_tx_build - Diagnostic - dump transaction build threads in internal format\\n"
		"cc.dump_exchange_requests ( start count show_transactions ) - Diagnostic - dump exchange requests in internal format\\n"
		"cc.dump_exchange_matches ( start count show_requests show_transactions ) - Diagnostic - dump exchange matches in internal format\\n"
		"\\n"
		"==Bitcoin Compatibility Commands==\\n"
		"getbestblockhash\\n"
		//"getblock hash ( verbose )\\n"
		"getblockchaininfo\\n"
		"getblockcount - returns compatibility block count\\n"
		"getblockhash index\\n"
		//"getblockheader hash ( verbose )\\n"
		"getchaintips\\n"
		"getdifficulty - reports constant value\\n"
		//"gettxout txid n ( includemempool )\\n"
		//"gettxoutproof [txid,...] ( blockhash )\\n"
		//"gettxoutsetinfo\\n"
		//"verifychain ( checklevel numblocks )\\n"
		//"verifytxoutproof proof\\n"
		"getinfo\\n"
		"getmininginfo - not supported\\n"
		"estimatefee nblocks - estimates donation for baseline 2-in 2-out transaction\\n"
		"validateaddress destination\\n"
		"abandontransaction txid\\n"
		//"backupwallet destination\\n"
		//"dumpwallet filename\\n"
		//"encryptwallet passphrase\\n"
		//"walletpassphrase passphrase\\n"
		//"walletlock\\n"
		"getaccount destination - only default account currently supported\\n"									// DEPRECATED in bitcoin
		//"getaccountaddress account\\n"																		// DEPRECATED in bitcoin
		//"getaddressesbyaccount account\\n"																	// DEPRECATED in bitcoin
		"getbalance ( account minconf includeWatchonly )\\n"
		"getnewaddress ( account )\\n"
		"getreceivedbyaccount account ( minconf )\\n"															// DEPRECATED in bitcoin
		"getreceivedbyaddress destination ( minconf )\\n"
		"gettransaction txid ( includeWatchonly )\\n"
		"getunconfirmedbalance\\n"
		"getwalletinfo\\n"
		//"keypoolrefill ( newsize )\\n"
		"listaccounts ( minconf includeWatchonly )\\n"																	// DEPRECATED in bitcoin
		//"listaddressgroupings\\n"
		//"listlockunspent\\n"
		"listreceivedbyaccount ( minconf includeempty includeWatchonly )\\n"											// DEPRECATED in bitcoin
		"listreceivedbyaddress ( minconf includeempty includeWatchonly destination_filter )\\n"
		"listsinceblock ( blockhash target-confirmations includeWatchonly )\\n"
		"listtransactions ( account count from includeWatchonly )\\n"
		"listunspent ( minconf maxconf [destination,...] [include_unsafe] ) - for diagnostic purposes only\\n"
		//"move fromaccount toaccount amount ( minconf comment )\\n"							// DEPRECATED in bitcoin
		//"sendfrom fromaccount todestination amount ( minconf comment comment-to )\\n"
		//"sendmany fromaccount {destination:amount,...} ( minconf comment [destination,...] )\\n"
		"sendtoaddress destination amount ( comment comment-to subtractfeefromamount )\\n"
		//"setaccount destination account\\n"															// DEPRECATED in bitcoin
		//"settxfee amount"
		;
	}
	else if (method == "getbestblockhash")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getbestblockhash(STDARGS);
	}
	else if (method == "getblock")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " hash ( verbose )");

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		if (params.size() > 1 && params[1].asBool() != true)
			throw RPC_Exception(RPC_MISC_ERROR, only_verbose_err);

		btc_getblock(params[0].asString(), STDARGS);
	}
	else if (method == "getblockchaininfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getblockchaininfo(STDARGS);
	}
	else if (method == "getblockcount")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getblockcount(STDARGSAQ);
	}
	else if (method == "getblockhash")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " index");

		if (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_getblockhash(params[0].asUInt64(), STDARGS);
	}
	else if (method == "getblockheader")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " hash ( verbose )");

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		if (params.size() > 1 && params[1].asBool() != true)
			throw RPC_Exception(RPC_MISC_ERROR, only_verbose_err);

		btc_getblockheader(params[0].asString(), STDARGS);
	}
	else if (method == "getchaintips")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getchaintips(STDARGS);
	}
	else if (method == "getdifficulty")													// implemented trivially
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		// difficulty is always 1.0

		if (IsInteractive())
		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
			cerr << "CredaCash does not support proof-of-work mining.\n" << endl;
		}

		rstream << "1.0";
		add_quotes = false;
	}
	else if ((method == "getmempoolancestors" || method == "getmempooldescendants"))	// implemented trivially
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " txid ( verbose )");

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		// not clear how the mempool would be useful, so this wallet always considers it to be empty

		throw RPC_Exception(RPC_INVALID_ADDRESS_OR_KEY, tx_not_in_mempool);
	}
	else if (method == "getmempoolentry")												// implemented trivially
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " txid");

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
			throw RPC_Exception(RPC_MISC_ERROR, method + " txid n ( includemempool )");

		if (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_gettxout(params[0].asString(), params[1].asUInt64(), STDARGS);
	}
	else if (method == "gettxoutproof")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " [txid,...] ( blockhash )");

		if (!params[0].isArray())
			throw RPC_Exception(RPC_MISC_ERROR, not_array_err);

		btc_gettxoutproof(params[0], params[1].asString(), STDARGS);
	}
	else if (method == "gettxoutsetinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_gettxoutsetinfo(STDARGS);
	}
	else if (method == "verifychain")
	{
		if ( /* params.size() < 0 || */ params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( checklevel numblocks )");

		if (params.size() > 0 && (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_verifychain(params.size() > 0 ? params[0].asUInt64() : 3, params.size() > 1 ? params[1].asUInt64() : 6, STDARGSAQ);
	}
	else if (method == "verifytxoutproof")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " proof");

		btc_verifytxoutproof(params[0].asString(), STDARGS);
	}
	else if (method == "getinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getinfo(STDARGS);
	}
	else if (method == "getconnectioncount")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getconnectioncount(STDARGSAQ);
	}
	else if (method == "getnettotals")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getnettotals(STDARGS);
	}
	else if (method == "getnetworkinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getnetworkinfo(STDARGS);
	}
	else if (method == "getpeerinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getpeerinfo(STDARGS);
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

		btc_ping(STDARGSAQ);
	}
	else if (method == "setban")														// implemented trivially
	{
		if (params.size() < 2 || params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ip(/netmask) \\\"add|remove\\\" ( bantime absolute )");

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

		cc_donation_estimate(0, 0, 0, STDARGSAQ);
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
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination");

		btc_validateaddress(params[0].asString(), STDARGS);
	}
	else if (method == "abandontransaction")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " txid");

		btc_abandontransaction(params[0].asString(), STDARGS);
	}
	else if (method == "backupwallet")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination");

		btc_backupwallet(params[0].asString(), STDARGS);
	}
	else if (method == "dumpwallet")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination");

		btc_dumpwallet(params[0].asString(), STDARGS);
	}
	else if (method == "encryptwallet")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " passphrase");

		btc_encryptwallet(params[0].asString(), STDARGS);
	}
	else if (method == "walletpassphrase")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " passphrase");

		btc_walletpassphrase(params[0].asString(), STDARGS);
	}
	else if (method == "walletlock")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_walletlock(STDARGS);
	}
	else if (method == "getaccount" && INCLUDE_DEPRECATED)
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination");

		btc_getaccount(params[0].asString(), STDARGS);
	}
	else if (method == "getaccountaddress" && INCLUDE_DEPRECATED)
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " account");

		btc_getaccountaddress(params[0].asString(), STDARGS);
	}
	else if (method == "getaddressesbyaccount" && INCLUDE_DEPRECATED)
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " account");

		btc_getaddressesbyaccount(params[0].asString(), STDARGS);
	}
	else if (method == "getbalance")
	{
		if ( /* params.size() < 0 || */ params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( account minconf includeWatchonly )");

		if (params.size() > 1 && !params[1].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_getbalance(params.size() > 0 ? params[0].asString() : "*", params.size() > 2 ? params[2].asBool() : false, STDARGSAQ);
	}
	else if (method == "getnewaddress")
	{
		if ( /* params.size() < 0 || */ params.size() > 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( account )");

		btc_getnewaddress(params.size() > 0 ? params[0].asString() : "", STDARGS);
	}
	else if (method == "getreceivedbyaccount" && INCLUDE_DEPRECATED)
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " account ( minconf )");

		if (params.size() > 1 && !params[1].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_getreceivedbyaccount(params[0].asString(), STDARGSAQ);
	}
	else if (method == "getreceivedbyaddress")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination ( minconf )");

		if (params.size() > 1 && !params[1].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_getreceivedbyaddress(params[0].asString(), STDARGSAQ);
	}
	else if (method == "gettransaction")
	{
		if (params.size() < 1 || params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " txid ( includeWatchonly )");

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_gettransaction(params[0].asString(), params.size() > 1 ? params[1].asBool() : false, STDARGS);
	}
	else if (method == "getunconfirmedbalance")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getunconfirmedbalance(STDARGSAQ);
	}
	else if (method == "getwalletinfo")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_getwalletinfo(STDARGS);
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

		btc_listaccounts(params.size() > 1 ? params[1].asBool() : false, STDARGS);
	}
	else if (method == "listaddressgroupings")
	{
		if (params.size() != 0 && !INCLUDE_BITCOIN_BUGS)	// bitcoin 13.1 erroneously doesn't enforce this
			throw RPC_Exception(RPC_MISC_ERROR, method);

		btc_listaddressgroupings(STDARGS);
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

		btc_listreceivedbyaccount(params.size() > 1 ? params[1].asBool() : false, params.size() > 2 ? params[2].asBool() : false, STDARGS);
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

		btc_listreceivedbyaddress(params.size() > 1 ? params[1].asBool() : false, params.size() > 2 ? params[2].asBool() : false, params.size() > 3 ? params[3].asString() : string(), STDARGS);
	}
	else if (method == "listsinceblock")
	{
		if ( /* params.size() < 0 || */ params.size() > 3 && !INCLUDE_BITCOIN_BUGS)	// bitcoin 13.1 erroneously doesn't enforce this
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( blockhash target-confirmations includeWatchonly )");

		if (params.size() > 1 && !params[1].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		//if (params.size() > 1 && params[1].asInt() < 1)	// asInt can throw Json::LogicError
		//	throw RPC_Exception(RPC_INVALID_PARAMETER, invalid_param_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		btc_listsinceblock(params.size() > 0 ? params[0].asString() : string(), params.size() > 2 ? params[2].asBool() : false, STDARGS);
	}
	else if (method == "listtransactions")
	{
		if ( /* params.size() < 0 || */ params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( account count from includeWatchonly )");

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		//if (params.size() > 1 && params[1].asInt() < 0)
		//	throw RPC_Exception(RPC_INVALID_PARAMETER, negative_count_err);

		if (params.size() > 2 && (!params[2].isIntegral() || !params[2].isConvertibleTo(Json::uintValue)))
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
						STDARGS);
	}
	else if (method == "listunspent")
	{
		if ( /* params.size() < 0 || */ params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( minconf maxconf  [destination,...] [include_unsafe] )");

		if (params.size() > 0 && (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isArray())
			throw RPC_Exception(RPC_MISC_ERROR, not_array_err);

		cc_billets_list_unspent(0, 0UL, STDARGS);
	}
	else if (method == "move" && INCLUDE_DEPRECATED)
	{
		bigint_t amount;

		if (params.size() < 3 || params.size() > 5)
			throw RPC_Exception(RPC_MISC_ERROR, method + " fromaccount toaccount amount ( minconf comment )");

		parse_amount(json, params[2], false, amount);

		if (params.size() > 3 && !params[3].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_move(params[0].asString(), params[1].asString(), amount, params.size() > 4 ? params[4].asString() : string(), STDARGSAQ);
	}
	else if (method == "sendfrom")
	{
		bigint_t amount;

		if (params.size() < 3 || params.size() > 6)
			throw RPC_Exception(RPC_MISC_ERROR, method + " fromaccount todestination amount ( minconf comment comment-to )");

		parse_amount(json, params[2], false, amount);

		if (params.size() > 3 && !params[3].isIntegral())
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		btc_sendfrom(params[0].asString(), params[1].asString(), amount,
							params.size() > 4 ? params[4].asString() : string(),
							params.size() > 5 ? params[5].asString() : string(),
							STDARGS);
	}
	else if (method == "sendmany")
	{
		throw RPC_Exception(RPC_MISC_ERROR, method + " has not yet been implemented");
	}
	else if (method == "sendtoaddress")
	{
		if (params.size() < 2 || params.size() > 5)
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination amount ( comment comment-to subtractfeefromamount )");

		bigint_t amount;
		parse_amount(json, params[1], false, amount);

		if (params.size() > 4 && !params[4].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		cc_send(false, "", params[0].asString(), amount,
				params.size() > 2 ? params[2].asString() : string(),
				params.size() > 3 ? params[3].asString() : string(),
				params.size() > 4 ? params[4].asBool() : false,
				STDARGS);
	}
	else if (method == "setaccount" && INCLUDE_DEPRECATED)
	{
		if (params.size() != 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination account");

		btc_setaccount(params[0].asString(), params[1].asString(), STDARGS);
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
		btc_getmininginfo(STDARGS);
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

		rstream << unixtime();
		add_quotes = false;
	}
	else if (method == "cc.pause")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " seconds");

		auto duration = params[0].asInt();

		if (duration < 0)
			throw RPC_Exception(RPC_INVALID_PARAMETER, "duration must be positive");

		ccsleep(duration);
	}
	else if (method == "cc.remark" || method[0] == '#')															// implemented
	{
		;;;
	}
	else if (method == "cc.mint")
	{
		if (params.size() == 0)
			cc_mint(STDARGS);
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
				if (!params[1].isIntegral())
					throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

				nthreads = params[1].asInt();

				if (nthreads < 0 || nthreads > CC_MINT_MAX_THREADS)
					throw RPC_Exception(RPC_INVALID_PARAMETER, "nthreads should be from 0 to " STRINGIFY(CC_MINT_MAX_THREADS));
			}

			cc_mint_threads(nthreads, STDARGS);
		}
		else if (params[0].asString() == "stop" && params.size() <= 1)
			cc_mint_threads(0, STDARGS);
		else
			throw RPC_Exception(RPC_MISC_ERROR, method);
	}
	else if (method == "cc.unique_id_generate")
	{
		if ( /* params.size() < 0 || */ params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( prefix random_bits checksum_chars )");

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && (!params[2].isIntegral() || !params[2].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		cc_unique_id_generate(params.size() > 0 ? params[0].asString() : "R", params.size() > 1 ? params[1].asUInt() : 0, params.size() > 2 ? params[2].asUInt() : 2, STDARGS);
	}
	else if (method == "cc.donation_estimate")
	{
		if (params.size() < 1 || params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " tx_type ( n_inputs n_outputs )");

		if (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && (!params[2].isIntegral() || !params[2].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		cc_donation_estimate(params[0].asUInt(), params.size() > 1 ? params[1].asUInt() : 0, params.size() > 2 ? params[2].asUInt() : 0, STDARGSAQ);
	}
	else if (method == "cc.send" || method == "cc.send_async")
	{
		if (params.size() != 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " reference_id destination asset amount");

		if (!params[2].isIntegral() || !params[2].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params[2].asUInt64())
			throw RPC_Exception(RPC_INVALID_PARAMETER, "asset must be 0");

		bigint_t amount;
		parse_amount(json, params[3], false, amount);

		cc_send(method.find("_async") != string::npos, params[0].asString(), params[1].asString(), amount,
				params.size() > 4 ? params[4].asString() : string(),
				params.size() > 5 ? params[5].asString() : string(),
				params.size() > 6 ? params[6].asBool() : false,
				STDARGS);
	}
	else if (method == "cc.transaction_cancel")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " txid");

		cc_transaction_cancel(params[0].asString(), STDARGS);
	}
	else if (method == "cc.transactions_list")
	{
		if ( /* params.size() < 0 || */ params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( start end limit )");

		if (params.size() > 0 && (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && (!params[2].isIntegral() || !params[2].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		cc_transactions_list(params.size() > 0 ? params[0].asUInt() : 0, params.size() > 1 ? params[1].asUInt64() : 0, params.size() > 2 ? params[2].asUInt64() : 0, STDARGS);
	}
	else if (method == "cc.destination_poll")
	{
		if (params.size() < 1 || params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " destination ( polling_addresses last_received_max )");

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && (!params[2].isIntegral() || !params[2].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		//if (params.size() > 3 && params[3].asInt() < 0)
		//	throw RPC_Exception(RPC_INVALID_PARAMETER, negative_from_err);

		cc_destination_poll(params[0].asString(), params.size() > 1 ? params[1].asUInt() : 20, params.size() > 2 ? params[2].asUInt64() : 0, STDARGS);
	}
	else if (method == "cc.mint_poll")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		cc_mint_poll(STDARGS);
	}
	else if (method == "cc.list_change_destinations")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		cc_list_change_destinations(STDARGS);
	}
	else if (method == "cc.billets_list_unspent")
	{
		if ( /* params.size() < 0 || */ params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( statuses min_amount )");

		unsigned statuses = 0;
		bigint_t min_amount = 0UL;


		if (params.size() > 0)
		{
			if (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue))
				throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

			statuses = params[0].asUInt();

			if (statuses > 5)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "statuses must be <= 5");
		}

		if (params.size() > 1)
			parse_amount(json, params[1], true, min_amount);

		cc_billets_list_unspent(params.size() > 0 ? params[0].asUInt() : 0, min_amount, STDARGS);
	}
	else if (method == "cc.billets_poll_unspent")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		cc_billets_poll_unspent(STDARGS);
	}
	else if (method == "cc.billets_release_allocated")
	{
		if ( /* params.size() < 0 || */ params.size() > 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( reset_balance )");

		if (params.size() > 0 && !params[0].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		cc_billets_release_allocated(params.size() > 0 ? params[0].asBool() : true, STDARGS);
	}
	else if (method == "cc.dump_secrets")
	{
		if ( params.size() < 1 || params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " type ( parent start count )");

		if (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && (!params[2].isIntegral() || !params[2].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 3 && (!params[3].isIntegral() || !params[3].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		cc_dump_secrets(params[0].asUInt(), params.size() > 1 ? params[1].asUInt64() : 0, params.size() > 2 ? params[2].asUInt64() : 0, params.size() > 3 ? params[3].asUInt64() : 0, STDARGS);
	}
	else if (method == "cc.dump_transactions")
	{
		if ( /* params.size() < 0 || */ params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( start count show_billets )");

		if (params.size() > 0 && (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		cc_dump_transactions(params.size() > 0 ? params[0].asUInt64() : 0, params.size() > 1 ? params[1].asUInt64() : 0, params.size() > 2 ? params[2].asBool() : false, STDARGS);
	}
	else if (method == "cc.dump_billets")
	{
		if ( /* params.size() < 0 || */ params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( start count show_spends )");

		if (params.size() > 0 && (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		cc_dump_billets(params.size() > 0 ? params[0].asUInt64() : 0, params.size() > 1 ? params[1].asUInt64() : 0, params.size() > 2 ? params[2].asBool() : false, STDARGS);
	}
	else if (method == "cc.dump_tx_build")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		cc_dump_tx_build(STDARGS);
	}
	else if (method == "cc.dump_exchange_requests")
	{
		if ( /* params.size() < 0 || */ params.size() > 3)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( start count show_transactions )");

		if (params.size() > 0 && (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		cc_dump_exchange_requests(params.size() > 0 ? params[0].asUInt64() : 0, params.size() > 1 ? params[1].asUInt64() : 0, params.size() > 2 ? params[2].asBool() : false, STDARGS);
	}
	else if (method == "cc.dump_exchange_matches")
	{
		if ( /* params.size() < 0 || */ params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( start count show_requests show_transactions )");

		if (params.size() > 0 && (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 1 && (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue)))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		if (params.size() > 3 && !params[3].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		cc_dump_exchange_matches(params.size() > 0 ? params[0].asUInt64() : 0, params.size() > 1 ? params[1].asUInt64() : 0, params.size() > 2 ? params[2].asBool() : false, params.size() > 3 ? params[3].asBool() : false, STDARGS);
	}
	else if (method == "cc.crosschain_query_requests" || method == "cc.crosschain_query_pending_matches")
	{
		unsigned query_recs = (method == "cc.crosschain_query_requests");

		if (params.size() < 6 || params.size() > 8 + query_recs)
			throw RPC_Exception(RPC_MISC_ERROR, method + " \\\"simple_buy|simple_sell|naked_buy|naked_sell\\\" min_amount max_amount rate costs cryptoasset ( count offset" + (query_recs ? " include_pending_matched" : "") + " )");

		unsigned maxret = 0, offset = 0;
		unsigned flags = (query_recs ? 0 : TX_QUERY_XREQS_FLAG_ONLY_PENDING_MATCHED);

		if (params.size() > 6)
		{
			if (!params[6].isIntegral() || !params[6].isConvertibleTo(Json::uintValue))
				throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

			maxret = params[6].asUInt();
		}

		if (params.size() > 7)
		{
			if (!params[7].isIntegral() || !params[7].isConvertibleTo(Json::uintValue))
				throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

			offset = params[7].asUInt();
		}

		if (params.size() > 8)
		{
			if (!params[8].isBool())
				throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

			if(params[8].asBool())
				flags = TX_QUERY_XREQS_FLAG_INCLUDE_PENDING_MATCHED;
		}

		unsigned xcx_type;
		bigint_t min_amount, max_amount;
		double rate, costs;
		uint64_t quote_asset;
		string foreign_asset;

		crosschain_parse(true, json, params, 0, xcx_type, min_amount, max_amount, rate, costs, quote_asset, foreign_asset);

		cc_exchange_query_requests(xcx_type, min_amount, max_amount, rate, 0, costs, 0, quote_asset, foreign_asset, maxret, offset, flags, STDARGS);
	}
	else if (method == "cc.crosschain_request_create" || method == "cc.crosschain_request_create_async" || method == "cc.crosschain_request_create_local")
	{
		int mode = (method.find("_async") != string::npos) * Transaction::TX_MODE_ASYNC
				 + (method.find("_local") != string::npos) * Transaction::TX_MODE_PREPARE;

		if (params.size() < 7 || params.size() > 10)
			throw RPC_Exception(RPC_MISC_ERROR, method + " reference_id \\\"simple_buy|simple_sell|naked_buy|naked_sell\\\" min_amount max_amount rate costs cryptoasset ( unique_foreign_address expiration wait_discount )");

		unsigned xcx_type;
		bigint_t min_amount, max_amount;
		double rate, costs;
		uint64_t quote_asset;
		string foreign_asset;

		crosschain_parse(false, json, params, 1, xcx_type, min_amount, max_amount, rate, costs, quote_asset, foreign_asset);

		string foreign_address;
		uint64_t expiration = 0;
		double wait_discount = 1;

		if (params.size() > 7)
		{
			if (Xtx::TypeIsSeller(xcx_type))
				foreign_address = params[7].asString();
			else if (params[7].asString().length() > 1)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "foreign payment address should be left blank for a buy request");
		}

		if (Xtx::TypeIsSeller(xcx_type) && !foreign_address.length())
			throw RPC_Exception(RPC_INVALID_PARAMETER, "foreign payment address must be included in a sell or trade request");

		if (foreign_address.length() > XTX_MAX_ITEM_SIZE + 1)
			throw RPC_Exception(RPC_INVALID_PARAMETER, "foreign payment address length must be <= " + to_string(XTX_MAX_ITEM_SIZE + 1));

		if (params.size() > 8)
		{
			if (!params[8].isIntegral() || !params[8].isConvertibleTo(Json::uintValue))
				throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

			expiration = params[8].asUInt64();

			unsigned min_exp = XREQ_SIMPLE_HOLD_TIME + XREQ_MIN_POSTHOLD_TIME + 20;

			if ((expiration && expiration < min_exp) || (expiration > 365*24*60*60 && (uint64_t)unixtime() + min_exp > expiration))
				throw RPC_Exception(RPC_INVALID_PARAMETER, "expiration must be >= " + to_string(min_exp) + " seconds");

			if (expiration > XREQ_MAX_EXPIRE_TIME && expiration <= 365*24*60*60)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "expiration must be <= " + to_string(XREQ_MAX_EXPIRE_TIME) + " seconds");
		}

		if (params.size() > 9)
		{
			if (!params[9].isNumeric())
				throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

			wait_discount = params[9].asDouble();

			if (wait_discount < 0)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "wait discount must be >= 0");

			if (wait_discount > 1)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "wait discount must be <= 1");
		}

		if (xcx_type == CC_TYPE_XCX_MINING_TRADE && wait_discount != 1)
			throw RPC_Exception(RPC_INVALID_PARAMETER, "wait discount must be 1 for a trade request");

		wait_discount = pow(wait_discount, XREQ_WAIT_DISCOUNT_INTERVAL / 60.0);

		cc_crosschain_request_create(mode, params[0].asString(), xcx_type, min_amount, max_amount, rate, costs, quote_asset, foreign_asset, foreign_address, expiration, wait_discount, STDARGS);
	}
	else if (method == "cc.broadcast")
	{
		if (params.size() != 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " reference_id data");

		cc_broadcast(params[0].asString(), params[1].asString(), STDARGS);
	}
	else if (method == "cc.exchange_requests_pending_totals")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " cryptoasset");

		auto foreign_asset = params[0].asString();

		uint64_t quote_asset;

		crosschain_parse_foreign_asset(foreign_asset, quote_asset);

		cc_exchange_requests_pending_totals(0, quote_asset, foreign_asset, STDARGS);
	}
	else if (method == "cc.exchange_query_mining_info")
	{
		if (params.size() != 0)
			throw RPC_Exception(RPC_MISC_ERROR, method);

		cc_exchange_query_mining_info(STDARGS);
	}
	else if (method == "cc.exchange_request_info")
	{
		if (params.size() != 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " key id");

		auto key = params[0].asString();
		auto strval = params[1].asString();

		uint64_t intval = 0;

		if (key == "id" || key == "reqnum")
		{
			if (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue))
				throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

			intval = params[1].asUInt64();
		}

		cc_exchange_request_info(key, strval, intval, STDARGS);
	}
	else if (method == "cc.exchange_match_info")
	{
		if (params.size() != 1)
			throw RPC_Exception(RPC_MISC_ERROR, method + " matchnum");

		if (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		cc_exchange_match_info(params[0].asUInt64(), STDARGS);
	}
	else if (method == "cc.exchange_request_list_matches")
	{
		throw txrpc_not_implemented_error;

		//cc_exchange_request_list_matches(STDARGS);
	}
	else if (method == "cc.crosschain_match_action_list")
	{
		if ( /* params.size() < 0 || */ params.size() > 2)
			throw RPC_Exception(RPC_MISC_ERROR, method + " ( minutes_until_deadline override_reminder_times )");

		if (params.size() > 0 && !params[0].isNumeric())
			throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

		if (params.size() > 1 && !params[1].isBool())
			throw RPC_Exception(RPC_MISC_ERROR, not_bool_err);

		cc_crosschain_match_action_list(params.size() > 0 ? params[0].asDouble() : 0, params.size() > 1 ? params[1].asBool() : false, STDARGS);
	}
	else if (method == "cc.crosschain_match_mark_paid")
	{
		if (params.size() < 1 || params.size() > 4)
			throw RPC_Exception(RPC_MISC_ERROR, method + " match_number ( foreign_txid reminder_minutes minimum_advance_minutes )");

		if (!params[0].isIntegral() || !params[0].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		if (params.size() > 2 && !params[2].isNumeric())
			throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

		if (params.size() > 3 && !params[3].isNumeric())
			throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

		cc_crosschain_match_mark_paid(params[0].asUInt64(), params.size() > 1 ? params[1].asString() : "", params.size() > 2 ? params[2].asDouble() : 24*60, params.size() > 3 ? params[3].asDouble() : 60, STDARGS);
	}
	else if (method == "cc.crosschain_payment_claim" || method == "cc.crosschain_payment_claim_async")
	{
		if (params.size() < 2 || params.size() > 7)
			throw RPC_Exception(RPC_MISC_ERROR, method + " reference_id match_number ( foreign_block_id foreign_payment_identifier amount reminder_minutes minimum_advance_minutes )");

		if (!params[1].isIntegral() || !params[1].isConvertibleTo(Json::uintValue))
			throw RPC_Exception(RPC_MISC_ERROR, not_int_err);

		string foreign_block_id, foreign_txid;

		if (params.size() > 2)
		{
			foreign_block_id = params[2].asString();

			if (foreign_block_id.length() > XTX_MAX_ITEM_SIZE)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "foreign block identifier length must be <= " + to_string(XTX_MAX_ITEM_SIZE));
		}

		if (params.size() > 3)
		{
			foreign_txid = params[3].asString();

			if (foreign_txid.length() > XTX_MAX_ITEM_SIZE + 1)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "foreign payment identifier length must be <= " + to_string(XTX_MAX_ITEM_SIZE + 1));
		}

		double amount = 0;

		if (params.size() > 4)
		{
			if (!params[4].isNumeric())
				throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

			amount = params[4].asDouble();

			if (amount < 0)
				throw RPC_Exception(RPC_INVALID_PARAMETER, "amount must be positive");
		}

		if (params.size() > 5 && !params[5].isNumeric())
			throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

		if (params.size() > 6 && !params[6].isNumeric())
			throw RPC_Exception(RPC_MISC_ERROR, not_num_err);

		cc_crosschain_payment_claim(method.find("_async") != string::npos, params[0].asString(), params[1].asUInt64(), amount, foreign_block_id, foreign_txid, params.size() > 5 ? params[5].asDouble() : 10, params.size() > 6 ? params[6].asDouble() : 2, STDARGS);
	}
	else if (method == "cc.malloc_log")
	{
		bool blog = false;

		if (params.size() > 0 && params[0].isNumeric())
			blog = params[0].asDouble();

		if (blog)
			cc_malloc_logging(true);
		else
			g_disable_malloc_logging = true;
	}
	#if TEST_INCLUDE_QUICK_TEST
	else if (method == "test")
	{
		void quick_test(DbConn *dbconn, TxQuery& txquery);
		quick_test(dbconn, txquery);
	}
	#endif
	else
	{
		throw RPC_Exception(RPC_METHOD_NOT_FOUND, "Method not found");
	}
}

#if TEST_INCLUDE_QUICK_TEST
#include "transactions.hpp"
#include "exchange.hpp"
#include "walletdb.hpp"
#include <xmatch.hpp>
#include <BlockChainStatus.hpp>

void quick_test(DbConn *dbconn, TxQuery& txquery)
{
	Xmatchreq xreq;
	Transaction tx;
	BlockChainStatus blockchain_status;

	dbconn->ExchangeRequestSelectNextPoll(INT64_MAX, xreq, &tx);
	ExchangeRequest::PollXmatchreq(dbconn, txquery, xreq, tx, blockchain_status);
}
#endif

static bool do_one_rpc(const string& json, Json::Value& root, DbConn *dbconn, TxQuery& txquery, ostringstream& response)
{
	if (0 && TRACE_JSONRPC) BOOST_LOG_TRIVIAL(info) << "do_one_rpc: " << json;

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
		try_one_rpc(json, method, params, STDARGSAQ);

		copy_result_to_response(add_quotes, rstream.str(), version, id, null_error, notification, response);

		rc = false;
	}
	catch (const Json::Exception& e)
	{
		copy_error_to_response(RPC_INVALID_REQUEST, "Invalid request", version, id, null_result, notification, response);
	}
	catch (const RPC_Exception& e)
	{
		if (0 && TRACE_JSONRPC) BOOST_LOG_TRIVIAL(info) << "do_one_rpc " << json << " RPC_Exception code " << e.code << " " << e.what();
		//cerr << "caught RPC_Exception code " << e.code << " " << e.what() << endl;

		if (g_shutdown)
			copy_error_to_response(txrpc_shutdown_error.code, txrpc_shutdown_error.what(), version, id, null_result, notification, response);
		else
			copy_error_to_response(e.code, e.what(), version, id, null_result, notification, response);
	}

done:

	if (TRACE_JSONRPC) BOOST_LOG_TRIVIAL(info) << "do_one_rpc " << method << " rc " << (int)rc << " response " << (response.str().length() <= 120 ? response.str() : response.str().substr(0,120) + "...");

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
		auto rc = do_one_rpc(json, (root.isArray() ? root[i] : root), dbconn, txquery, response);

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
