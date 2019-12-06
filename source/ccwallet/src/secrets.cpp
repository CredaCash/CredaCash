/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * secrets.cpp
*/

#include "ccwallet.h"
#include "accounts.hpp"
#include "secrets.hpp"
#include "billets.hpp"
#include "transactions.hpp"
#include "txparams.hpp"
#include "txquery.hpp"
#include "walletdb.hpp"
#include "rpc_errors.hpp"

#include <CCcrypto.hpp>
#include <transaction.h>
#include <transaction.hpp>
#include <payspec.h>
#include <jsonutil.h>
#include <dblog.h>

#include <blake2/blake2.h>
#include <siphash/siphash.h>

//!#define TEST_RANDOM_POLLING		16

#ifndef TEST_RANDOM_POLLING
#define TEST_RANDOM_POLLING		0	// don't test
#endif

static const string cc_destination_prefix = "CredaCash_";

#define TRACE_SECRETS	(g_params.trace_secrets)
#define TRACE_POLLING	(g_params.trace_polling)

Secret::Secret()
{
	Clear();
}

void Secret::Clear()
{
	memset((void*)this, 0, sizeof(*this));
}

void Secret::Copy(const Secret& other)
{
	memcpy(this, &other, sizeof(*this));
}

string Secret::DebugString() const
{
	ostringstream out;

	out << "id " << id;
	out << " type " << type;
	out << " parent_id " << parent_id;
	out << " dest_id " << dest_id;
	out << " number " << number;
	if (packed_params_bytes)
		out << " packed_params " << buf2hex(packed_params, packed_params_bytes);
	out << " dest_chain " << dest_chain;
	out << " value " << hex << value << dec;
	out << " account_id " << account_id;
	if (label[0])
		out << " label " << label;
	out << " create_time " << create_time;
	out << " first_receive " << first_receive;
	out << " last_receive " << last_receive;
	out << " last_check " << last_check;
	out << " next_check " << next_check;
	out << " query_commitnum " << query_commitnum;
	out << " expected_commitnum " << expected_commitnum;

	return out.str();
}

bool Secret::IsValid() const
{
	if (!TypeIsAddress() && (first_receive || last_receive || last_check || next_check || query_commitnum || expected_commitnum))
		return false;

	if (type > SECRET_TYPE_PRE_ADDRESS)
		return dest_chain && dest_id;
	if (type >= SECRET_TYPE_PRE_ADDRESS)
		return dest_chain;
	else
		return TypeIsValid(type);
}

bool Secret::SetNumber(uint64_t _number)
{
	if (_number > MaxNumber())
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::SetNumber overflow type " << type << " number " << _number;

		return -1;
	}

	number = _number;

	return 0;
}

bool Secret::IncrementNumber()
{
	if (number >= MaxNumber())
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::IncrementNumber overflow type " << type << " number " << number;

		return -1;
	}

	++number;

	return 0;
}

bool Secret::AcceptanceRequired() const
{
	CCASSERT(TypeIsDestination());

	return ((BIG64(value) & TX_ACCEPT_REQ_DEST_MASK) == 0);
}

bool Secret::HasStaticAddress() const
{
	CCASSERT(TypeIsDestination());

	return ((BIG64(value) & TX_STATIC_ADDRESS_MASK) == 0);
}

int Secret::SetFromTxSecrets(unsigned _type, uint64_t _parent_id, const SpendSecretParams& params, SpendSecret *txsecrets, unsigned size)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::SetFromTxSecrets type " << _type << " parent " << _parent_id;

	// Secret = SpendSecretParams + SpendSecrets

	Clear();

	bool have_all_secrets = (size == sizeof(SpendSecrets));

	if (_type < SECRET_TYPE_PRE_ADDRESS)
	{
		CCASSERT(size >= sizeof(*txsecrets));

		for (unsigned i = 0; i < size/sizeof(*txsecrets); ++i)
		{
			auto errmsg = compute_or_verify_secrets(params, *(txsecrets + i), false);
			if (errmsg)
			{
				BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets compute_or_verify_secrets " << i << " failed error: " << errmsg;

				return -1;
			}
		}
	}

	bool do_hash = false;
	uint32_t bufpos = 0;

	switch (_type)
	{
	case SECRET_TYPE_MASTER:
		if (!txsecrets[0].____have_master_secret)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets secret type " << _type << " is not valid";

			return -1;
		}

		// @master_secret may = hash_passphrase(passphrase, salt, memory, iterations)
		// if so, packed_params: salt, memory, iterations -- TODO

		value = txsecrets[0].____master_secret;

		break;

	case SECRET_TYPE_ROOT:
		if (!txsecrets[0].____have_root_secret)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets secret type " << _type << " is not valid";

			return -1;
		}

		// note RULE tx input: @root_secret = zkhash(@master_secret)

		value = txsecrets[0].____root_secret;

		break;

	case SECRET_TYPE_SPEND:
		if (!txsecrets[0].____have_spend_secret)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets secret type " << _type << " is not valid";

			return -1;
		}

		// note RULE tx input: @spend_secret[0] = zkhash(@root_secret, @spend_secret_number)

		value = txsecrets[0].____spend_secret;
		number = txsecrets[0].____spend_secret_number;

		break;

	case SECRET_TYPE_TRUST:
		if (!txsecrets[0].____have_trust_secret)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets secret type " << _type << " is not valid";

			return -1;
		}

		// note RULE tx input: @trust_secret[i] = zkhash(@spend_secret[i])

		value = txsecrets[0].____trust_secret;

		break;

	case SECRET_TYPE_MONITOR:
		if (!txsecrets[0].____have_monitor_secret)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets secret type " << _type << " is not valid";

			return -1;
		}

		// note RULE tx input: @monitor_secret[i] = zkhash(@trust_secret[i])

		value = txsecrets[0].____monitor_secret;

		break;

	case SECRET_TYPE_RECEIVE:
	{
		if (!txsecrets[0].____have_receive_secret)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets secret type " << _type << " is not valid";

			return -1;
		}

		// note RULE tx input: @receive_secret = zkhash(@monitor_secret[0], @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime)
		// packed_params: @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime

		value = txsecrets[0].____receive_secret;

		auto flags =	(params.____enforce_spendspec_with_spend_secrets	<< 0);
		flags |=		(params.____enforce_spendspec_with_trust_secrets	<< 1);
		flags |=		(params.____allow_master_secret						<< 2);
		flags |=		(params.____allow_freeze							<< 3);
		flags |=		(params.____allow_trust_unfreeze					<< 4);
		flags |=		(params.____require_public_hashkey					<< 5);
		flags |=		(params.____restrict_addresses						<< 6);

		copy_to_buf(flags, 1, bufpos, packed_params, sizeof(packed_params));
		copy_to_buf(params.____spend_locktime, TX_TIME_BYTES, bufpos, packed_params, sizeof(packed_params));
		copy_to_buf(params.____trust_locktime, TX_TIME_BYTES, bufpos, packed_params, sizeof(packed_params));
		copy_to_buf(params.____spend_delaytime, TX_DELAYTIME_BYTES, bufpos, packed_params, sizeof(packed_params));
		copy_to_buf(params.____trust_delaytime, TX_DELAYTIME_BYTES, bufpos, packed_params, sizeof(packed_params));

		if (params.____enforce_spendspec_with_spend_secrets || params.____enforce_spendspec_with_trust_secrets)
			copy_to_buf(params.____required_spendspec_hash, sizeof(params.____required_spendspec_hash), bufpos, packed_params, sizeof(packed_params));

		break;
	}

	case SECRET_TYPE_PRE_DESTINATION:
	{
		// note pre-destination = blake2s(receive_secret_id, monitor_secret_id[1..Q], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets)
		// packed_params: @receive_secret, @monitor_secret[1..Q], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets // FUTURE: add restricted addresses

		CCASSERT(have_all_secrets);

		copy_to_buf(params.____required_spend_secrets, TX_MAX_SECRETS_BYTES, bufpos, packed_params, sizeof(packed_params));
		copy_to_buf(params.____required_trust_secrets, TX_MAX_SECRETS_BYTES, bufpos, packed_params, sizeof(packed_params));

		uint64_t flags = 0;
		for (unsigned i = 0; i < TX_MAX_SECRETS; ++i)
		{
			flags <<= 1;
			flags |= params.____use_spend_secret[i];
			flags <<= 1;
			flags |= params.____use_trust_secret[i];
		}

		unsigned nbytes = (2*TX_MAX_SECRETS + 7)/8;
		copy_to_buf(flags, nbytes, bufpos, packed_params, sizeof(packed_params));

		for (unsigned i = 1; i < TX_MAX_SECRETS; ++i)
		{
			if (params.____use_spend_secret[i] || params.____use_trust_secret[i])	// FUTURE: pack restricted addresses
				copy_to_buf(txsecrets[i].____monitor_secret, sizeof(txsecrets[i].____monitor_secret), bufpos, packed_params, sizeof(packed_params));
		}

		do_hash = true;

		break;
	}

	case SECRET_TYPE_SPENDABLE_DESTINATION:
	case SECRET_TYPE_TRACK_DESTINATION:
	case SECRET_TYPE_WATCH_DESTINATION:
	case SECRET_TYPE_SEND_DESTINATION:
		// note RULE tx input: #dest = zkhash(@receive_secret, @monitor_secret[1..R], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets, @destnum)

		CCASSERT(have_all_secrets);

		compute_destination(params, *(SpendSecrets*)txsecrets, value);
		number = params.____destnum;

		break;

	case SECRET_TYPE_PRE_ADDRESS:

		copy_to_buf(params.addrparams.dest_chain, sizeof(params.addrparams.dest_chain), bufpos, packed_params, sizeof(packed_params));

		do_hash = true;

		break;

	case SECRET_TYPE_SEND_ADDRESS:
	case SECRET_TYPE_SELF_ADDRESS:
	case SECRET_TYPE_RECV_ADDRESS:
	case SECRET_TYPE_POLL_ADDRESS:
	case SECRET_TYPE_STATIC_ADDRESS:
		if (!params.addrparams.__dest_id)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets secret type " << _type << " is not valid";

			return -1;
		}

		dest_id = params.addrparams.__dest_id;

		// note RULE tx output: M_address = zkhash(#dest, dest_chain, #paynum)

		compute_address(params.addrparams.__dest, params.addrparams.dest_chain, params.addrparams.__paynum, value);
		number = params.addrparams.__paynum;

		break;

	default:
		BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets unsupported type " << _type;

		return -1;
	}

	if (!id && _type > SECRET_TYPE_PRE_DESTINATION)	// don't change dest_chain after secret is created
		dest_chain = params.addrparams.dest_chain;

	if (bufpos > sizeof(packed_params))
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets packed_params overflow " << bufpos << " > " << sizeof(packed_params);

		return -1;
	}

	if (do_hash)
	{
		if (!_parent_id)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::SetFromTxSecrets secret type " << _type << " must have a parent secret";

			return -1;
		}

		blake2s_ctx ctx;
		auto rc =
		blake2s_init(&ctx, sizeof(value), NULL, 0);
		blake2s_update(&ctx, &_parent_id, sizeof(_parent_id));
		blake2s_update(&ctx, packed_params, bufpos);
		blake2s_final(&ctx, &value);
		CCASSERTZ(rc);
	}

	type = _type;
	parent_id = _parent_id;

	if (_type != SECRET_TYPE_PRE_ADDRESS)
		packed_params_bytes = bufpos;

	if (packed_params_bytes)
	{
		if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::SetFromTxSecrets type " << type << " packed_params " << buf2hex(packed_params, packed_params_bytes);
	}

	return 0;
}

int Secret::ExtractToTxSecrets(SpendSecretParams& params, SpendSecret *txsecrets, unsigned size) const
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::ExtractToTxSecrets id " << id << " type " << type;

	// Secret --> SpendSecretParams + SpendSecrets

	bool have_all_secrets = (size == sizeof(SpendSecrets));

	if (type < SECRET_TYPE_SPENDABLE_DESTINATION)
	{
		CCASSERT(size >= sizeof(*txsecrets));
	}

	memset((void*)txsecrets, 0, size);

	uint32_t bufpos = 0;

	switch (type)
	{
	case SECRET_TYPE_MASTER:
		txsecrets[0].____have_master_secret = true;
		txsecrets[0].____master_secret = value;

		break;

	case SECRET_TYPE_ROOT:
		txsecrets[0].____have_root_secret = true;
		txsecrets[0].____root_secret = value;

		break;

	case SECRET_TYPE_SPEND:
		txsecrets[0].____have_spend_secret = true;
		txsecrets[0].____spend_secret = value;
		txsecrets[0].____spend_secret_number = number;

		break;

	case SECRET_TYPE_TRUST:
		txsecrets[0].____have_trust_secret = true;
		txsecrets[0].____trust_secret = value;

		break;

	case SECRET_TYPE_MONITOR:
		txsecrets[0].____have_monitor_secret = true;
		txsecrets[0].____monitor_secret = value;

		break;

	case SECRET_TYPE_RECEIVE:
	{
		txsecrets[0].____have_receive_secret = true;
		txsecrets[0].____receive_secret = value;

		// packed_params: @enforce_spendspec_with_spend_secret, @enforce_spendspec_with_trust_secret, @required_spendspec_hash, @allow_master_secret, @allow_freeze, @allow_trust_unfreeze, @require_public_hashkey, @restrict_addresses, @spend_locktime, @trust_locktime, @spend_delaytime, @trust_delaytime

		uint16_t flags;
		copy_from_buf(flags, 1, bufpos, packed_params, sizeof(packed_params));
		copy_from_buf(params.____spend_locktime, TX_TIME_BYTES, bufpos, packed_params, sizeof(packed_params));
		copy_from_buf(params.____trust_locktime, TX_TIME_BYTES, bufpos, packed_params, sizeof(packed_params));
		copy_from_buf(params.____spend_delaytime, TX_DELAYTIME_BYTES, bufpos, packed_params, sizeof(packed_params));
		copy_from_buf(params.____trust_delaytime, TX_DELAYTIME_BYTES, bufpos, packed_params, sizeof(packed_params));

		params.____enforce_spendspec_with_spend_secrets =	(flags >> 0) & 1;
		params.____enforce_spendspec_with_trust_secrets =	(flags >> 1) & 1;
		params.____allow_master_secret =					(flags >> 2) & 1;
		params.____allow_freeze =							(flags >> 3) & 1;
		params.____allow_trust_unfreeze =					(flags >> 4) & 1;
		params.____require_public_hashkey =					(flags >> 5) & 1;
		params.____restrict_addresses =						(flags >> 6) & 1;

		if (params.____enforce_spendspec_with_spend_secrets || params.____enforce_spendspec_with_trust_secrets)
			copy_from_buf(params.____required_spendspec_hash, sizeof(params.____required_spendspec_hash), bufpos, packed_params, sizeof(packed_params));

		break;
	}

	case SECRET_TYPE_PRE_DESTINATION:
	{
		CCASSERT(have_all_secrets);

		// packed_params: @receive_secret, @monitor_secret[1..Q], @use_spend_secret[0..Q], @use_trust_secret[0..Q], @required_spend_secrets, @required_trust_secrets

		copy_from_buf(params.____required_spend_secrets, TX_MAX_SECRETS_BYTES, bufpos, packed_params, sizeof(packed_params));
		copy_from_buf(params.____required_trust_secrets, TX_MAX_SECRETS_BYTES, bufpos, packed_params, sizeof(packed_params));

		uint64_t flags;
		unsigned nbytes = (2*TX_MAX_SECRETS + 7)/8;
		copy_from_buf(flags, nbytes, bufpos, packed_params, sizeof(packed_params));

		for (int i = TX_MAX_SECRETS - 1; i >= 0; --i)
		{
			params.____use_trust_secret[i] = (flags & 1);
			flags >>= 1;
			params.____use_spend_secret[i] = (flags & 1);
			flags >>= 1;
		}

		for (unsigned i = 1; i < TX_MAX_SECRETS; ++i)
		{
			if (params.____use_spend_secret[i] || params.____use_trust_secret[i])	// FUTURE: pack restricted addresses
				copy_from_buf(txsecrets[i].____monitor_secret, sizeof(txsecrets[i].____monitor_secret), bufpos, packed_params, sizeof(packed_params));
		}

		// if receive_secret is needed, it must be retrieved from db and extracted separately

		break;
	}

	case SECRET_TYPE_SPENDABLE_DESTINATION:
	case SECRET_TYPE_TRACK_DESTINATION:
	case SECRET_TYPE_WATCH_DESTINATION:
	case SECRET_TYPE_SEND_DESTINATION:
		params.addrparams.__dest_id = id;
		params.addrparams.__dest = value;
		params.____destnum = number;

		if (AcceptanceRequired())
			params.__acceptance_required = true;

		if (HasStaticAddress())
			params.__static_address = true;

		break;

	case SECRET_TYPE_PRE_ADDRESS:

		break;

	case SECRET_TYPE_SEND_ADDRESS:
	case SECRET_TYPE_SELF_ADDRESS:
	case SECRET_TYPE_RECV_ADDRESS:
	case SECRET_TYPE_POLL_ADDRESS:
	case SECRET_TYPE_STATIC_ADDRESS:
		params.addrparams.__dest_id = dest_id;
		params.addrparams.__paynum = number;

		// if dest is needed, it must be retrieved from db and extracted separately

		break;

	default:
		BOOST_LOG_TRIVIAL(error) << "Secret::ExtractToTxSecrets unsupported type " << type;

		return -1;
	}

	if (bufpos != packed_params_bytes && type != SECRET_TYPE_MASTER)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::ExtractToTxSecrets packed_params size mismatch " << bufpos << " != " << packed_params_bytes;

		return -1;
	}

	if (type > SECRET_TYPE_PRE_DESTINATION && params.addrparams.dest_chain == 0)	// allow dest_chain to be overridden by caller
		params.addrparams.dest_chain = dest_chain;

	//tx_dump_spend_secret_params_stream(cerr, params);
	//for (unsigned i = 0; i < size/sizeof(*txsecrets); ++i)
	//	tx_dump_spend_secret_stream(cerr, txsecrets[i], 0);

	return 0;
}

string Secret::EncodeDestination() const
{
	CCASSERT(TypeIsDestination());

	return EncodeDestination(value, dest_chain);
}

string Secret::EncodeDestination(const bigint_t& destination, uint64_t dest_chain)
{
	//cerr << hex << "destination " << destination << dec << endl;

	string outs = cc_destination_prefix;

	cc_encode(base26, 26, 0UL, false, -1, dest_chain, outs);

	//cerr << outs << endl;

	cc_encode(base26, 26, 0UL, false, 0, destination, outs);

	// add checksum

	uint64_t hash = siphash((const uint8_t *)outs.data(), outs.length());
	//cerr << "hash bytes " << buf2hex(outs.data(), outs.length()) << endl;
	//cerr << "hash " << hex << hash << dec << endl;
	cc_encode(base26, 26, 0UL, false, 9, hash, outs);

	bigint_t dest_check;
	uint64_t chain_check = -1;
	auto rc = DecodeDestination(outs, chain_check, dest_check);
	CCASSERT(!rc && dest_chain == chain_check && destination == dest_check);

	return outs;
}

int Secret::DecodeDestination(const string& encoded, uint64_t& dest_chain, bigint_t& destination)
{
	dest_chain = -1;
	subBigInt(bigint_t(0UL), bigint_t(1UL), destination, false);

	string fn;
	char output[128];
	uint32_t outsize = sizeof(output);

	auto inlen = encoded.length();
	int chainlen = inlen - (cc_destination_prefix.length() + 55 + 9);
	if (chainlen < 1)
	{
		BOOST_LOG_TRIVIAL(info) << "Secret::DecodeDestination invalid length";

		return -1;
	}

	if (!boost::iequals(encoded.substr(0, cc_destination_prefix.length()), cc_destination_prefix))
	{
		BOOST_LOG_TRIVIAL(info) << "Secret::DecodeDestination prefix != \"" << cc_destination_prefix << "\"";

		return -1;
	}

	string instring = encoded.substr(cc_destination_prefix.length());

	//cerr << "encoded destination " << instring << endl;

	bigint_t bigval;
	auto rc = cc_decode(fn, base26int, 26, false, chainlen, instring, bigval, output, outsize);
	if (rc || bigval > bigint_t((uint64_t)(-1)))
	{
		BOOST_LOG_TRIVIAL(info) << "Secret::DecodeDestination blockchain decode failed: " << output;

		return -1;
	}

	dest_chain = BIG64(bigval);

	rc = cc_decode(fn, base26int, 26, false, 55, instring, destination, output, outsize);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(info) << "Secret::DecodeDestination destination decode failed: " << output;

		return -1;
	}

	//cerr << "decoded dest_chain " << dest_chain << endl;
	//cerr << "decoded destination " << destination << endl;
	//cerr << "encoded checksum " << instring << endl;
	//cerr << "hash bytes " << buf2hex(encoded.data(), inlen - instring.length()) << endl;

	string outs;
	uint64_t hash = siphash((const uint8_t *)encoded.data(), inlen - instring.length());
	cc_encode(base26, 26, 0UL, false, 9, hash, outs);
	//cerr << "hash " << hex << hash << dec << " " << outs << " " << instring << endl;
	if (outs != instring)
	{
		BOOST_LOG_TRIVIAL(info) << "Secret::DecodeDestination checksum mismatch";

		return -1;
	}

	//cerr << hex << "destination " << destination << dec << endl;

	return 0;
}

string Secret::GetNewPassphrase(const char *use)
{
	string passphrase;

	while (true)
	{
		passphrase.clear();

		cerr << "Enter a passphrase for " << use << ":" << endl;

		if (!cin.good() || g_shutdown) goto abort;

		getline(cin, passphrase);

		if (!cin.good() || g_shutdown) goto abort;

		string fn;
		char output[1];
		uint32_t outsize = sizeof(output);

		auto rc = check_ascii_only(fn, passphrase, output, outsize);
		if (rc)
		{
			cerr << "The passphrase should be ASCII-only.  Please try again.\n" << endl;

			continue;
		}

		cerr << "Re-enter the passphrase:" << endl;

		if (!cin.good() || g_shutdown) goto abort;

		string p2;
		getline(cin, p2);

		if (!cin.good() || g_shutdown) goto abort;

		check_ascii_only(fn, p2, output, outsize);

		if (passphrase != p2)
		{
			cerr << "The passphrases do not match.  Please try again.\n" << endl;

			continue;
		}

		break;
	}

	return passphrase;

abort:

	g_shutdown = true;

	return string();
}

string Secret::GetPassphrase(const char *use)
{
	string passphrase;

	cerr << "Enter the passphrase for " << use << ":" << endl;

	getline(cin, passphrase);

	if (!cin.good() || g_shutdown)
		g_shutdown = true;

	return passphrase;
}

void Secret::GenerateMasterSecret(string& encrypted_master_secret, string& passphrase)
{
	cerr << "The wallet will generate a master secret." << endl;

	if (!passphrase.length())
		passphrase = GetNewPassphrase("the new master secret");

	if (g_shutdown) return;

	string fn;
	char output[256];
	uint32_t outsize = sizeof(output);

	bigint_t salt;
	salt.randomize();

	cerr << "\nGenerating master secret" << (passphrase.length() ? "" : " with no passphrase") << "..." << endl;

	auto rc = generate_master_secret(fn, g_params.secret_gen_memory, g_params.secret_gen_time, salt, output, outsize);
	if (rc) throw runtime_error(string("Error generating master secret: ") + output);

	Json::Reader reader;
	Json::Value root, value;

	reader.parse(output, root);

	string key = "encrypted-master-secret";
	if (!root.removeMember(key, &value))
		throw runtime_error(string("Error unexpected API result: ") + output);

	encrypted_master_secret = value.asString();
}

int Secret::CreateBaseSecrets(DbConn *dbconn)
{
	// for now, the only case this needs to handle is creating the initial master secret and it's descendants for the default spend params
	// TODO: support user option to derive master secret from a passphrase and store the params (seed, etc) in PackedParams field

	static mutex createlock;
	lock_guard<mutex> lock(createlock);

	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(debug) << "Secret::CreateBaseSecrets";

	Secret secret;
	SpendSecretParams params;
	memset((void*)&params, 0, sizeof(params));

	params.____allow_master_secret = 1;
	params.____use_spend_secret[0] = 1;
	params.____use_trust_secret[0] = 1;
	params.____required_spend_secrets = 1;
	params.____required_trust_secrets = 1;

	auto rc = dbconn->SecretSelectId(MAIN_SECRET_ID, secret, true);	// or_greater = true so function logs a trace msg instead of a warning
	if (rc < 0)
		throw runtime_error("Error reading database");

	if (!rc)
	{
		if (secret.id != MAIN_SECRET_ID)
			throw runtime_error("Secret id mismatch reading master secret");

		const char *msg =  "The wallet already contains a master secret";

		if (g_params.initial_master_secret.length())
		{
			cerr << "ERROR: " << msg << endl;

			return 1;
		}
		else
		{
			cerr << "Note: " << msg << endl;

			return 0;
		}
	}

	// MAIN_SECRET_ID was not found, so create it

	secret.Clear();

	string encrypted_master_secret = g_params.initial_master_secret;
	string passphrase = g_params.initial_master_secret_passphrase;

	g_params.initial_master_secret_passphrase.replace(0, 99999, passphrase.length(), 0).clear();

	if (!encrypted_master_secret.length())
		GenerateMasterSecret(encrypted_master_secret, passphrase);
	else if (!passphrase.length())
		passphrase = GetPassphrase("the imported master secret");

	if (g_shutdown) return -1;

	if (encrypted_master_secret.length() >= sizeof(secret.packed_params))
		throw runtime_error(string("Error encrypted master secret too long"));

	string fn;
	char output[256];
	uint32_t outsize = sizeof(output);

	rc = compute_master_secret(fn, encrypted_master_secret, passphrase, secret.value, output, outsize);
	if (rc) throw runtime_error(string("Error generating master secret: ") + output);

	secret.type = SECRET_TYPE_MASTER;
	secret.packed_params_bytes = encrypted_master_secret.length();
	memcpy(secret.packed_params, encrypted_master_secret.data(), secret.packed_params_bytes);
	//cerr << "master secret " << hex << secret.value << dec << endl;
	//cerr << "encrypted master-secret: " << (char*)secret.packed_params << endl;
	CCASSERT(secret.value);

	rc = dbconn->SecretInsert(secret, 1);
	if (rc || secret.id != MAIN_SECRET_ID)
		throw runtime_error("Error storing master secret");

	// create MAIN_PRE_DESTINATION_ID

	rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_PRE_DESTINATION, secret.id, 0, params);
	if (rc || secret.id != MAIN_PRE_DESTINATION_ID)
		throw runtime_error("Error generating pre-destination");

	// create MINT_DESTINATION_ID

	rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_SPENDABLE_DESTINATION, MAIN_PRE_DESTINATION_ID, 0, params);
	if (rc || secret.id != MINT_DESTINATION_ID)
		throw runtime_error("Error generating mint destination");

	// create SELF_DESTINATION_ID

	rc = secret.CreateNewSecret(dbconn, SECRET_TYPE_SPENDABLE_DESTINATION, MAIN_PRE_DESTINATION_ID, 0, params);
	if (rc || secret.id != SELF_DESTINATION_ID)
		throw runtime_error("Error generating self destination");

	//throw runtime_error("test");

	cerr << "The master secret has been stored in the wallet file.  This file should be kept safe and private." << endl;
	cerr << "The encrypted master secret is: " << encrypted_master_secret << endl;
	cerr <<

R"(If the wallet file is lost, this encrypted master secret and the passphrase can regenerate the master secret and
might be able recover some or all of the wallet contents. These values could also be used to steal the contents of the
wallet, so if they are retained, they should be stored separately (not together), and both kept private and secure.)"

	"\n" << endl;

	return 0;
}

/*

Create Destination or Address scenarios:

- To request a payment, create a new destination and send it to Payor.
	Also need to create and monitor first address and a few forward addresses for incoming payments.
- To send a payment, need to import destination and create one or more new addresses from the destination
	and use them in Tx's.  Also need to monitor addresses to see when the payment is made.
- To get change, generate an address from the change destination and use in a Tx.
	Also need to monitor address to see when the payment is made.

At the moment, wallet has only one SECRET_TYPE_PRE_DESTINATION, although eventually it will have more when:
(a) new unique spend secrets are needed for multi-secrets
(b) receive secrets and pre-destinations are generated with different params
(c) monitor secrets are imported for watch purposes

TODO eventually: check if new address has already been used and if so, retry with new destination?

TODO: handle multi-secrets

*/

int Secret::CreateNewSecret(DbConn *dbconn, unsigned _type, uint64_t _parent_id, uint64_t _dest_chain, SpendSecretParams& params, bool recurse)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(debug) << "Secret::CreateNewSecret type " << _type << " parent id " << _parent_id << " dest_chain " << _dest_chain << (recurse ? " recurse" : "");

	// TODO: retry if derived secret already in db (because it was previously imported)

	Clear();

	params.addrparams.dest_chain = _dest_chain;

	// retrieve parent

	Secret parent;

	auto rc = dbconn->SecretSelectId(_parent_id, parent);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::CreateNewSecret error retrieving parent id " << _parent_id;

		return -1;
	}

	if (recurse && parent.TypeHierarchy() == TypeHierarchy(_type))
	{
		Copy(parent);

		return 0;
	}

	CCASSERT(parent.TypeHierarchy() < TypeHierarchy(_type));

	// destination must have a specific type, so it can't be skipped over
	CCASSERTZ(parent.type <= SECRET_TYPE_PRE_DESTINATION && _type >= SECRET_TYPE_PRE_ADDRESS);

	// When descending the secret hierarchy, the following secrets are always generated and saved:
	//		SECRET_TYPE_ROOT - to save next spend_secret_number
	//			increment spend_secret_number at this crossing, if no lower number will be incremented later
	//		SECRET_TYPE_SPEND - to save spend_secret_number
	//		...
	//		SECRET_TYPE_RECEIVE - to save packed params
	//		SECRET_TYPE_PRE_DESTINATION - to save next destnum + packed_params
	//			increment destnum at this crossing, if no lower number will be incremented later
	//		TypeIsDestination - to save destnum
	//		SECRET_TYPE_PRE_ADDRESS - to save next paynum + packed_params
	//			increment paynum at this crossing
	//		TypeIsAddress - to save paynum

	if (parent.type < SECRET_TYPE_ROOT)
	{
		return CreateIntermediateSecret(dbconn, SECRET_TYPE_ROOT, _type, parent, params);
	}

	else if (parent.type == SECRET_TYPE_ROOT)
	{
		if (_type > SECRET_TYPE_PRE_DESTINATION)
			return CreateIntermediateSecret(dbconn, SECRET_TYPE_SPEND, _type, parent, params);
		else
			return CreateNextSecretNumber(dbconn, SECRET_TYPE_SPEND, _type, parent.id, params);	// increment spend_secret_number
	}

	else if (parent.type < SECRET_TYPE_RECEIVE)
	{
		return CreateIntermediateSecret(dbconn, SECRET_TYPE_RECEIVE, _type, parent, params);
	}

	else if (parent.type == SECRET_TYPE_RECEIVE)
	{
		return CreateIntermediateSecret(dbconn, SECRET_TYPE_PRE_DESTINATION, _type, parent, params);
	}

	else if (parent.type == SECRET_TYPE_PRE_DESTINATION)
	{
		CCASSERT(TypeIsDestination(_type));			// can't skip over the destination

		return CreateNextSecretNumber(dbconn, _type, _type, parent.id, params);					// increment destnum
	}

	else if (parent.TypeIsDestination())
	{
		return CreateIntermediateSecret(dbconn, SECRET_TYPE_PRE_ADDRESS, _type, parent, params);
	}

	else if (parent.type == SECRET_TYPE_PRE_ADDRESS)
	{
		CCASSERT(TypeIsAddress(_type));

		return CreateNextSecretNumber(dbconn, _type, _type, parent.id, params);					// increment paynum
	}

	CCASSERT(0);	// shouldn't get here

	return -1;
}

int Secret::CreateIntermediateSecret(DbConn *dbconn, unsigned _type, unsigned target_type, Secret& parent, SpendSecretParams& params)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(debug) << "Secret::CreateIntermediateSecret intermediate type " << _type << " target type " << target_type << " parent id " << parent.id << " parent type " << parent.type;

	Secret midsecret;

	auto rc = midsecret.DeriveSecret(dbconn, _type, parent, params);
	if (rc)
		return -1;

	return CreateNewSecret(dbconn, target_type, midsecret.id, params.addrparams.dest_chain, params, true);
}

int Secret::DeriveSecret(DbConn *dbconn, unsigned _type, uint64_t _parent_id, SpendSecretParams& params)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(debug) << "Secret::DeriveSecret type " << _type << " parent id " << _parent_id;

	Clear();

	// retrieve parent

	Secret parent;

	auto rc = dbconn->SecretSelectId(_parent_id, parent);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::DeriveSecret error retrieving parent id " << _parent_id;

		return -1;
	}

	return DeriveSecret(dbconn, _type, parent, params);
}

int Secret::DeriveSecret(DbConn *dbconn, unsigned _type, Secret& parent, SpendSecretParams& params)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(debug) << "Secret::DeriveSecret type " << _type << " parent id " << parent.id << " type " << parent.type;

	Clear();

	//		SECRET_TYPE_ROOT - to save next spend_secret_number
	//			increment spend_secret_number at this crossing, if no lower number will be incremented later
	//		SECRET_TYPE_SPEND - to save spend_secret_number
	//		...
	//		SECRET_TYPE_RECEIVE - to save packed params
	//		SECRET_TYPE_PRE_DESTINATION - to save next destnum + packed_params
	//			increment destnum at this crossing, if no lower number will be incremented later
	//		TypeIsDestination - to save destnum
	//		SECRET_TYPE_PRE_ADDRESS - to save next paynum + packed_params
	//			increment paynum at this crossing
	//		TypeIsAddress - to save paynum

	CCASSERT(parent.TypeHierarchy() < TypeHierarchy(_type));

	CCASSERTZ(parent.type <= SECRET_TYPE_ROOT && _type > SECRET_TYPE_ROOT);
	CCASSERTZ(parent.type < SECRET_TYPE_RECEIVE && _type > SECRET_TYPE_RECEIVE);
	CCASSERTZ(parent.type <= SECRET_TYPE_PRE_DESTINATION && _type > SECRET_TYPE_PRE_DESTINATION);
	CCASSERTZ(parent.type <= SECRET_TYPE_PRE_ADDRESS && _type > SECRET_TYPE_PRE_ADDRESS);

	SpendSecrets txsecrets;

	auto rc = parent.ExtractToTxSecrets(params, &txsecrets[0], sizeof(txsecrets));
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::DeriveSecret error extracting to tx txsecrets";

		return -1;
	}

	//tx_dump_spend_secret_params_stream(cerr, params);
	//tx_dump_spend_secret_stream(cerr, txsecrets[0], 0);

	rc = SetFromTxSecrets(_type, parent.id, params, &txsecrets[0], sizeof(txsecrets));
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::DeriveSecret error generating secret type " << _type;

		return -1;
	}

	rc = dbconn->SecretInsert(*this, 1);
	if (rc <= 0) return rc;

	// FUTURE: if rc = 1 (constraint violation?), double check that the secret was previously imported?

	auto _value = value;

	rc = dbconn->SecretSelectSecret(&_value, ValueBytes(), *this);

	return rc;
}

int Secret::CreateNextSecretNumber(DbConn *dbconn, unsigned _type, unsigned target_type, uint64_t _parent_id, SpendSecretParams& params)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(debug) << "Secret::CreateNextSecretNumber intermediate type " << _type << " target type " << target_type << " parent id " << _parent_id;

	// lock db

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	// read parent secret (makes sure it isn't changed by another thread since db was locked)

	Secret parent;

	rc = dbconn->SecretSelectId(_parent_id, parent);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error reading parent secret id " << _parent_id;

		return -1;
	}

	CCASSERT(parent.TypeHierarchy() + 1 == TypeHierarchy(_type));

	SpendSecrets txsecrets;

	rc = parent.ExtractToTxSecrets(params, &txsecrets[0], sizeof(txsecrets));
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error extracting to tx txsecrets";

		return -1;
	}

	if (parent.type == SECRET_TYPE_PRE_DESTINATION || parent.type == SECRET_TYPE_PRE_ADDRESS)
	{
		CCASSERT(parent.parent_id);

		Secret super;

		auto rc = dbconn->SecretSelectId(parent.parent_id, super);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error retrieving parent id " << parent.id << " super parent id " << parent.parent_id;

			return -1;
		}

		rc = super.ExtractToTxSecrets(params, &txsecrets[0], sizeof(txsecrets));
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error extracting super parent to tx txsecrets";

			return -1;
		}

		if (parent.type == SECRET_TYPE_PRE_ADDRESS)
			params.addrparams.__dest_id = parent.parent_id;
	}

	Secret midsecret;

	while (true)
	{
		if (_type > SECRET_TYPE_PRE_ADDRESS)
		{
			if (params.__static_address)
				params.addrparams.__paynum = 0;
			else
				params.addrparams.__paynum = parent.number;
		}
		else if (_type > SECRET_TYPE_PRE_DESTINATION)
			params.____destnum = parent.number;
		else
			txsecrets[0].____spend_secret_number = parent.number;

		if (_type != SECRET_TYPE_STATIC_ADDRESS)
		{
			if (parent.IncrementNumber())
				return -1;					// TODO eventually: instead of failing, climb back up hierarchy and generate new parent
		}

		//tx_dump_spend_secret_params_stream(cerr, params);
		//tx_dump_spend_secret_stream(cerr, txsecrets[0], 0);

		rc = midsecret.SetFromTxSecrets(_type, parent.id, params, &txsecrets[0], sizeof(txsecrets));
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error generating secret type " << _type;

			return -1;
		}

		if (!(midsecret.TypeIsDestination()))
			break;
		else if (midsecret.AcceptanceRequired() == params.__acceptance_required && midsecret.HasStaticAddress() == params.__static_address)
			break;
	}

	if (midsecret.type >= SECRET_TYPE_RECV_ADDRESS)
	{
		midsecret.UpdatePollingTimes();
	}

	rc = dbconn->SecretInsert(midsecret);
	if (rc > 0 && _type > SECRET_TYPE_PRE_ADDRESS)
	{
		// load existing address

		auto _value = midsecret.value;

		rc = dbconn->SecretSelectSecret(&_value, TX_ADDRESS_BYTES, midsecret);
		if (rc)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error reading existing address";

			return -1;
		}

		if (!params.__static_address && midsecret.type < SECRET_TYPE_RECV_ADDRESS)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber unexpected existing address " << midsecret.DebugString();

			return -1;
		}
	}
	else if (rc)
	{
		// FUTURE: check if this failed because the secret was previously imported and already exists

		BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error storing intermediate secret";

		return -1;
	}

	rc = dbconn->SecretInsert(parent);
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error storing parent id " << parent.id;

		return -1;
	}

	rc = dbconn->Commit();
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::CreateNextSecretNumber error committing db transaction";

		return -1;
	}

	dbconn->DoDbFinishTx();

	finally.Clear();

	return CreateNewSecret(dbconn, target_type, midsecret.id, params.addrparams.dest_chain, params, true);
}

int Secret::GetParentValue(DbConn *dbconn, unsigned _type, uint64_t _parent_id, SpendSecretParams& params, SpendSecret *txsecrets, unsigned size)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::GetParentValue type " << _type << " _parent_id " << _parent_id;

	while (true)
	{
		if (!_parent_id)
			return 1;

		Secret secret;
		auto rc = dbconn->SecretSelectId(_parent_id, secret);
		if (rc) return rc;

		rc = secret.ExtractToTxSecrets(params, txsecrets, size);
		if (rc) return rc;

		if (secret.type <= _type)
			break;

		_parent_id = secret.parent_id;
	}

	for (unsigned i = 0; i < size/sizeof(*txsecrets); ++i)
	{
		auto errmsg = compute_or_verify_secrets(params, *(txsecrets + i), false);
		if (errmsg)
		{
			BOOST_LOG_TRIVIAL(error) << "Secret::GetParentValue compute_or_verify_secrets " << i << " failed error: " << errmsg;

			return -1;
		}
	}

	return 0;
}

int Secret::ImportSecret(DbConn *dbconn)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(debug) << "Secret::ImportSecret " << DebugString();

	CCASSERTZ(id);

	// add secret to DB, and if that fails, attempt to retrieve existing (already imported) secret from DB
	// TODO: whenever a secret is added, attempt to link to parent and child secrets

	auto rc = dbconn->SecretInsert(*this, 1);
	if (rc <= 0) return rc;

	auto _value = value;

	rc = dbconn->SecretSelectSecret(&_value, ValueBytes(), *this);

	return rc;
}

int Secret::CheckForConflict(DbConn *dbconn, TxQuery& txquery, uint64_t _dest_chain) const
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::CheckForConflict _dest_chain " << _dest_chain << " " << DebugString();

	SpendSecretParams params;
	SpendSecret txsecret;

	auto rc = ExtractToTxSecrets(params, &txsecret, sizeof(txsecret));
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::CheckForConflict error extracting to tx txsecrets";

		throw txrpc_wallet_error;
	}

	params.addrparams.dest_chain = _dest_chain;
	params.addrparams.__paynum = 0;

	//tx_dump_spend_secret_params_stream(cerr, params);
	//tx_dump_spend_secret_stream(cerr, txsecrets[0], 0);

	Secret address, check;

	rc = address.SetFromTxSecrets(SECRET_TYPE_SEND_ADDRESS, id, params, &txsecret, sizeof(txsecret));
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::CheckForConflict error generating address";

		throw txrpc_wallet_error;
	}

	// check if address already in wallet

	rc = dbconn->SecretSelectSecret(&address.value, TX_ADDRESS_BYTES, check);
	if (rc < 0) throw txrpc_wallet_db_error;
	if (!rc)
		return 0;	// no conflict if wallet has already used this destination

	// query address and see if it has already been used on the blockchain

	QueryAddressResults results;
	rc = txquery.QueryAddress(_dest_chain, address.value, query_commitnum, results);
	if (g_shutdown) throw txrpc_shutdown_error;
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::CheckForConflict query address failed";

		throw txrpc_server_error;
	}

	bool already_used = (results.nresults > 0);

	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(debug) << "Secret::CheckForConflict _dest_chain " << _dest_chain << " already_used " << already_used << " address " << hex << address.value << dec;
	//cerr << "CheckForConflict already_used " << already_used << " address " << hex << address.value << dec << endl;

	return already_used;
}

int Secret::CreatePollingAddresses(DbConn *dbconn, uint64_t _dest_chain, SpendSecretParams& params) const
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::CreatePollingAddresses " << DebugString();

	Secret address;

	auto rc = address.CreateNewSecret(dbconn, SECRET_TYPE_STATIC_ADDRESS, id, _dest_chain, params);
	if (rc) return rc;

	if (!HasStaticAddress())
	{
		// change SECRET_TYPE_STATIC_ADDRESS to SECRET_TYPE_RECV_ADDRESS and add some SECRET_TYPE_POLL_ADDRESS
		auto rc = address.SetPollingAddresses(dbconn, *this, g_params.polling_addresses, true, true);
		if (rc) return rc;
	}

	return 0;
}

int Secret::UpdatePollingAddresses(DbConn *dbconn, const Secret &destination)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::UpdatePollingAddresses " << DebugString();

	CCASSERT(dest_id == destination.id);

	if (number >= MaxNumber(SECRET_TYPE_RECV_ADDRESS))
		return 0;

	++number;

	return SetPollingAddresses(dbconn, destination, g_params.polling_addresses, true, false);
}

int Secret::SetPollingAddresses(DbConn *dbconn, const Secret &destination, unsigned polling_addresses, bool update_current, bool is_new)
{
	SpendSecret txsecrets;
	SpendSecretParams params;
	memset((void*)&params, 0, sizeof(params));

	auto rc = destination.ExtractToTxSecrets(params, &txsecrets, sizeof(txsecrets));
	if (rc) return rc;

	// set or change the address for the next payment to SECRET_TYPE_RECV_ADDRESS and force an immediate check

	params.addrparams.dest_chain = dest_chain;
	params.addrparams.__paynum = number;

	rc = SetFromTxSecrets(SECRET_TYPE_RECV_ADDRESS, parent_id, params, &txsecrets, sizeof(txsecrets));
	if (rc) return rc;

	// lock db

	rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	Secret secret;

	rc = dbconn->SecretSelectSecret(&value, TX_ADDRESS_BYTES, secret);
	if (rc < 0) return rc;
	if (!rc)
		*this = secret;

	uint64_t now = time(NULL);

	if (update_current)
	{
		type = SECRET_TYPE_RECV_ADDRESS;

		if (is_new)
			UpdatePollingTimes(now);
		else
			next_check = 1;	// check now

		rc = dbconn->SecretInsert(*this);
		if (rc) return rc;
	}

	// add more SECRET_TYPE_POLL_ADDRESS if needed

	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::UpdatePollingAddresses polling_addresses " << polling_addresses << " parent " << parent_id;

	for (unsigned i = 0; i < polling_addresses; ++i)
	{
		if (IncrementNumber())
			break;

		if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::UpdatePollingAddresses generating paynum " << number;

		params.addrparams.__paynum = number;
		rc = SetFromTxSecrets(SECRET_TYPE_POLL_ADDRESS, parent_id, params, &txsecrets, sizeof(txsecrets));
		if (rc) break;

		UpdatePollingTimes(now);

		rc = dbconn->SecretInsert(*this);
		if (rc < 0) break;
	}

	rc = dbconn->Commit();
	if (rc)
	{
		BOOST_LOG_TRIVIAL(error) << "Secret::UpdatePollingAddresses error committing db transaction";

		return -1;
	}

	dbconn->DoDbFinishTx();

	finally.Clear();

	return 0;
}

int Secret::PollAddress(DbConn *dbconn, TxQuery& txquery, bool update_times)
{
	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "Secret::PollAddress update_times " << update_times << " " << DebugString();

	//BOOST_LOG_TRIVIAL(info) << "Secret::PollAddress dest_id " << dest_id << " paynum " << number;

	CCASSERT(TypeIsAddress());

	/*
		query address for a new payment
		if payment received at address:
			create a billet & tx for the payment, or update existing billet & tx
			if the existing address is not a change or mint address:
				create next address for destination
				link new address to any existing billets if not linked
				query new address
	*/

	int return_code = 0;
	uint64_t now = 0;

	bool is_first = !first_receive;

	Secret destination;

	while (!g_shutdown)
	{
		QueryAddressResults results;
		auto rc = txquery.QueryAddress(dest_chain, value, query_commitnum, results);
		if (rc) goto error;

		if (results.nresults && TRACE_POLLING) BOOST_LOG_TRIVIAL(debug) << "Secret::PollAddress nresults " << results.nresults;

		for (unsigned i = 0; i < results.nresults; ++i)
		{
			QueryAddressResult &result = results.results[i];

			if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "Secret::PollAddress result.commitnum " << result.commitnum;

			if (destination.id != dest_id)
			{
				auto rc = dbconn->SecretSelectId(dest_id, destination);
				if (rc) goto error;
			}

			bool ignore = false;
			bool create_new = false;
			bool update_existing = false;
			bool duplicate_txid = false;

			auto rc = dbconn->BeginWrite();
			if (rc)
			{
				dbconn->DoDbFinishTx(-1);

				goto error;
			}

			Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

			Billet bill;

			rc = dbconn->BilletSelectCommitnum(result.commitnum, bill);
			if (rc < 0)
				goto error;
			if (!rc)
			{
				ignore = true;

				if (result.commitment != bill.commitment)
					BOOST_LOG_TRIVIAL(info) << "Secret::PollAddress commitment mismatch: bill commitnum " << bill.commitnum << " commitment " << bill.commitment << " query result commitnum " << result.commitnum << " commitment " << result.commitment;
			}

			if (!ignore)
			{
				rc = dbconn->BilletSelectTxid(&value, &result.commitment, bill);
				if (rc < 0)
					goto error;

				if (rc && destination.type == SECRET_TYPE_SEND_DESTINATION)
					ignore = true;
				else if (rc)
					create_new = true;			// billet doesn't exist, so create it
				else if (bill.BillIsPending())
					update_existing = true;
				else
				{
					CCASSERT(bill.commitnum != result.commitnum || bill.commitnum == 0);

					duplicate_txid = true;		// can happen when two wallets are sending tx's to the same destination
					create_new = true;			// billet doesn't exist, so create it
				}
			}

			if (update_existing)
			{
				CCASSERTZ(create_new);

				Transaction tx;

				auto rc = tx.ReadTx(dbconn, bill.create_tx);
				if (rc) return rc;

				if (tx.status == TX_STATUS_ERROR)
				{
					create_new = true;
					duplicate_txid = true;
				}
				else
				{
					rc = tx.UpdateStatus(dbconn, bill.id, result.commitnum);
					if (rc) goto error;
				}
			}

			if (create_new)
			{
				Transaction tx;

				rc = tx.CreateTxFromAddressQueryResult(dbconn, txquery, destination, *this, result, duplicate_txid);
				if (rc) goto error;
			}
			else
				(void)ignore;

			// commit db writes

			rc = dbconn->Commit();
			if (rc)
			{
				BOOST_LOG_TRIVIAL(fatal) << "Secret::PollAddress error committing db transaction";

				goto error;
			}

			dbconn->DoDbFinishTx();

			finally.Clear();

			if (query_commitnum <= result.commitnum)
				query_commitnum = result.commitnum + 1;
		}

		if (results.nresults)
		{
			now = time(NULL);
			last_receive = now;
			if (!first_receive)
				first_receive = now;

			update_times = true;
			return_code = 1;
		}

		if (!results.more_results)
			break;
	}

	goto no_err;

error:
	update_times = true;
	return_code = -1;

no_err:

	if (update_times)
		UpdateSavePollingTimes(dbconn, now, true);

	bool update_addresses = (last_receive && is_first && (type == SECRET_TYPE_RECV_ADDRESS || type == SECRET_TYPE_POLL_ADDRESS));

	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::PollAddress update_addresses " << update_addresses << " last_receive " << last_receive << " is_first " << is_first << " type " << type;

	if (update_addresses)
	{
		auto rc = UpdatePollingAddresses(dbconn, destination);
		(void)rc;
	}

	return return_code;
}

int Secret::UpdateSavePollingTimes(DbConn *dbconn, uint64_t now, bool checked_now)
{
	if (TRACE_POLLING) BOOST_LOG_TRIVIAL(trace) << "Secret::UpdateSavePollingTimes";

	auto rc = dbconn->BeginWrite();
	if (rc)
	{
		dbconn->DoDbFinishTx(-1);

		return -1;
	}

	Finally finally(boost::bind(&DbConn::DoDbFinishTx, dbconn, 1));		// 1 = rollback

	auto _query_commitnum = query_commitnum;
	auto _first_receive = first_receive;
	auto _last_receive = last_receive;

	rc = dbconn->SecretSelectId(id, *this);
	if (rc) return -1;

	query_commitnum = _query_commitnum;
	first_receive = _first_receive;
	last_receive = _last_receive;

	UpdatePollingTimes(now, checked_now);

	rc = dbconn->SecretInsert(*this);
	if (rc) return -1;

	rc = dbconn->Commit();
	if (rc)
	{
		BOOST_LOG_TRIVIAL(fatal) << "Secret::UpdateSavePollingTimes error committing db transaction";

		return -1;
	}

	dbconn->DoDbFinishTx();

	finally.Clear();

	return 0;
}

int Secret::UpdatePollingTimes(uint64_t now, bool checked_now)
{
	CCASSERT(TypeIsAddress());

	if (!now)
		now = time(NULL);

	if (!create_time)
		create_time = now;

	if (checked_now)
		last_check = now;

	if (last_receive && type == SECRET_TYPE_POLL_ADDRESS)
		type = SECRET_TYPE_RECV_ADDRESS;

	unsigned dim0 = type - SECRET_TYPE_SEND_ADDRESS;
	bool dim1 = last_receive && query_commitnum > expected_commitnum;

	CCASSERT(dim0 < g_params.polling_table.size());

	int64_t ref_time = last_receive ? last_receive : create_time;
	int64_t tier_time = now - ref_time;
	unsigned tier = -1;

	for (unsigned i = 0; i < g_params.polling_table[dim0][dim1].size(); ++i)
	{
		//cout << "polling_table[" << dim0 << "][" << dim1 << "][" << i << "].second = " << g_params.polling_table[dim0][dim1][i].second << endl;

		if (tier_time < (int64_t)g_params.polling_table[dim0][dim1][i].second)
		{
			tier = i;
			break;
		}
	}

	if (tier == (unsigned)(-1))
		next_check = 0;
	else
	{
		auto dt = g_params.polling_table[dim0][dim1][tier].first;

		#if TEST_RANDOM_POLLING
		dt = (rand() % TEST_RANDOM_POLLING) + 1;
		#endif

		if (last_check)
			next_check = last_check + dt;
		else
			next_check = now + (rand() % ((dt*3+1)/2));		// for privacy, randomize first check time
	}

	if (TRACE_SECRETS || TRACE_POLLING) BOOST_LOG_TRIVIAL(debug) << "Secret::UpdatePollingTimes id " << id << " type " << type << " dest_id " << dest_id << " paynum " << number << " create_time " << create_time << " last_receive " << last_receive << " last_check " << last_check << " now " << now  << " tier_time " << tier_time << " tier " << tier << " dim1 " << dim1 << " next_check " << next_check;

	return 0;
}

int Secret::PollDestination(DbConn *dbconn, TxQuery& txquery, uint64_t dest_id, unsigned polling_addresses, uint64_t last_receive_max)
{
	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::PollDestination dest_id " << dest_id << " polling_addresses " << polling_addresses << " last_receive_max " << last_receive_max;

	TxParams txparams;

	auto rc = g_txparams.GetParams(txparams, txquery);
	if (rc) return rc;

	uint64_t next_id = 0;
	bool update_addresses = false;
	Secret destination, address, update_address;

	while (next_id < INT64_MAX && !g_shutdown)
	{
		auto rc = dbconn->SecretSelectDestination(dest_id, next_id, address);
		if (rc < 0)
			return -1;
		if (rc || address.dest_id != dest_id)
		{
			if (!polling_addresses || !update_addresses)
				break;

			if (!destination.id)
			{
				rc = dbconn->SecretSelectId(dest_id, destination);
				if (rc) return rc;
			}

			rc = update_address.SetPollingAddresses(dbconn, destination, polling_addresses);
			if (rc) return rc;

			update_addresses = false;

			continue;
		}

		next_id = address.id + 1;

		bool skip = (address.dest_chain != txparams.blockchain || (last_receive_max && address.last_receive && (time(NULL) - address.last_receive) > last_receive_max));

		if (!skip)
		{
			rc = address.PollAddress(dbconn, txquery);
			if (rc < 0)
				return -1;
		}
		else
			if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::PollDestination dest_id " << dest_id << " skipping address " << address.id << " dest_chain " << address.dest_chain << " last_receive " << address.last_receive;

		if (address.type != SECRET_TYPE_POLL_ADDRESS || (!skip && rc))
		{
			update_address = address;
			update_addresses = true;
		}
	}

	if (TRACE_SECRETS) BOOST_LOG_TRIVIAL(trace) << "Secret::PollDestination dest_id " << dest_id << " done";

	return 0;
}
