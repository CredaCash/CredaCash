/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * walletutil.cpp
*/

#include "ccwallet.h"
#include "walletutil.h"
#include "rpc_errors.hpp"
#include "walletdb.hpp"

#include <encode.h>
#include <siphash/siphash.h>

using namespace snarkfront;

string unique_id_generate(DbConn *dbconn, const string& prefix, unsigned random_bits, unsigned checksum_chars)
{
	static mutex unique_refid_mutex;

	while (true)
	{
		if (g_shutdown)
			throw txrpc_shutdown_error;

		bigint_t refid;

		{
			lock_guard<mutex> lock(unique_refid_mutex);

			auto rc = dbconn->ParameterSelect(DB_KEY_UNIQUE_REFID, 0, &refid, sizeof(refid));
			if (rc) throw txrpc_wallet_db_error;

			refid = refid + bigint_t(1UL);

			rc = dbconn->ParameterInsert(DB_KEY_UNIQUE_REFID, 0, &refid, sizeof(refid), true);
			if (rc) throw txrpc_wallet_db_error;
		}

		string outs = prefix;

		cc_stringify(base57sym, 0UL, false, -1, refid, outs);

		if (random_bits)
		{
			bigint_t rval, maxval;

			subBigInt(bigint_t(0UL), bigint_t(1UL), maxval, false);

			CCRandom(&rval, (random_bits+7)/8);

			bigint_mask(rval, random_bits);
			bigint_mask(maxval, random_bits);

			cc_stringify(base57sym, maxval, false, 0, rval, outs);
		}

		auto hash = siphash(outs.data(), outs.length());
		cc_stringify(base57sym, 0UL, false, checksum_chars, hash, outs);

		auto lower = boost::to_lower_copy(outs);

		if (boost::contains(lower, "fatal"))
			continue;

		if (boost::contains(lower, "warn"))
			continue;

		if (boost::contains(lower, "err"))
			continue;

		if (boost::contains(lower, "invalid"))
			continue;

		if (boost::contains(lower, "info"))
			continue;

		if (boost::contains(lower, "debug"))
			continue;

		if (boost::contains(lower, "trace"))
			continue;

		return outs;
	}
}
