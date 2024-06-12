/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * xtransaction-xreq.hpp
*/

// The Xreq class represents exchange trade requests.
// It is currently used by:
//		The wallet: to construct exchange request messages
//		The nodes: to deserialize exchange request messages, and to match requests.

#pragma once

#include "xtransaction.hpp"

#include <unifloat.hpp>
#include <ed25519/ed25519.h>

#define XREQ_BLOCKCHAIN_BTC		1
#define XREQ_BLOCKCHAIN_BCH		2
#define XREQ_BLOCKCHAIN_MAX		XREQ_BLOCKCHAIN_BCH

#define XREQ_SYMBOL_BTC			"btc"
#define XREQ_SYMBOL_BCH			"bch"

#define XREQ_SIMPLE_PLEDGE			50

#define XREQ_SIMPLE_HOLD_TIME		60
#define XREQ_MIN_POSTHOLD_TIME		60			// minimum matching time after hold and before expiration
#define XREQ_SIMPLE_WAIT_TIME		60
#define XREQ_MAX_EXPIRE_TIME		(2*60*60)

#define XREQ_MAINNET_CONFIRMATIONS	12
#define XREQ_TESTNET_CONFIRMATIONS	2
#define XREQ_MAINNET_PAYMENT_TIME	((60 * XREQ_MAINNET_CONFIRMATIONS + 0) * 60)
#define XREQ_TESTNET_PAYMENT_TIME	((30 * XREQ_TESTNET_CONFIRMATIONS + 30) * 60)
//#define XREQ_TESTNET_PAYMENT_TIME	(5*60)		// for regnet testing

#define XCX_CONFIRMATIONS_WIRE_BYTES	1
#define XCX_BLOCKCHAIN_WIRE_BYTES		(16/8)

#define XCX_MATCHING_SECS_PER_EPOCH		20

#define XREQ_WAIT_DISCOUNT_INTERVAL		XCX_MATCHING_SECS_PER_EPOCH	// compounding interval (in seconds) for wait discount

#define XREQ_RECALC_NOT		0	// do not set Xreq recalc flag
#define XREQ_RECALC_NEXT	1	// set Xreq recalc flag at start of next matching round

#if TEST_XREQ
void XcxTest();
#pragma pack(push, 1)
#endif

class Xreq : public Xtx
{
public:
	static void Init();

	bigint_t	destination;
	bigint_t	min_amount;				// units of base_asset
	bigint_t	max_amount;				// units of base_asset
	bigint_t	open_amount;			// units of base_asset
	bigint_t	matching_amount;		// units of base_asset
	bigint_t	best_amount;			// units of base_asset
	bigint_t	best_other_matching_amount;	// units of base_asset
	bigint_t	pending_match_amount;	 // amount of pending match

	ed25519_secret_key	signing_private_key;
	ed25519_public_key	signing_public_key;

	ccoid_t		objid;

	uint64_t	address_id;				// used by wallet

	int64_t		seqnum;
	int64_t		best_other_seqnum;

	uint64_t	blocktime;
	uint64_t	xreqnum;
	uint64_t	db_search_max_xreqnum;
	uint64_t	recalc_time;			// one of XREQ_RECALC_* values; otherwise blocktime to recalc Xreq's matches
	uint64_t	last_matched;
	uint64_t	best_other_xreqnum;
	uint64_t	pending_match_epoch;
	uint64_t	pending_match_order;

	uint64_t	base_asset;				// for now, this is always zero = CredaCash
	uint64_t	quote_asset;			// for XCX, this is the foreign blockchain id

	UniFloat	net_rate_required;		// Buyer's max; Seller's min
	UniFloat	wait_discount;
	UniFloat	base_costs;				// units of base_asset
	UniFloat	quote_costs;			// units of quote_asset

	UniFloat	open_rate_required;		// rate required at open_amount
	UniFloat	matching_rate_required;	// rate required at matching_amount
	UniFloat	best_rate;
	UniFloat	best_net_rate;
	UniFloat	best_other_net_rate;

	UniFloat	pending_match_rate;		// exchange rate of pending match
	uint16_t	pending_match_hold_time; // hold time of pending match

	uint16_t	for_testnet;			// flags if this object is for a testnet
	uint16_t	for_witness;			// flags if this object is for witness calculations
	uint16_t	recalc;					// flag to recompute the best_xxx values

	uint16_t	consideration_required;	// units = 1% of base amount
	uint16_t	consideration_offered;	// units = 1% of base amount
	uint16_t	pledge;					// required by XCX Seller; offered by XCX Buyer; units = 1% of base amount
	uint16_t	hold_time;
	uint16_t	hold_time_required;
	uint16_t	min_wait_time;
	uint16_t	accept_time_required;
	uint16_t	accept_time_offered;
	uint16_t	payment_time;			// required by XCX Seller; offered by XCX Buyer
	uint16_t	confirmations;			// required by XCX Seller; offered by XCX Buyer

	struct
	{
		uint16_t	add_immediately_to_blockchain;
		uint16_t	auto_accept_matches;
		uint16_t	no_minimum_after_first_match;
		uint16_t	must_liquidate_crossing_minimum;
		uint16_t	must_liquidate_below_minimum;
		uint16_t	has_signing_key;
	} flags;

	bool		changed;

	string		foreign_asset;
	string		foreign_address;

	~Xreq() = default;

	string DebugString() const;

	void Clear(bool top_only = false)
	{
		if (!top_only)
			Xtx::Clear();

		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wunknown-pragmas"
		#pragma GCC diagnostic ignored "-Warray-bounds"

		memset((void*)&destination, 0, (uintptr_t)&foreign_asset - (uintptr_t)&destination);

		#pragma GCC diagnostic pop

		foreign_asset.clear();
		foreign_address.clear();
	}

	void Copy(const Xreq& other, bool top_only = false)
	{
		if (!top_only)
			Xtx::Copy(other);

		memcpy((void*)&destination, &other.destination, (uintptr_t)&foreign_asset - (uintptr_t)&destination);

		foreign_asset = other.foreign_asset;
		foreign_address = other.foreign_address;
	}

	Xreq* Clone() const
	{
		return new Xreq(*this);
	}

	Xreq(const Xreq& other)
		: Xtx(other)
	{
		Copy(other, true);
	}

	Xreq(const unsigned type_ = 0, const bool for_testnet_ = false)
		: Xtx(type_)
	{
		Clear(true);

		for_testnet = for_testnet_;
	}

	Xreq(const unsigned type, const unsigned expiration, const bigint_t& min_amount, const bigint_t& max_amount, const UniFloat& net_rate_required, const UniFloat& quote_costs, const uint64_t quote_asset, const string& foreign_asset, const string& foreign_address, const bool for_testnet);

	static Xreq* Cast(Xtx* p)
	{
		return dynamic_cast<Xreq*>(p);
	}

	static Xreq* Cast(shared_ptr<Xtx>& p)
	{
		return Cast(p.get());
	}

#if TEST_XREQ
	void Randomize()
	{
		Xtx::Randomize();
		CCRandom(&destination, (uintptr_t)&foreign_asset - (uintptr_t)&destination);
		foreign_asset = "x";
		foreign_address = "x";
		CCRandom(&foreign_asset[0], 1);
		CCRandom(&foreign_address[0], 1);
	}
#endif

	bool NormalizeForeignAddress();
	bool CheckForeignAddress() const;

	static unsigned DefaultPaymentTime(bool istestnet)
	{
		return (istestnet ? XREQ_TESTNET_PAYMENT_TIME : XREQ_MAINNET_PAYMENT_TIME);
	}

	static unsigned DefaultConfirmations(bool istestnet)
	{
		return (istestnet ? XREQ_TESTNET_CONFIRMATIONS : XREQ_MAINNET_CONFIRMATIONS);
	}

	unsigned DefaultPaymentTime() const
	{
		return DefaultPaymentTime(for_testnet);
	}

	unsigned DefaultConfirmations() const
	{
		return DefaultConfirmations(for_testnet);
	}

	static string ForeignAssetString(uint64_t quote_asset, const string& foreign_asset);

	static int RateSign(bool isbuyer)
	{
		return (isbuyer ? -1 : 1);
	}

	static UniFloat SignedRate(bool isbuyer, const UniFloat& rate)
	{
		return (isbuyer ? -rate : rate);
	}

	static UniFloat SignedRate(unsigned type, const UniFloat& rate)
	{
		return SignedRate(TypeIsBuyer(type), rate);
	}

	UniFloat SignedRate(const UniFloat& rate) const
	{
		return SignedRate(type, rate);
	}

	UniFloat BaseAmountAsUniFloat(const bigint_t& base_amount) const
	{
		return asUniFloat(base_asset, base_amount);
	}

	static UniFloat QuoteAmount(const UniFloat& base_amount, const UniFloat& rate);

	UniFloat NetRate(const UniFloat& base_amount, const UniFloat& rate, int rounding = 0) const;
	UniFloat MatchRateRequired(const UniFloat& base_amount, int rounding = 0) const;

	UniFloat NetRate(const bigint_t& base_amount, const UniFloat& rate, int rounding = 0) const
	{
		return NetRate(BaseAmountAsUniFloat(base_amount), rate, rounding);
	}

	UniFloat MatchRateRequired(const bigint_t& base_amount, int rounding = 0) const
	{
		return MatchRateRequired(BaseAmountAsUniFloat(base_amount), rounding);
	}

	void DataToWire(const string& fn, void *binbuf, const uint32_t binsize, uint32_t &bufpos);
	void DataFromWire(const string& fn, const void *binbuf, const uint32_t binsize, uint32_t &bufpos);

	#if TEST_XREQ
	void TestWire();
	void TryTestWireOne(const long double& minrate, const long double& maxrate, unsigned &good, long double &ratediffmin, long double &ratediffmax) const;
	CCRESULT TestWireOne(bool good, long double &ratediffmin, long double &ratediffmax) const;
	#endif
};

#if TEST_XREQ
#pragma pack(pop)
#endif
