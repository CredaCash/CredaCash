/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * exchange_mining.hpp
*/

#pragma once

#include "dbconn.hpp"

#include <unifloat.hpp>
#include <SpinLock.hpp>

struct TxPay;

#pragma pack(push, 1)

struct ExchangeMatchStats
{
	UniFloat avg_amount;
	UniFloat avg_amount_weight;
	UniFloat avg_match_rate_required;
	UniFloat avg_match_rate_required_weight;
	UniFloat avg_match_rate;
	UniFloat avg_match_rate_weight;
};

struct ExchangeMiningParams
{
	snarkfront::bigint_t total_mined;
	snarkfront::bigint_t total_remaining_to_mine;

	ExchangeMatchStats mining_stats;

	UniFloat mining_remaining_fraction_per_interval;
	UniFloat mining_min_amount_per_interval;
	UniFloat mining_max_currently_mineable_intervals;
	UniFloat last_nominal_mineable_amount_increase;
	UniFloat currently_mineable_amount;
	UniFloat max_currently_mineable_amount;

	UniFloat mining_short_decay_factor;
	UniFloat mining_long_decay_factor;

	UniFloat mining_amount_multiplier;
	UniFloat mining_max_fraction_per_match;
	UniFloat mining_min_fraction_per_match;

	uint64_t mining_period;

	uint16_t mining_update_time_increment;
};

#pragma pack(pop)

class ExchangeMining
{
	friend class BlockChain;

	ExchangeMiningParams saved;
	ExchangeMiningParams copy;

	FastSpinLock copy_lock;

	uint32_t saved_data_update_counter;
	uint32_t copy_data_update_counter;

	void SnapshotMiningParams();

	bool UpdateMiningStats(const Xmatch& match, UniFloat& base_amount, UniFloat& buyer_match_rate_required);

	UniFloat ComputeMiningAmount(const UniFloat& base_amount, const UniFloat& buyer_match_rate_required);

	void TestUpdateMiningAmount(Xmatch& match);

public:

	string DebugString(bool incl_consts = false) const;

	ExchangeMining()
	 :	copy_lock(__FILE__, __LINE__)
	{ }

	void Init();
	int SaveMining(DbConn *dbconn);
	int RestoreMining(DbConn *dbconn);

	void GetMiningParams(ExchangeMiningParams &params);

	bool UpdateMiningTime(uint64_t timestamp);

	void SetMiningAmount(Xmatch& match);
	void GetAdjustedMiningAmount(Xmatch& match, snarkfront::bigint_t& adj_mining_amount);
	void FinalizeMiningAmount(Xmatch& match, const snarkfront::bigint_t& adj_mining_amount);
	void UpdateMatchStats(Xmatch& match, const snarkfront::bigint_t& seller_amount);

	void Test();
	void Test1();
	void Test2();
	void Test3();

	uint64_t mining_start_time;
	uint64_t mined_asset;
};

extern ExchangeMining g_exchange_mining;
