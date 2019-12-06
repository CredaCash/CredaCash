/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * txparams.hpp
*/

#pragma once

#include <CCbigint.hpp>

class TxParams
{
public:
	 int64_t clock_diff;
	uint64_t server_version;
	uint64_t protocol_version;
	uint64_t effective_level;
	uint64_t query_work_difficulty;
	uint64_t tx_work_difficulty;
	uint64_t blockchain;
	uint64_t block_level;
	uint64_t oldest_commitnum;
	uint64_t next_commitnum;
	uint16_t connected;
	uint16_t asset_bits;
	uint16_t amount_bits;
	uint16_t donation_bits;
	uint16_t exponent_bits;
	uint16_t outvalmin;
	uint16_t outvalmax;
	uint16_t invalmax;
	uint32_t default_output_pool;
	uint64_t donation_per_tx;
	uint64_t donation_per_byte;
	uint64_t donation_per_output;
	uint64_t donation_per_input;
	uint64_t minimum_donation;

	unsigned update_counter;

	TxParams();

	bool NotConnected() const;

	unsigned ComputeTxSize(unsigned nout, unsigned nin) const;
	void ComputeDonation(unsigned nout, unsigned nin, snarkfront::bigint_t& donation) const;
};

class TxQuery;

class TxParamQuery
{
	mutex m_update_mutex;
	mutex m_fetch_mutex;

	TxParams m_params;

public:

	TxParamQuery();

	int GetParams(TxParams& txparams, TxQuery& txquery);
	int UpdateParams(TxParams& txparams, TxQuery& txquery);
	int FetchParams(TxParams& txparams, TxQuery& txquery, bool force = false);

private:

};

extern TxParamQuery g_txparams;
