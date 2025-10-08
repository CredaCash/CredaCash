/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * txparams.hpp
*/

#pragma once

#include <CCbigint.hpp>
#include <BlockChainStatus.hpp>

class TxParams
{
public:
	 int64_t clock_diff;
	uint64_t server_version;
	uint64_t protocol_version;
	uint16_t queries_only;
	uint64_t params_last_modified_level;
	uint64_t query_work_difficulty;
	uint64_t tx_work_difficulty;
	uint64_t xcx_naked_buy_work_difficulty;
	uint64_t xcx_pay_work_difficulty;
	uint64_t xcx_minimum_expiration;
	uint64_t blockchain;
	BlockChainStatus blockchain_status;
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
	uint32_t default_domain;
	uint64_t minimum_donation;
	uint64_t donation_per_tx;
	uint64_t donation_per_byte;
	uint64_t donation_per_output;
	uint64_t donation_per_input;
	uint64_t donation_per_xcx_req;

	unsigned update_counter;

	TxParams()
	{
		Clear();
	}

	void Clear()
	{
		memset((void*)this, 0, sizeof(*this));
	}

	bool NotConnected() const;

	unsigned ComputeTxSize(unsigned nout, unsigned nin) const;
	void ComputeDonation(unsigned type, unsigned nbytes, unsigned nout, unsigned nin, snarkfront::bigint_t& donation) const;
};

class TxQuery;

class TxParamQuery
{
	mutex m_update_mutex;
	mutex m_fetch_mutex;

	TxParams m_params;

public:

	TxParamQuery()
	{
		Reset();
	}

	void Reset();

	int GetParams(TxParams& txparams, TxQuery& txquery);
	int UpdateParams(TxParams& txparams, TxQuery& txquery);
	int FetchParams(TxParams& txparams, TxQuery& txquery, bool force = false);

private:

};

extern TxParamQuery g_txparams;
