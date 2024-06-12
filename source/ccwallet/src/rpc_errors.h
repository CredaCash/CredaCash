/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * rpc_errors.h
*/

/*
	Bitcoin RPC errors adapted from bitcoin core source file src\rpc\protocol.h
	Copyright (c) 2010 Satoshi Nakamoto
	Copyright (c) 2009-2016 The Bitcoin Core developers
	Distributed under the MIT software license, see
	http://www.opensource.org/licenses/mit-license.php
*/

#pragma once

enum RPCErrorCode
{
	//! Standard JSON-RPC 2.0 errors
	// Server error      = -32000 to -32099	// Reserved for implementation-defined server-errors.
	RPC_INVALID_REQUEST  = -32600,			// The JSON sent is not a valid Request object.
	RPC_METHOD_NOT_FOUND = -32601,			// The method does not exist / is not available.
	RPC_INVALID_PARAMS   = -32602,			// Invalid method parameter(s).
	RPC_INTERNAL_ERROR   = -32603,			// Internal JSON-RPC error.
	RPC_PARSE_ERROR      = -32700,			// Invalid JSON was received by the server. An error occurred on the server while parsing the JSON text.

	RPC_SERVER_ERROR			= -32000,
	RPC_SYNC_ERROR				= -32001,
	RPC_WALLET_INTERNAL_ERROR	= -32090,
	RPC_WALLET_SIMULATED_ERROR	= -32090,	// simulated test error

	//! General application defined errors
	RPC_MISC_ERROR                  = -1,  //!< std::exception thrown in command handling
	RPC_FORBIDDEN_BY_SAFE_MODE      = -2,  //!< Server is in safe mode, and command is not allowed in safe mode
	RPC_TYPE_ERROR                  = -3,  //!< Unexpected type was passed as parameter
	RPC_INVALID_ADDRESS_OR_KEY      = -5,  //!< Invalid address or key
	RPC_OUT_OF_MEMORY               = -7,  //!< Ran out of memory during operation
	RPC_INVALID_PARAMETER           = -8,  //!< Invalid, missing or duplicate parameter
	RPC_DATABASE_ERROR              = -20, //!< Database error
	RPC_DESERIALIZATION_ERROR       = -22, //!< Error parsing or validating structure in raw format
	RPC_VERIFY_ERROR                = -25, //!< General error during transaction or block submission
	RPC_VERIFY_REJECTED             = -26, //!< Transaction or block was rejected by network rules
	RPC_VERIFY_ALREADY_IN_CHAIN     = -27, //!< Transaction already in chain
	RPC_IN_WARMUP                   = -28, //!< Client still warming up

	//! P2P client errors
	RPC_CLIENT_NOT_CONNECTED        = -9,  //!< Bitcoin is not connected

	//! Wallet errors
	RPC_WALLET_ERROR                = -4,  //!< Unspecified problem with wallet (key not found etc.)
	RPC_WALLET_INSUFFICIENT_FUNDS   = -6,  //!< Not enough funds in wallet or account
	RPC_WALLET_INVALID_ACCOUNT_NAME = -11, //!< Invalid account name
	RPC_WALLET_KEYPOOL_RAN_OUT      = -12, //!< Keypool ran out, call keypoolrefill first
	RPC_WALLET_UNLOCK_NEEDED        = -13, //!< Enter the wallet passphrase with walletpassphrase first
	RPC_WALLET_PASSPHRASE_INCORRECT = -14, //!< The wallet passphrase entered was incorrect
	RPC_WALLET_WRONG_ENC_STATE      = -15, //!< Command given in wrong wallet encryption state (encrypting an encrypted wallet etc.)
	RPC_WALLET_ENCRYPTION_FAILED    = -16, //!< Failed to encrypt the wallet
	RPC_WALLET_ALREADY_UNLOCKED     = -17, //!< Wallet is already unlocked

	// CredaCash errors
	RPC_TRANSACTION_TIMEOUT			= -1001,
	RPC_TRANSACTION_MISMATCH		= -1002,
	RPC_TRANSACTION_FAILED			= -1003,
	RPC_TRANSACTION_EXPIRED         = -1004
};

/*

Some bitcoin core 0.14.2 errors, for reference:

RPC_CLIENT_NOT_CONNECTED, "Bitcoin is not connected!"
RPC_CLIENT_NOT_CONNECTED, "Shutting down"
RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled"
RPC_DATABASE_ERROR, "database error"
RPC_DATABASE_ERROR, state.GetRejectReason()
RPC_DESERIALIZATION_ERROR, "Block decode failed"
RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase"
RPC_DESERIALIZATION_ERROR, "Missing transaction"
RPC_DESERIALIZATION_ERROR, "Previous output scriptPubKey mismatch:\n" + ScriptToAsmStr(coins->vout[nOut].scriptPubKey) + "\nvs:\n"+ ScriptToAsmStr(scriptPubKey);
RPC_DESERIALIZATION_ERROR, "TX decode failed"
RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}"
RPC_DESERIALIZATION_ERROR, "vout must be positive"
RPC_FORBIDDEN_BY_SAFE_MODE, std::string("Safe mode: ") + strWarning
RPC_INTERNAL_ERROR, "Block not available (pruned data)"
RPC_INTERNAL_ERROR, "Blockchain is too short for pruning."
RPC_INTERNAL_ERROR, "Can't read block from disk"
RPC_INTERNAL_ERROR, "Could not find block with at least the specified timestamp."
RPC_INTERNAL_ERROR, "Could not properly delete the transaction."
RPC_INTERNAL_ERROR, "Couldn't create new block"
RPC_INTERNAL_ERROR, "No coinbase script available (mining requires a wallet)"
RPC_INTERNAL_ERROR, "No timer handler registered for RPC"
RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted"
RPC_INTERNAL_ERROR, "Transaction does not exist in wallet."
RPC_INTERNAL_ERROR, "Transaction index corrupt"
RPC_INTERNAL_ERROR, "Unable to read UTXO set"
RPC_INTERNAL_ERROR, strFailReason
RPC_INVALID_ADDRESS_OR_KEY, "(Not all) transactions not found in specified block"
RPC_INVALID_ADDRESS_OR_KEY, "Already have this key"
RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain"
RPC_INVALID_ADDRESS_OR_KEY, "Block not found"
RPC_INVALID_ADDRESS_OR_KEY, "Cannot use the p2sh flag with an address - use a script instead"
RPC_INVALID_ADDRESS_OR_KEY, "Consistency check failed"
RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address or script"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid P2SH address / script"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid address"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid redeem script"
RPC_INVALID_ADDRESS_OR_KEY, "Invalid scriptPubKey"
RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding"
RPC_INVALID_ADDRESS_OR_KEY, "No addresses in wallet correspond to included transaction"
RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range"
RPC_INVALID_ADDRESS_OR_KEY, "Pubkey is not a valid public key"
RPC_INVALID_ADDRESS_OR_KEY, "Pubkey must be a hex string"
RPC_INVALID_ADDRESS_OR_KEY, "Sign failed"
RPC_INVALID_ADDRESS_OR_KEY, "Something wrong with merkleblock"
RPC_INVALID_ADDRESS_OR_KEY, "Transaction contains inputs that cannot be signed"
RPC_INVALID_ADDRESS_OR_KEY, "Transaction contains inputs that don't belong to this wallet"
RPC_INVALID_ADDRESS_OR_KEY, "Transaction given doesn't exist in proof"
RPC_INVALID_ADDRESS_OR_KEY, "Transaction has been mined, or is conflicted with a mined transaction"
RPC_INVALID_ADDRESS_OR_KEY, "Transaction is not BIP 125 replaceable"
RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment"
RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool"
RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block"
RPC_INVALID_ADDRESS_OR_KEY, std::string(fTxIndex ? "No such mempool or blockchain transaction" : "No such mempool transaction. Use -txindex to enable blockchain transaction queries") + ". Use gettransaction for wallet transactions."
RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Bitcoin address: ")+input.get_str()
RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Bitcoin address: ")+name_
RPC_INVALID_PARAMETER, "Block height out of range"
RPC_INVALID_PARAMETER, "Blockchain is shorter than the attempted prune height."
RPC_INVALID_PARAMETER, "Cannot open wallet dump file"
RPC_INVALID_PARAMETER, "Incompatibility found between internal and label"
RPC_INVALID_PARAMETER, "Incompatibility found between watchonly and keys"
RPC_INVALID_PARAMETER, "Internal must be set for hex scriptPubKey"
RPC_INVALID_PARAMETER, "Invalid amount"
RPC_INVALID_PARAMETER, "Invalid confTarget (cannot be <= 0)"
RPC_INVALID_PARAMETER, "Invalid mode"
RPC_INVALID_PARAMETER, "Invalid parameter"
RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null"
RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid"
RPC_INVALID_PARAMETER, "Invalid parameter, expected object"
RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size."
RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range"
RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key"
RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range"
RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive"
RPC_INVALID_PARAMETER, "Invalid scriptPubKey"
RPC_INVALID_PARAMETER, "Invalid sighash param"
RPC_INVALID_PARAMETER, "More than private key given for one address"
RPC_INVALID_PARAMETER, "Negative block height."
RPC_INVALID_PARAMETER, "Negative count"
RPC_INVALID_PARAMETER, "Negative from"
RPC_INVALID_PARAMETER, "TX must have at least one output"
RPC_INVALID_PARAMETER, "Unknown named parameter " + argsIn.begin()->first
RPC_INVALID_PARAMETER, "changeAddress must be a valid bitcoin address"
RPC_INVALID_PARAMETER, "changePosition out of bounds"
RPC_INVALID_PARAMETER, "confTarget and totalFee options should not both be set. Please provide either a confirmation target for fee estimation or an explicit total fee for the transaction."
RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')"
RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+input.get_str()
RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+name_
RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated txid: ")+txid.get_str()
RPC_INVALID_PARAMETER, string("Invalid txid ")+txid.get_str()
RPC_INVALID_PARAMETER, strprintf("%s must be of length %d (not %d)", strName, 64, strHex.length())
RPC_INVALID_PARAMETER, strprintf("Insufficient totalFee (cannot be less than required fee %s)", FormatMoney(requiredFee))
RPC_INVALID_PARAMETER, strprintf("Insufficient totalFee, must be at least %s (oldFee %s + incrementalFee %s)", FormatMoney(minTotalFee), FormatMoney(nOldFeeRate.GetFee(maxNewTxSize)), FormatMoney(::incrementalRelayFee.GetFee(maxNewTxSize)))
RPC_INVALID_PARAMETER, strprintf("Invalid parameter, duplicated position: %d", pos)
RPC_INVALID_PARAMETER, strprintf("Invalid parameter, negative position: %d", pos)
RPC_INVALID_PARAMETER, strprintf("Invalid parameter, position too large: %d", pos)
RPC_INVALID_PARAMETER, strprintf("Support for '%s' rule requires explicit client support", vbinfo.name)
RPC_INVALID_REQUEST, "Invalid Request object"
RPC_INVALID_REQUEST, "Method must be a string"
RPC_INVALID_REQUEST, "Missing method"
RPC_INVALID_REQUEST, "Params must be an array or object"
RPC_INVALID_REQUEST, strprintf("Cannot bump transaction %s which was already bumped by transaction %s", hash.ToString(), wtx.mapValue.at("replaced_by_txid"))
RPC_IN_WARMUP, rpcWarmupStatus
RPC_METHOD_NOT_FOUND, "Cannot prune blocks because node is not in prune mode."
RPC_METHOD_NOT_FOUND, "Method not found (disabled)"
RPC_METHOD_NOT_FOUND, "Method not found"
RPC_MISC_ERROR, "Change output is too small to bump the fee"
RPC_MISC_ERROR, "Error: Unban failed"
RPC_MISC_ERROR, "Transaction does not have a change output"
RPC_MISC_ERROR, "Transaction has descendants in the mempool"
RPC_MISC_ERROR, "Transaction has descendants in the wallet"
RPC_MISC_ERROR, "Transaction has multiple change outputs"
RPC_MISC_ERROR, "setaccount can only be used with own address"
RPC_MISC_ERROR, e.what()
RPC_MISC_ERROR, strprintf("New fee rate (%s) is less than the minimum fee rate (%s) to get into the mempool. totalFee value should to be at least %s or settxfee value should be at least %s to add transaction.", FormatMoney(nNewFeeRate.GetFeePerK()), FormatMoney(minMempoolFeeRate.GetFeePerK()), FormatMoney(minMempoolFeeRate.GetFee(maxNewTxSize)), FormatMoney(minMempoolFeeRate.GetFeePerK()))
RPC_MISC_ERROR, strprintf("Specified or calculated fee %s is too high (cannot be higher than maxTxFee %s)", FormatMoney(nNewFee), FormatMoney(maxTxFee))
RPC_OUT_OF_MEMORY, "Out of memory"
RPC_PARSE_ERROR, "Parse error"
RPC_PARSE_ERROR, "Top-level object parse error"
RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain"
RPC_TRANSACTION_ERROR, "Missing inputs"
RPC_TRANSACTION_ERROR, state.GetRejectReason()
RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason())
RPC_TYPE_ERROR, "Address does not refer to a key"
RPC_TYPE_ERROR, "Address does not refer to key"
RPC_TYPE_ERROR, "Amount is not a number or string"
RPC_TYPE_ERROR, "Amount out of range"
RPC_TYPE_ERROR, "Invalid address"
RPC_TYPE_ERROR, "Invalid amount for send"
RPC_TYPE_ERROR, "Invalid amount"
RPC_TYPE_ERROR, "Invalid type provided. Verbose parameter must be a boolean."
RPC_TYPE_ERROR, "Missing data String key for proposal"
RPC_TYPE_ERROR, "Missing required timestamp field for key"
RPC_TYPE_ERROR, strprintf("Expected number or \"now\" timestamp value for key. got type %s", uvTypeName(timestamp.type()))
RPC_TYPE_ERROR, strprintf("Expected type %s for %s, got %s"
RPC_TYPE_ERROR, strprintf("Expected type %s, got %s", uvTypeName(typeExpected), uvTypeName(value.type()))
RPC_TYPE_ERROR, strprintf("Missing %s", t.first)
RPC_TYPE_ERROR, strprintf(Unexpected key %s", k)
RPC_VERIFY_ERROR, strRejectReason
RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet."
RPC_WALLET_ERROR, "Can't sign transaction."
RPC_WALLET_ERROR, "Error adding address to wallet"
RPC_WALLET_ERROR, "Error adding key to wallet"
RPC_WALLET_ERROR, "Error adding p2sh redeemScript to wallet"
RPC_WALLET_ERROR, "Error adding some keys to wallet"
RPC_WALLET_ERROR, "Error refreshing keypool."
RPC_WALLET_ERROR, "Error: Wallet backup failed!"
RPC_WALLET_ERROR, "Importing wallets is disabled in pruned mode"
RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known"
RPC_WALLET_ERROR, "Private key not available"
RPC_WALLET_ERROR, "Public key or redeemscript not known to wallet, or the key is uncompressed"
RPC_WALLET_ERROR, "Rescan is disabled in pruned mode"
RPC_WALLET_ERROR, "Segregated witness not enabled on network"
RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script"
RPC_WALLET_ERROR, strprintf("Error: The transaction was rejected! Reason given: %s", state.GetRejectReason())
RPC_WALLET_ERROR, strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired))
RPC_WALLET_ERROR, strprintf("Transaction commit failed:: %s", state.GetRejectReason())
RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds"
RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds"
RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason
RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name"
RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect."
RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first."
RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called."
RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called."
RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called."
RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called."

HTTP status codes used by bitcoin:

if (code == RPC_INVALID_REQUEST)
	nStatus = HTTP_BAD_REQUEST;
else if (code == RPC_METHOD_NOT_FOUND)
	nStatus = HTTP_NOT_FOUND;
else
	nStatus = HTTP_INTERNAL_SERVER_ERROR;

HTTP_BADMETHOD
HTTP_BAD_METHOD, "JSONRPC server handles only POST requests"
HTTP_BAD_REQUEST, "Combination of URI scheme inputs and raw post data is not allowed"
HTTP_BAD_REQUEST, "Error: empty request"
HTTP_BAD_REQUEST, "Header count out of range: " + path[0]
HTTP_BAD_REQUEST, "Invalid hash: " + hashStr
HTTP_BAD_REQUEST, "No header count specified. Use /rest/headers/<count>/<hash>.<ext>."
HTTP_BAD_REQUEST, "Parse error"
HTTP_BAD_REQUEST, strprintf("Error: max outpoints exceeded (max: %d, tried: %d)", MAX_GETUTXOS_OUTPOINTS, vOutPoints.size())
HTTP_FORBIDDEN
HTTP_INTERNAL, "Unhandled request"
HTTP_INTERNAL, "Work queue depth exceeded"
HTTP_NOTFOUND
HTTP_NOT_FOUND, "output format not found (available: " + AvailableDataFormatsString() + ")"
HTTP_NOT_FOUND, "output format not found (available: .bin, .hex)"
HTTP_NOT_FOUND, "output format not found (available: json)"
HTTP_NOT_FOUND, hashStr + " not available (pruned data)"
HTTP_NOT_FOUND, hashStr + " not found"
HTTP_OK
HTTP_SERVICE_UNAVAILABLE, "Service temporarily unavailable: " + statusmessage
HTTP_UNAUTHORIZED

*/
