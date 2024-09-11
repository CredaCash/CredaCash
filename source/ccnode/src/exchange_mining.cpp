/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * exchange_mining.cpp
*/

#include "ccnode.h"
#include "exchange_mining.hpp"
#include "blockchain.hpp"
#include "block.hpp"
#include "witness.hpp"
#include "dbparamkeys.h"

#include <xtransaction-xpay.hpp>
#include <xmatch.hpp>

#define MINING_MIN_CUTOFF_FACTOR				0.05
#define MINING_MAX_CUTOFF_FACTOR				2

#define MINING_MULTIPLIER_INC_THRESHOLD			0.5
#define MINING_MULTIPLIER_INC_AMOUNT			0.02
#define MINING_MULTIPLIER_MAX					1

#define MINING_MULTIPLIER_DEC_THRESHOLD			0.5
#define MINING_MULTIPLIER_DEC_MULTIPLIER		0.98
#define MINING_MULTIPLIER_MIN					0.01

#define MINING_MIN_CUTOFF_AMOUNT				1
#define MINING_AMOUNT_MIN_MAX					1

#define TRACE_EXCHANGE_MINING	(g_params.trace_exchange_mining)

ExchangeMining g_exchange_mining;

/*
	Mining Amount Accounting:

	variables:
		bigint_t total_mined
		bigint_t total_remaining_to_mine
		UniFloat currently_mineable_amount
		for each match: UniFloat mining_amount

	when match.mining_amount is set:
		currently_mineable_amount -= match.mining_amount

	when match becomes XMATCH_STATUS_PAID:
		match.mining_amount = max(match.mining_amount, total_remaining_to_mine)
		a single TxOut is created
		match.mining_amount -= TxOut residual
		total_remaining_to_mine -= match.mining_amount
		total_mined += match.mining_amount
*/

string ExchangeMining::DebugString(bool incl_consts) const
{
	ostringstream out;
	string amts;

	out << "ExchangeMining";
	if (incl_consts) out << " mining_start_time " << mining_start_time;
	if (incl_consts) out << " mining_update_time_increment " << saved.mining_update_time_increment;

	out << " mining_period " << saved.mining_period;

	amount_to_string(MINED_ASSET, saved.total_mined, amts);
	out << " total_mined " << saved.total_mined << " = " << amts;
	amount_to_string(MINED_ASSET, saved.total_remaining_to_mine, amts);
	out << " total_remaining_to_mine " << saved.total_remaining_to_mine << " = " << amts;

	if (incl_consts) out << " mining_remaining_fraction_per_interval " << saved.mining_remaining_fraction_per_interval;
	if (incl_consts) out << " mining_min_amount_per_interval " << saved.mining_min_amount_per_interval;
	if (incl_consts) out << " mining_max_currently_mineable_intervals " << saved.mining_max_currently_mineable_intervals;

	out << " last_nominal_mineable_amount_increase " << saved.last_nominal_mineable_amount_increase;
	out << " currently_mineable_amount " << saved.currently_mineable_amount;
	out << " max_currently_mineable_amount " << saved.max_currently_mineable_amount;

	if (incl_consts) out << " mining_short_decay_factor " << saved.mining_short_decay_factor;
	if (incl_consts) out << " mining_long_decay_factor " << saved.mining_long_decay_factor;

	out << " mining_amount_multiplier " << saved.mining_amount_multiplier;
	if (incl_consts) out << " mining_max_fraction_per_match " << saved.mining_max_fraction_per_match;
	if (incl_consts) out << " mining_min_fraction_per_match " << saved.mining_min_fraction_per_match;

	out << " avg_amount " << saved.mining_stats.avg_amount;
	out << " avg_amount_weight " << saved.mining_stats.avg_amount_weight;

	out << " avg_match_rate_required " << saved.mining_stats.avg_match_rate_required;
	out << " avg_match_rate_required_weight " << saved.mining_stats.avg_match_rate_required_weight;

	out << " avg_match_rate " << saved.mining_stats.avg_match_rate;
	out << " avg_match_rate_weight " << saved.mining_stats.avg_match_rate_weight;

	out << " data_update_counter " << saved_data_update_counter;

	return out.str();
}

void ExchangeMining::Init()
{
	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(trace) << "ExchangeMining::Init";

	memset((void*)this, 0, sizeof(*this));

	saved_data_update_counter = 0;
	copy_data_update_counter = -1;

	uint64_t total_to_mine = 0.2 * 200000 * 50000 + 0.5;
	mined_asset = MINED_ASSET;
	saved.mining_update_time_increment = 4*60;

	if (IsTestnet(g_params.blockchain))
		mining_start_time = 1718193600;							// 12-Jun-2024 12:00:00 GMT
	else
		mining_start_time = 1718467200;							// 15-Jun-2024 16:00:00 GMT

	saved.mining_short_decay_factor = UniFloat(0.97716, 0);		// half life =  30 periods =  2 hours
	saved.mining_long_decay_factor  = UniFloat(0.99615659, 0);	// half life = 180 periods = 12 hours

	//cerr << saved.mining_short_decay_factor.asFloat() - exp2(-1/( 2*3600.0/saved.mining_update_time_increment)) << endl;
	//cerr << saved.mining_long_decay_factor.asFloat()  - exp2(-1/(12*3600.0/saved.mining_update_time_increment)) << endl;

	saved.mining_amount_multiplier = UniFloat(MINING_MULTIPLIER_MAX, 0);	// initial value

	saved.mining_max_fraction_per_match = UniFloat(0.05, 0);
	saved.mining_min_fraction_per_match = UniFloat(0.01, 0);

#if 0	// for testing

	mining_start_time = 0;				// 0 = no exchange mining
	mining_start_time = 1714968000;
	//mining_start_time = unixtime() - saved.mining_update_time_increment;	// only works with single server that isn't restarted

	//saved.mining_long_decay_factor = 0.99;

#endif

	mining_start_time += XCX_MATCHING_SECS_PER_EPOCH - 1;
	mining_start_time /= XCX_MATCHING_SECS_PER_EPOCH;
	mining_start_time *= XCX_MATCHING_SECS_PER_EPOCH;

	saved.total_mined = 0UL;
	amount_from_float(MINED_ASSET, total_to_mine, saved.total_remaining_to_mine);

	//saved.mining_remaining_fraction_per_interval = UniFloat::Divide(1000 * saved.mining_update_time_increment, 60 * total_to_mine, 0);

	saved.mining_remaining_fraction_per_interval = UniFloat(1.7583627e-6, 0);	// half life = 394,200 periods = 3 years

	//cerr << 1 - saved.mining_remaining_fraction_per_interval.asFloat() - exp2(-1/(3*365*86400.0/saved.mining_update_time_increment)) << endl;

	saved.mining_min_amount_per_interval = 100;	// mining done in approx 20 years = 20*365*86400/mining_update_time_increment = 2,522,880 periods

	saved.mining_max_currently_mineable_intervals = UniFloat::Divide(3600, saved.mining_update_time_increment, 1); // 1 hour

	saved.mining_stats.avg_amount = 500;
	double					 rate = 1.0/5000;
	saved.mining_stats.avg_match_rate_required = UniFloat(rate, 0);
	saved.mining_stats.avg_match_rate = UniFloat(rate, 0);

	saved.mining_stats.avg_amount_weight = 100;	// init weights so first mining match doesn't completely reset the averages
	saved.mining_stats.avg_match_rate_required_weight = UniFloat::Multiply(100, saved.mining_stats.avg_amount);

	BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Init mining_start_time " << mining_start_time;
	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(trace) << "ExchangeMining::Init " << DebugString(true);
	//cerr << "ExchangeMining::Init " << DebugString(true) << endl;
}

int ExchangeMining::SaveMining(DbConn *dbconn)
{
	if (g_shutdown) return 0;

	if (saved_data_update_counter == copy_data_update_counter)
		return 0;	// nothing changed

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(trace) << "ExchangeMining::SaveMining " << DebugString();

	auto rc = dbconn->ParameterInsert(DB_KEY_XMINING, 0, &saved, sizeof(saved));

	if (rc) BOOST_LOG_TRIVIAL(error) << "ExchangeMining::SaveMining error saving mining parameters";

	SnapshotMiningParams();

	return rc;
}

int ExchangeMining::RestoreMining(DbConn *dbconn)
{
	//if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(trace) << "ExchangeMining::RestoreMining";

	lock_guard<FastSpinLock> lock(copy_lock);

	auto rc = dbconn->ParameterSelect(DB_KEY_XMINING, 0, &copy, sizeof(copy));

	if (rc < 0)
	{
		BOOST_LOG_TRIVIAL(error) << "ExchangeMining::RestoreMining error restoring mining parameters";

		return -1;
	}
	else if (!rc)
	{
		memcpy((void*)&saved, &copy, sizeof(saved));

		if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(info) << "ExchangeMining::RestoreMining " << DebugString(true);
	}

	return 0;
}

void ExchangeMining::SnapshotMiningParams()
{
	lock_guard<FastSpinLock> lock(copy_lock);

	memcpy((void*)&copy, &saved, sizeof(saved));

	copy_data_update_counter = saved_data_update_counter;
}

void ExchangeMining::GetMiningParams(ExchangeMiningParams &params)
{
	lock_guard<FastSpinLock> lock(copy_lock);

	memcpy((void*)&params, &copy, sizeof(copy));
}

static void UpdateWeightedAverage(UniFloat& avg, UniFloat& weight, const UniFloat& amt, const UniFloat& new_weight)
{
	avg = UniFloat::Multiply(avg, weight);
	avg = UniFloat::Add(avg, amt);

	weight = UniFloat::Add(weight, new_weight);

	avg = UniFloat::Divide(avg, weight);
}

bool ExchangeMining::UpdateMiningStats(const Xmatch& match, UniFloat& base_amount, UniFloat& buyer_match_rate_required)
{
	// returns true if not mineable

	base_amount = Xtx::asUniFloat(match.xsell.base_asset, match.base_amount);

	if (base_amount <= 0)
	{
		BOOST_LOG_TRIVIAL(warning) << "ExchangeMining::UpdateMiningStats base_amount <= 0 ; " << match.DebugString();

		return true;
	}

	UpdateWeightedAverage(saved.mining_stats.avg_amount, saved.mining_stats.avg_amount_weight, base_amount, 1);

	//BOOST_LOG_TRIVIAL(info) << "ExchangeMining::UpdateMiningStats xmatchnum " << match.xmatchnum << " amount " << base_amount << " avg " << saved.mining_stats.avg_amount << " weight " << saved.mining_stats.avg_amount_weight;

	// no rate tracking or mining for very small matches
	if (base_amount < UniFloat::Multiply(saved.mining_stats.avg_amount, MINING_MIN_CUTOFF_FACTOR))
	{
		if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::UpdateMiningStats skipping small " << match.DebugString();

		return true;
	}

	// to prevent whales from manipulating the mining parameters, skip matches larger than twice the average amount
	if (base_amount > UniFloat::Multiply(saved.mining_stats.avg_amount, MINING_MAX_CUTOFF_FACTOR))
	{
		if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::UpdateMiningStats skipping large " << match.DebugString();

		return true;
	}

	if (match.xbuy.net_rate_required <= 0)
	{
		BOOST_LOG_TRIVIAL(warning) << "ExchangeMining::UpdateMiningStats net_rate_required <= 0 ; " << match.DebugString();

		return true;
	}

	Xreq xreq;
	xreq.type = match.xbuy.type;
	xreq.base_costs = match.xbuy.base_costs;
	xreq.quote_costs = match.xbuy.quote_costs;
	xreq.net_rate_required = match.xbuy.net_rate_required;

	buyer_match_rate_required = xreq.MatchRateRequired(base_amount);

	if (match.xbuy.type == CC_TYPE_XCX_MINING_BUY)
		buyer_match_rate_required = UniFloat::Multiply(buyer_match_rate_required, 2);

	if (buyer_match_rate_required <= 0)
		return true;

	auto weighted_rate = UniFloat::Multiply(base_amount, buyer_match_rate_required);

	UpdateWeightedAverage(saved.mining_stats.avg_match_rate_required, saved.mining_stats.avg_match_rate_required_weight, weighted_rate, base_amount);

	return false;
}

bool ExchangeMining::UpdateMiningTime(uint64_t timestamp)
{
	if (timestamp < mining_start_time || !mining_start_time)
		return true;

	auto period = (timestamp - mining_start_time) / saved.mining_update_time_increment + 1;

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::UpdateMiningTime timestamp " << timestamp << " period " << period;

	if (period != saved.mining_period)
	{
		++saved_data_update_counter;

		//if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(trace) << "ExchangeMining::UpdateMiningTime period " << period << " " << DebugString();
	}

	CCASSERT(period >= saved.mining_period);

	while (period > saved.mining_period)
	{
		//if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::UpdateMiningTime  pre-update " << DebugString();

		++saved.mining_period;

		saved.mining_stats.avg_amount_weight = UniFloat::Multiply(saved.mining_stats.avg_amount_weight, saved.mining_long_decay_factor);
		saved.mining_stats.avg_match_rate_required_weight = UniFloat::Multiply(saved.mining_stats.avg_match_rate_required_weight, saved.mining_short_decay_factor);
		saved.mining_stats.avg_match_rate_weight = UniFloat::Multiply(saved.mining_stats.avg_match_rate_weight, saved.mining_long_decay_factor);

		auto remaining = Xtx::asUniFloat(MINED_ASSET, saved.total_remaining_to_mine);

		saved.last_nominal_mineable_amount_increase = UniFloat::Multiply(remaining, saved.mining_remaining_fraction_per_interval);
		if (saved.last_nominal_mineable_amount_increase < saved.mining_min_amount_per_interval)
			saved.last_nominal_mineable_amount_increase = saved.mining_min_amount_per_interval;

		saved.max_currently_mineable_amount = UniFloat::Multiply(saved.last_nominal_mineable_amount_increase, saved.mining_max_currently_mineable_intervals);

		//cerr << "remaining " << remaining << " currently_mineable_amount " << saved.currently_mineable_amount
		//	<< " saved.last_nominal_mineable_amount_increase " << saved.last_nominal_mineable_amount_increase << " max_currently_mineable_amount " << saved.max_currently_mineable_amount << endl;

		saved.currently_mineable_amount = UniFloat::Add(saved.currently_mineable_amount, saved.last_nominal_mineable_amount_increase);
		if (saved.currently_mineable_amount > saved.max_currently_mineable_amount)
			saved.currently_mineable_amount = saved.max_currently_mineable_amount;

		auto current_frac = UniFloat::Divide(saved.currently_mineable_amount, saved.max_currently_mineable_amount);

		if (current_frac > MINING_MULTIPLIER_INC_THRESHOLD)
		{
			saved.mining_amount_multiplier = UniFloat::Add(saved.mining_amount_multiplier, MINING_MULTIPLIER_INC_AMOUNT);
			if (saved.mining_amount_multiplier > MINING_MULTIPLIER_MAX)
				saved.mining_amount_multiplier = MINING_MULTIPLIER_MAX;
		}

		if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::UpdateMiningTime " << DebugString();
	}

	return false;
}

UniFloat ExchangeMining::ComputeMiningAmount(const UniFloat& base_amount, const UniFloat& buyer_match_rate_required)
{
	if (saved.currently_mineable_amount <= 0)
		return 0;

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(trace) << "ExchangeMining::ComputeMiningAmount buyer_match_rate_required " << buyer_match_rate_required << (buyer_match_rate_required <= saved.mining_stats.avg_match_rate_required ? " <=" : " >") << " avg_match_rate_required " << saved.mining_stats.avg_match_rate_required;

	if (buyer_match_rate_required <= saved.mining_stats.avg_match_rate_required)
		return 0;

	auto current_frac = UniFloat::Divide(saved.currently_mineable_amount, saved.max_currently_mineable_amount);

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(trace) << "ExchangeMining::ComputeMiningAmount currently_mineable_amount " << saved.currently_mineable_amount << " max_currently_mineable_amount " << saved.max_currently_mineable_amount << " current_frac " << current_frac;

	auto mining_amount = UniFloat::Multiply(base_amount, saved.mining_amount_multiplier);
	auto max_amount = UniFloat::Multiply(saved.currently_mineable_amount, saved.mining_max_fraction_per_match);
	auto min_max = UniFloat::Multiply(saved.max_currently_mineable_amount, saved.mining_min_fraction_per_match);

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(trace) << "ExchangeMining::ComputeMiningAmount mining_amount " << mining_amount << " mining_amount_multiplier " << saved.mining_amount_multiplier << " max_amount " << max_amount << " min_max " << min_max;

	if (min_max < MINING_AMOUNT_MIN_MAX)
		min_max = MINING_AMOUNT_MIN_MAX;

	if (max_amount < min_max)
		max_amount = min_max;

	if (mining_amount > max_amount)
		mining_amount = max_amount;

	if (mining_amount > saved.currently_mineable_amount)
		mining_amount = saved.currently_mineable_amount;

	if (mining_amount < MINING_MIN_CUTOFF_AMOUNT)
		return 0;

	if (current_frac < MINING_MULTIPLIER_DEC_THRESHOLD)
	{
		saved.mining_amount_multiplier = UniFloat::Multiply(saved.mining_amount_multiplier, MINING_MULTIPLIER_DEC_MULTIPLIER);
		if (saved.mining_amount_multiplier < MINING_MULTIPLIER_MIN)
			saved.mining_amount_multiplier = MINING_MULTIPLIER_MIN;
	}

	return mining_amount;
}

void ExchangeMining::SetMiningAmount(Xmatch& match)
{
	if (!saved.total_remaining_to_mine)
		return;

	if ((match.xbuy.type != CC_TYPE_XCX_SIMPLE_BUY && match.xbuy.type != CC_TYPE_XCX_MINING_BUY)
			|| match.xbuy.quote_asset != XREQ_BLOCKCHAIN_BCH || match.xbuy.base_asset)
		return;

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::SetMiningAmount " << match.DebugString();

	CCASSERT(match.xbuy.flags.have_matching);
	CCASSERT(match.xbuy.destination);

	if (UpdateMiningTime(match.match_timestamp))
		return;

	++saved_data_update_counter;

	UniFloat base_amount, buyer_match_rate_required;

	if (UpdateMiningStats(match, base_amount, buyer_match_rate_required))
		return;

	match.mining_amount = ComputeMiningAmount(base_amount, buyer_match_rate_required);

	bigint_t amount;

	auto rc = amount_from_float(MINED_ASSET, (amtfloat_t)match.mining_amount.asFullString(), amount);
	CCASSERTZ(rc);

	CCASSERT(match.mining_amount <= saved.currently_mineable_amount);
	saved.currently_mineable_amount = UniFloat::Add(saved.currently_mineable_amount, -match.mining_amount);

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::SetMiningAmount " << DebugString();
}

void ExchangeMining::GetAdjustedMiningAmount(Xmatch& match, bigint_t& adj_mining_amount)
{
	adj_mining_amount = 0UL;

	if (match.mining_amount == 0)
		return;

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::GetAdjustedMiningAmount " << match.DebugString();

	CCASSERT(match.status == XMATCH_STATUS_PAID);

	auto rc = amount_from_float(MINED_ASSET, (amtfloat_t)match.mining_amount.asFullString(), adj_mining_amount);
	CCASSERTZ(rc);

	if (adj_mining_amount > saved.total_remaining_to_mine)
		adj_mining_amount = saved.total_remaining_to_mine;
}

void ExchangeMining::FinalizeMiningAmount(Xmatch& match, const bigint_t& adj_mining_amount)
{
	if (match.mining_amount == 0)
		return;

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::FinalizeMiningAmount adj_mining_amount " << adj_mining_amount << " ; " << match.DebugString();

	CCASSERT(match.status == XMATCH_STATUS_PAID);

	match.mining_amount = Xtx::asUniFloat(MINED_ASSET, adj_mining_amount);

	++saved_data_update_counter;

	CCASSERT(adj_mining_amount <= saved.total_remaining_to_mine);
	saved.total_remaining_to_mine = saved.total_remaining_to_mine - adj_mining_amount;
	saved.total_mined = saved.total_mined + adj_mining_amount;

	if (!saved.total_remaining_to_mine)
		saved.currently_mineable_amount = 0;

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::FinalizeMiningAmount adj_mining_amount " << adj_mining_amount << " ; " << DebugString();
	//cerr << "ExchangeMining::FinalizeMiningAmount xmatchnum " << match.xmatchnum << " amount mined " << adj_mining_amount << endl;
}

void ExchangeMining::UpdateMatchStats(Xmatch& match, const bigint_t& buyer_amount)
{
	if ((match.xbuy.type != CC_TYPE_XCX_SIMPLE_BUY && match.xbuy.type != CC_TYPE_XCX_MINING_BUY)
			|| match.xbuy.quote_asset != XREQ_BLOCKCHAIN_BCH || match.xbuy.base_asset)
		return;

	if (!buyer_amount)
		return;

	if (TRACE_EXCHANGE_MINING) BOOST_LOG_TRIVIAL(debug) << "ExchangeMining::UpdateMatchStats buyer_amount " << buyer_amount << " " << match.DebugString();

	// rate = BCH to seller / Creda to buyer

	auto base_amount = Xreq::asUniFloat(match.xbuy.base_asset, buyer_amount);
	auto rate = UniFloat::Divide(match.amount_paid, base_amount);

	auto weighted_rate = UniFloat::Multiply(base_amount, rate);
	UpdateWeightedAverage(saved.mining_stats.avg_match_rate, saved.mining_stats.avg_match_rate_weight, weighted_rate, base_amount);
}

void ExchangeMining::TestUpdateMiningAmount(Xmatch& match)
{
	bigint_t adj_mining_amount;

	SetMiningAmount(match);
	GetAdjustedMiningAmount(match, adj_mining_amount);

	if (adj_mining_amount && (rand() & 1))
		adj_mining_amount = adj_mining_amount - bigint_t(1UL);

	FinalizeMiningAmount(match, adj_mining_amount);
}

void ExchangeMining::Test()
{
	//Test1();
	//Test2();
	//Test3();
	//start_shutdown();
}

void ExchangeMining::Test1()
{
	// simulate the mining schedule

	unsigned log_start = 0;
	unsigned log_end = 0;
	unsigned log_iter_mod = 1000;
	unsigned log_count_mod = 10000;

	//log_start = 1000;
	//log_end = log_start + 1;
	//log_iter_mod = 1;
	//log_count_mod = 1;

	mt19937_64 rgen(time(NULL));
	#define rdist(x) generate_canonical<double, 64>(x)

	BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test1 " << DebugString(true);

	Xmatch match;
	match.xbuy.type = CC_TYPE_XCX_SIMPLE_BUY;
	match.xbuy.quote_asset = XREQ_BLOCKCHAIN_BCH;
	match.xbuy.flags.have_matching = true;
	match.xbuy.destination = 1UL;
	match.status = XMATCH_STATUS_PAID;

	auto t = mining_start_time;

	unsigned iter;

	for (iter = 0; saved.total_remaining_to_mine && (!log_end || iter < log_end); ++iter)
	{
		for (unsigned count = 0; saved.currently_mineable_amount.asFloat() + saved.last_nominal_mineable_amount_increase.asFloat() <= saved.max_currently_mineable_amount.asFloat() || saved.max_currently_mineable_amount <= 0; ++count)
		{
			if (iter >= log_start && !(iter % log_iter_mod) && !(count % log_count_mod)) BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test " << iter << " up " << count << " " << DebugString();

			t += saved.mining_update_time_increment;
			UpdateMiningTime(t);
		}

		if (log_start && iter == log_start)
			set_trace_level(9);

		//uint32_t max_match_amount = 5000;	// must be at least MINING_MIN_CUTOFF_AMOUNT / MINING_MULTIPLIER_MIN = 100
		uint32_t max_match_amount = -1;	// must be at least MINING_MIN_CUTOFF_AMOUNT / MINING_MULTIPLIER_MIN = 100

		for (unsigned count = 0; saved.total_remaining_to_mine && (saved.currently_mineable_amount >= saved.max_currently_mineable_amount || saved.currently_mineable_amount >= 1.00001); ++count)
		{
			if (count == 2*log_count_mod && count > 10000)
				set_trace_level(9);

			if (iter >= log_start && !(iter % log_iter_mod) && !(count % log_count_mod)) BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test " << iter << " down " << count << " " << DebugString();

			CCASSERT(count <= 2*log_count_mod || count < 10000);

			auto base_amount = max_match_amount * (double)asset_scale_factor(0); // * (rgen() & 7 ? 1 : rdist(rgen));
			CCASSERTZ(amount_from_float(0, base_amount, match.base_amount));
			match.xbuy.net_rate_required = 1 + rdist(rgen);
			match.mining_amount = 0;
			match.match_timestamp = t;

			TestUpdateMiningAmount(match);
		}
	}

	BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test1 " << iter << " done 0 " << DebugString();
}

void ExchangeMining::Test2()
{
	// cycle the mining amount and/or rate to test the decay rates
	// note if both are cycled, the rate decay will become asymertric, due to the weighting by the variable amount
	#define TEST_CYCLE_AMOUNT	1	// long decay rate
	#define TEST_CYCLE_RATE		1	// short decay rate

	//BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test2 " << DebugString(true);

	Xmatch match;
	match.xbuy.type = CC_TYPE_XCX_SIMPLE_BUY;
	match.xbuy.quote_asset = XREQ_BLOCKCHAIN_BCH;
	match.xbuy.flags.have_matching = true;
	match.xbuy.destination = 1UL;
	match.status = XMATCH_STATUS_PAID;

	auto t = mining_start_time;

	//set_trace_level(9);

	while ((saved.currently_mineable_amount < saved.max_currently_mineable_amount || saved.max_currently_mineable_amount <= 0))
	{
		t += saved.mining_update_time_increment;
		UpdateMiningTime(t);
	}

	unsigned drive_mod = 12*3600 / saved.mining_update_time_increment;
	unsigned update_mod = 1;
	unsigned log_mod = 10;

	double total = 0;
	unsigned count = 0;

	for (unsigned iter = 0; saved.mining_period < 14 * drive_mod; ++iter)
	{
		if (!(iter % update_mod))
			t += saved.mining_update_time_increment;

		//if (saved.mining_period == 200)
		//	set_trace_level(9);
		//else if (saved.mining_period > 201)
		//	break;

		auto base_amount = 1000 + 1200 * TEST_CYCLE_AMOUNT * ((saved.mining_period / drive_mod) & 1);		// cycle from 1000 to 2200 avg 1600
		CCASSERTZ(amount_from_float(0, base_amount, match.base_amount));
		match.xbuy.net_rate_required = 1 + 1 * TEST_CYCLE_RATE * ((saved.mining_period / drive_mod) & 1);	// cycle from 1 to 2 avg 1.5

		total += base_amount;
		++count;

		match.mining_amount = 0;
		match.match_timestamp = t;

		TestUpdateMiningAmount(match);

		if (!(iter & log_mod)) BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test  period " << saved.mining_period << " actual_avg_amount " << total/count << " " << DebugString();
	}

	//BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test2 done " << DebugString();
}

void ExchangeMining::Test3()
{
	// simulate avg_match_rate_required

	//BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test3 " << DebugString(true);

	const float min_mineable_fraction = 0.25;
	const float rate_fraction = 1.001;
	unsigned log_mod = 100000;

	Xmatch match;
	match.xbuy.type = CC_TYPE_XCX_SIMPLE_BUY;
	match.xbuy.quote_asset = XREQ_BLOCKCHAIN_BCH;
	match.xbuy.flags.have_matching = true;
	match.xbuy.destination = 1UL;
	match.status = XMATCH_STATUS_PAID;

	auto t = mining_start_time;

	//set_trace_level(9);

	for (unsigned iter = 0; saved.total_remaining_to_mine; ++iter)
	{
		if (!(iter % log_mod)) BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test " << DebugString();

		if ((saved.currently_mineable_amount < min_mineable_fraction * saved.max_currently_mineable_amount.asFloat() || saved.max_currently_mineable_amount <= 0))
			t += saved.mining_update_time_increment;

		//if (saved.mining_period == 200)
		//	set_trace_level(9);
		//else if (saved.mining_period > 201)
		//	break;

		auto base_amount = 1000 * (double)asset_scale_factor(0);
		CCASSERTZ(amount_from_float(0, base_amount, match.base_amount));
		match.xbuy.net_rate_required = max(1.0, rate_fraction * saved.mining_stats.avg_match_rate_required.asFloat());

		match.mining_amount = 0;
		match.match_timestamp = t;

		TestUpdateMiningAmount(match);
	}

	BOOST_LOG_TRIVIAL(info) << "ExchangeMining::Test3 " << DebugString();
}
