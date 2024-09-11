/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * xtransaction-xreq.cpp
*/

#include "cclib.h"
#include "xtransaction-xreq.hpp"
#include "transaction.h"
#include "encode.h"
#include "map_values.h"
#include "CCparams.h"

//#define TRACE_RATES			1	// for debugging
//#define TRACE_RATE_TEST		1	// for debugging
//#define TRACE_WIRE_TEST		1	// for debugging

#ifndef TRACE_RATES
#define TRACE_RATES				0	// don't debug
#endif

#ifndef TRACE_RATE_TEST
#define TRACE_RATE_TEST			0	// don't debug
#endif

#ifndef TRACE_WIRE_TEST
#define TRACE_WIRE_TEST			0	// don't debug
#endif

#define RATE_TEST_ITER			2000000
#define WIRE_TEST_ITER			200000

static exp_map_t paytime_params;

void Xreq::Init()
{
	map_exp_init_params(paytime_params, 10, 5, 255, 172800, 2786840450);

	//map_exp_test_check(paytime_params);
}

string Xreq::DebugString() const
{
	if (type && !TypeIsXreq(type))
		return Xtx::DebugString();

	ostringstream out;

	out << "Xreq";
	out << " xreqnum " << xreqnum;
	out << " seqnum " << seqnum;
	out << " linked_seqnum " << linked_seqnum;
	out << " db_search_max_xreqnum " << db_search_max_xreqnum;
	out << " type " << type << " = " << TypeString();

	//out << " amount_bits " << amount_bits;
	//out << " exponent_bits " << exponent_bits;
	//out << " amount_carry_in " << amount_carry_in;
	out << " amount_carry_out " << amount_carry_out;

	out << " IsBuyer " << IsBuyer();
	out << " objid " << buf2hex(&objid, CC_OID_TRACE_SIZE);
	out << " expire_time " << expire_time;
	out << " base_asset " << base_asset;
	out << " quote_asset " << quote_asset;
	out << " foreign_asset " << foreign_asset;
	out << " min_amount " << min_amount;
	out << " max_amount " << max_amount;
	out << " net_rate_required " << net_rate_required;
	out << " wait_discount " << wait_discount;
	out << " base_costs " << base_costs;
	out << " quote_costs " << quote_costs;
	out << " destination " << destination;
	out << " foreign_address " << foreign_address;

	out << " add_immediately_to_blockchain " << flags.add_immediately_to_blockchain;
	out << " auto_accept_matches " << flags.auto_accept_matches;
	out << " no_minimum_after_first_match " << flags.no_minimum_after_first_match;
	out << " must_liquidate_crossing_minimum " << flags.must_liquidate_crossing_minimum;
	out << " must_liquidate_below_minimum " << flags.must_liquidate_below_minimum;
	out << " has_signing_key " << flags.has_signing_key;

	out << " consideration_required " << consideration_required;
	out << " consideration_offered " << consideration_offered;
	out << " pledge " << pledge;
	out << " hold_time " << hold_time;
	out << " hold_time_required " << hold_time_required;
	out << " min_wait_time " << min_wait_time;
	out << " accept_time_required " << accept_time_required;
	out << " accept_time_offered " << accept_time_offered;
	out << " payment_time " << payment_time;
	out << " confirmations " << confirmations;

	out << " pending_match_epoch " << pending_match_epoch;
	out << " pending_match_order " << pending_match_order;
	out << " pending_match_amount " << pending_match_amount;
	out << " pending_match_rate " << pending_match_rate;
	out << " pending_match_hold_time " << pending_match_hold_time;

	out << " for_testnet " << for_testnet;
	out << " for_witness " << for_witness;
	out << " blocktime " << blocktime;
	out << " open_amount " << open_amount;
	out << " matching_amount " << matching_amount;
	out << " open_rate_required " << open_rate_required;
	out << " matching_rate_required " << matching_rate_required;
	out << " recalc_time " << recalc_time;
	out << " last_matched " << last_matched;
	out << " recalc " << recalc;
	out << " best_amount " << best_amount;
	out << " best_rate " << best_rate;
	out << " best_net_rate " << best_net_rate;
	out << " best_other_seqnum " << best_other_seqnum;
	out << " best_other_xreqnum " << best_other_xreqnum;
	out << " best_other_matching_amount " << best_other_matching_amount;
	out << " best_other_net_rate " << best_other_net_rate;

	return out.str();
}

Xreq::Xreq(const unsigned type_, const uint64_t expiration_, const bigint_t& min_amount_, const bigint_t& max_amount_, const UniFloat& req_rate_, const UniFloat& quote_costs_, const uint64_t quote_asset_, const string& foreign_asset_, const string& foreign_address_, const bool for_testnet_)
 :	Xtx(type_, expiration_),
	foreign_asset(foreign_asset_),
	foreign_address(foreign_address_)
{
	memset((void*)&destination, 0, (uintptr_t)&foreign_asset - (uintptr_t)&destination);

	for_testnet = for_testnet_;

	min_amount = min_amount_;
	max_amount = max_amount_;
	net_rate_required = req_rate_;
	quote_costs = quote_costs_;
	quote_asset = quote_asset_;

	open_amount = max_amount;
	flags.auto_accept_matches = true;
}

string Xreq::ForeignAssetString(uint64_t quote_asset, const string& foreign_asset)
{
	//cerr << "ForeignAssetString quote_asset " << quote_asset << " foreign_asset " << foreign_asset << endl;

	CCASSERT(foreign_asset.empty());

	switch (quote_asset)
	{
	case XREQ_BLOCKCHAIN_BTC:
		return XREQ_SYMBOL_BTC;
	case XREQ_BLOCKCHAIN_BCH:
		return XREQ_SYMBOL_BCH;
	}

	return foreign_asset;
}

bool Xreq::NormalizeForeignAddress()
{
	if (!TypeIsCrosschain(type) || !TypeIsSeller(type))
		return false;

	//cerr << "NormalizeForeignAddress quote_asset " << quote_asset << " foreign_address " << foreign_address << endl;

	switch (quote_asset)
	{
	case XREQ_BLOCKCHAIN_BCH:
		auto pos = foreign_address.rfind(':');
		if (pos != string::npos)
			foreign_address.erase(0, pos + 1);
		break;
	}

	//cerr << "NormalizeForeignAddress result foreign_address " << foreign_address << endl;

	return CheckForeignAddress();
}

bool Xreq::CheckForeignAddress() const
{
	if (!TypeIsCrosschain(type) || !TypeIsSeller(type))
		return false;

	auto str = foreign_address.data();
	auto len = foreign_address.length();
	unsigned start = 0;

	switch (quote_asset)
	{
	case XREQ_BLOCKCHAIN_BTC:
		if (len != 42 && len != 62)
			return true;
		start = 4;
		if (memcmp(str, "bc1q", start) && memcmp(str, "tb1q", start))
			return true;
		break;

	case XREQ_BLOCKCHAIN_BCH:
		if (len != 42)
			return true;
		start = 1;
		if (str[0] != 'p' && str[0] != 'q')
			return true;
		break;
	}

	// check base32 charset

	for (unsigned i = start; i < len; ++i)
	{
		auto c = str[i];

		if ((c < '0' || c > '9') && (c < 'a' || c > 'z'))
			return true;
		if (c == '1' || c == 'b' || c == 'i' || c == 'o')
			return true;
	}

	return false;
}

void Xreq::ConvertTradeToBuy()
{
	CCASSERT(type == CC_TYPE_XCX_MINING_TRADE);

	type = CC_TYPE_XCX_MINING_BUY;

	foreign_address.clear();
}

void Xreq::ConvertTradeToSell()
{
	CCASSERT(type == CC_TYPE_XCX_MINING_TRADE);

	type = CC_TYPE_XCX_MINING_SELL;

	seqnum = 0;
	xreqnum = 0;

	ConvertTradeObjIdToSellObjId(objid);
}

void Xreq::ConvertTradeObjIdToSellObjId(unsigned tx_type, unsigned xreq_type, ccoid_t& objid)
{
	if (tx_type == CC_TYPE_XCX_MINING_TRADE && xreq_type == CC_TYPE_XCX_MINING_SELL)
		ConvertTradeObjIdToSellObjId(objid);
}

void Xreq::ConvertTradeObjIdToSellObjId(ccoid_t& objid)
{
	objid[0] ^= 1;	// make a unique objid
}

/*

Exchange rate = value of Base Asset (CredaCash) in Quote Asset (Foreign) = Quote/Base = Foreign/CredaCash

Net_Rate[amount, rate] = Net_Quote_Asset[amount, rate] / Net_Base_Asset[amount, rate]

	where amount is in Base Asset

net_rate_required = Net_Rate required by Seller or Buyer to obtain a match

Seller: Base (CredaCash) -> Quote (Foreign)

	- Net_Base_Asset:  amount + Seller_base_costs
	+ Net_Quote_Asset: amount*rate - Seller_quote_costs

	Net_Rate = (amount*rate - quote_costs) / (amount + base_costs)
		check: Seller wants a higher rate (more Quote for less Base); quote_costs decrease rate; base_costs decrease rate

	net_rate_required = (amount*rate_required - quote_costs) / (amount + base_costs)
	amount*rate_required - quote_costs = net_rate_required * (amount + base_costs)
	rate_required = (net_rate_required * (amount + base_costs) + quote_costs) / amount

	check: higher costs drives rate_required up and makes match less likely

Buyer: Quote (Foreign) -> Base (CredaCash)

	+ Net_Base_Asset:  amount - Buyer_base_costs
	- Net_Quote_Asset: amount*rate + Buyer_quote_costs

	Net_Rate = (amount*rate + quote_costs) / (amount - base_costs)
		check: Buyer wants a lower rate (less Quote and more Base); quote_costs increase rate; base_costs increase rate

	net_rate_required = (amount*rate_required + quote_costs) / (amount - base_costs)
	amount*rate_required + quote_costs = net_rate_required * (amount - base_costs)
	rate_required = (net_rate_required * (amount - base_costs) - quote_costs) / amount

	check: higher costs drives rate_required down and makes match less likely

Note: base_costs is in units of base_asset; quote_costs is in units of quote_asset

Note: computations are done using integers to ensure they produce the same values regardless of the precision of type "double"!

*/

UniFloat Xreq::QuoteAmount(const UniFloat& base_amount, const UniFloat& rate)
{
	return UniFloat::Multiply(base_amount, rate, 1);	// round up
}

UniFloat Xreq::NetRate(const UniFloat& base_amount, const UniFloat& rate, int rounding) const
{
	// Computes NetRate for a given exchange rate
	// Note that this is the inverse function of MatchRateRequired()

	// Seller: Net_Rate = (amount*rate - quote_costs) / (amount + base_costs)
	//  Buyer: Net_Rate = (amount*rate + quote_costs) / (amount - base_costs)

	auto sign = RateSign(IsBuyer());

	auto base = UniFloat::Add(base_amount, UniFloat::ApplySign(sign, base_costs), -rounding);

	if (base == 0)
		return sign * DBL_MAX;

	auto quote = UniFloat::Multiply(base_amount, rate, rounding);

	quote = UniFloat::Add(quote, UniFloat::ApplySign(-sign, quote_costs), rounding);

	auto result = UniFloat::Divide(quote, base, rounding);

	if (TRACE_RATES) cout << "Xreq::NetRate amount " << base_amount << " rate " << rate << " returning " << result << " thread " << cc_thread_id() << " ; " << DebugString() << endl;

	return result;
}

UniFloat Xreq::MatchRateRequired(const UniFloat& base_amount, int rounding) const
{
	// Computes the match rate required to satisfy the xreq's net_rate_required.
	// Note that this is the inverse function of NetRate()

	// Seller: rate_required = (net_rate_required * (base_amount + base_costs) + quote_costs) / base_amount
	//  Buyer: rate_required = (net_rate_required * (base_amount - base_costs) - quote_costs) / base_amount


	auto sign = RateSign(IsBuyer());

	//cout << "MatchRateRequired seqnum " << seqnum << " IsBuyer " << IsBuyer() << " net_rate_required " << net_rate_required << " base_amount " << base_amount << " sign " << sign << " base_costs " << base_costs << " quote_costs " << quote_costs << endl;

	if (base_amount == 0)
		return sign * DBL_MAX;

	auto result = base_amount;

	result = UniFloat::Add(result, UniFloat::ApplySign(sign, base_costs), rounding);

	result = UniFloat::Multiply(result, net_rate_required);

	result = UniFloat::Add(result, UniFloat::ApplySign(sign, quote_costs), rounding);

	result = UniFloat::Divide(result, base_amount, rounding);

	if (TRACE_RATES) cout << "Xreq::MatchRateRequired amount " << base_amount << " returning " << result << " thread " << cc_thread_id() << " ; " << DebugString() << endl;

	return result;
}

void Xreq::DataToWire(const string& fn, void *binbuf, const uint32_t binsize, uint32_t &bufpos)
{
	const bool bhex = false;
	const unsigned amount_bytes = (amount_bits + 7) / 8;
	vector<char> encoded_sval;

	if (!TypeIsXreq(type))
		return Xtx::DataToWire(fn, binbuf, binsize, bufpos);

	if (type == CC_TYPE_XCX_MINING_BUY || type == CC_TYPE_XCX_MINING_SELL)
		throw range_error("type not serializable");

	if (!expire_time)
		throw range_error("expire_time is 0");

	if (NormalizeForeignAddress())
		throw range_error("invalid foreign_address");

	auto encoded_time = Xtx::Encode_Time(expire_time);
	copy_to_buf(encoded_time, CC_BLOCKTIME_WIRE_BYTES, bufpos, binbuf, binsize, bhex);

	//cerr << "ToWire expire_time " << expire_time << " encoded " << encoded_time << endl;

	copy_to_buf(destination, sizeof(destination), bufpos, binbuf, binsize, bhex);

	auto amount_fp = tx_amount_encode(min_amount, false, amount_bits, exponent_bits);
	copy_to_buf(amount_fp, amount_bytes, bufpos, binbuf, binsize, bhex);

	if (type != CC_TYPE_XCX_MINING_TRADE)
	{
		amount_fp = tx_amount_encode(max_amount, false, amount_bits, exponent_bits);
		copy_to_buf(amount_fp, amount_bytes, bufpos, binbuf, binsize, bhex);
	}
	else if (max_amount != min_amount)
		throw range_error("max_amount must = min_amount for a trade request");

	auto rate_fp = UniFloat::WireEncode(net_rate_required);
	copy_to_buf(rate_fp, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);

	if (type != CC_TYPE_XCX_MINING_TRADE)
	{
		rate_fp = UniFloat::WireEncode(wait_discount);
		copy_to_buf(rate_fp, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
	}
	else if (wait_discount != 1)
		throw range_error("wait_discount must be 1 for a trade request");

	if (base_asset != 0)
		throw range_error("base_asset must be 0");

	if (base_costs != 0)
		throw range_error("base_costs must be 0");

	if (quote_asset >= ((uint64_t)1 << (XCX_BLOCKCHAIN_WIRE_BYTES*8)))
		throw range_error("quote_asset exceeds limit");
	copy_to_buf(quote_asset, XCX_BLOCKCHAIN_WIRE_BYTES, bufpos, binbuf, binsize, bhex);

	if (type != CC_TYPE_XCX_MINING_TRADE)
	{
		rate_fp = UniFloat::WireEncode(quote_costs);
		copy_to_buf(rate_fp, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
	}
	else if (quote_costs != 0)
		throw range_error("quote_costs must be 0 for a trade request");

	if (((type >= CC_TYPE_XCX_NAKED_BUY && type <= CC_TYPE_XCX_SIMPLE_SELL) || type == CC_TYPE_XCX_MINING_TRADE) && !TEST_XREQ)
	{
		if (consideration_required)
			throw range_error("consideration_required non-zero");

		if (consideration_offered)
			throw range_error("consideration_offered non-zero");

		if (IsSimple())
		{
			if (pledge != XREQ_SIMPLE_PLEDGE)
				throw range_error("pledge != XREQ_SIMPLE_PLEDGE");
		}
		else if (pledge)
			throw range_error("pledge != 0");

		if (hold_time != XREQ_SIMPLE_HOLD_TIME)
			throw range_error("hold_time != XREQ_SIMPLE_HOLD_TIME");

		if (hold_time_required != XREQ_SIMPLE_HOLD_TIME)
			throw range_error("hold_time_required != XREQ_SIMPLE_HOLD_TIME");

		if (accept_time_required)
			throw range_error("accept_time_required non-zero");

		if (accept_time_offered)
			throw range_error("accept_time_offered non-zero");

		if (payment_time != DefaultPaymentTime())
			throw range_error("payment_time != XREQ_SIMPLE_PAYMENT_TIME");

		if (confirmations != DefaultConfirmations())
			throw range_error("confirmations != XREQ_SIMPLE_CONFIRMATIONS");
	}
	else
	{
		if (consideration_required > 255)
			throw range_error("consideration_required exceeds limit");
		copy_to_buf(consideration_required, 1, bufpos, binbuf, binsize, bhex);

		if (consideration_offered > 255)
			throw range_error("consideration_offered exceeds limit");
		copy_to_buf(consideration_offered, 1, bufpos, binbuf, binsize, bhex);

		if (pledge > 255)
			throw range_error("pledge exceeds limit");
		copy_to_buf(pledge, 1, bufpos, binbuf, binsize, bhex);

		if (hold_time > 255)
			throw range_error("hold_time exceeds limit");
		copy_to_buf(hold_time, 1, bufpos, binbuf, binsize, bhex);

		if (hold_time_required > 255)
			throw range_error("hold_time_required exceeds limit");
		copy_to_buf(hold_time_required, 1, bufpos, binbuf, binsize, bhex);

		if (accept_time_required > 255)
			throw range_error("accept_time_required exceeds limit");
		copy_to_buf(accept_time_required, 1, bufpos, binbuf, binsize, bhex);

		if (accept_time_offered > 255)
			throw range_error("accept_time_offered exceeds limit");
		copy_to_buf(accept_time_offered, 1, bufpos, binbuf, binsize, bhex);

		unsigned encoded_payment_time = payment_time - 1;
		if (!TEST_XREQ)
			encoded_payment_time = map_exp_encode(paytime_params, payment_time);
		if (encoded_payment_time > 255)
			throw range_error("payment_time exceeds limits");
		copy_to_buf(encoded_payment_time, 1, bufpos, binbuf, binsize, bhex);

		unsigned encoded_confirmations = confirmations - 1;
		if (encoded_confirmations >= ((uint64_t)1 << (XCX_CONFIRMATIONS_WIRE_BYTES*8)))
			throw range_error("confirmations exceeds limits");
		copy_to_buf(encoded_confirmations, XCX_CONFIRMATIONS_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
	}

	if (min_wait_time > 255)
		throw range_error("min_wait_time exceeds limit");
	if (type != CC_TYPE_XCX_MINING_TRADE)
		copy_to_buf(min_wait_time, 1, bufpos, binbuf, binsize, bhex);
	else if (min_wait_time != XREQ_SIMPLE_WAIT_TIME)
		throw range_error("min_wait_time invalid for trade request");

	unsigned packed_flags = 0;

	if (flags.add_immediately_to_blockchain > 1)
		throw range_error("flags.add_immediately_to_blockchain exceeds limit");
	packed_flags = (packed_flags << 1) | flags.add_immediately_to_blockchain;

	if (flags.auto_accept_matches > 1)
		throw range_error("flags.auto_accept_matches exceeds limit");
	packed_flags = (packed_flags << 1) | flags.auto_accept_matches;

	if (flags.no_minimum_after_first_match > 1)
		throw range_error("flags.no_minimum_after_first_match exceeds limit");
	packed_flags = (packed_flags << 1) | flags.no_minimum_after_first_match;

	if (flags.must_liquidate_crossing_minimum > 1)
		throw range_error("flags.must_liquidate_crossing_minimum exceeds limit");
	packed_flags = (packed_flags << 1) | flags.must_liquidate_crossing_minimum;

	if (flags.must_liquidate_below_minimum > 1)
		throw range_error("flags.must_liquidate_below_minimum exceeds limit");
	packed_flags = (packed_flags << 1) | flags.must_liquidate_below_minimum;

	if (flags.has_signing_key > 1)
		throw range_error("flags.has_signing_key exceeds limit");
	packed_flags = (packed_flags << 1) | flags.has_signing_key;

	copy_to_buf(packed_flags, 1, bufpos, binbuf, binsize, bhex);

	if (!TypeIsCrosschain(type))
	{
		;;;
	}
	else if (	(quote_asset == XREQ_BLOCKCHAIN_BTC && foreign_asset == XREQ_SYMBOL_BTC)
			 ||	(quote_asset == XREQ_BLOCKCHAIN_BCH && foreign_asset == XREQ_SYMBOL_BCH) )
	{
		unsigned encoded_size = 0;
		copy_to_buf(encoded_size, 1, bufpos, binbuf, binsize, bhex);
	}
	else
	{
		auto rc = cc_alpha_encode_best(foreign_asset.data(), foreign_asset.length(), encoded_sval);
		if (rc) throw range_error("failure encoding foreign_asset");
		unsigned encoded_size = encoded_sval.size();
		if (encoded_size > XTX_MAX_ITEM_SIZE)
			throw range_error("foreign_asset length exceeds limit");
		copy_to_buf(encoded_size, 1, bufpos, binbuf, binsize, bhex);
		copy_to_bufp(encoded_sval.data(), encoded_sval.size(), bufpos, binbuf, binsize, bhex);
		//cerr << "ToWire encoded_size " << encoded_size << " foreign_asset " << foreign_asset << endl;
	}

	if (TypeIsCrosschain(type) && TypeIsSeller(type))
	{
		auto rc = cc_alpha_encode_best(foreign_address.data(), foreign_address.length(), encoded_sval);
		if (rc) throw range_error("failure encoding foreign_address");
		unsigned encoded_size = encoded_sval.size() - 1;
		if (encoded_size > XTX_MAX_ITEM_SIZE)
			throw range_error("foreign_address length exceeds limit");
		copy_to_buf(encoded_size, 1, bufpos, binbuf, binsize, bhex);
		copy_to_bufp(encoded_sval.data(), encoded_sval.size(), bufpos, binbuf, binsize, bhex);
	}

	if (flags.has_signing_key)
		copy_to_buf(signing_public_key, sizeof(signing_public_key), bufpos, binbuf, binsize, bhex);

	if (type == CC_TYPE_XCX_NAKED_BUY)
	{
		uint64_t pow = 0;
		copy_to_buf(pow, sizeof(pow), bufpos, binbuf, binsize, bhex);
	}
}

void Xreq::DataFromWire(const string& fn, const void *binbuf, const uint32_t binsize, uint32_t &bufpos)
{
	const bool bhex = false;
	const unsigned amount_bytes = (amount_bits + 7) / 8;
	uint64_t encoded;
	char strbuf[288];

	if (!TypeIsXreq(type))
		return Xtx::DataFromWire(fn, binbuf, binsize, bufpos);

	if (type == CC_TYPE_XCX_MINING_BUY || type == CC_TYPE_XCX_MINING_SELL)
		throw range_error("type not serializable");

	copy_from_buf(encoded, CC_BLOCKTIME_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
	expire_time = Xtx::Decode_Time(encoded);

	//cerr << "FromWire expire_time " << expire_time << " encoded " << encoded << endl;

	copy_from_buf(destination, sizeof(destination), bufpos, binbuf, binsize, bhex);

	copy_from_buf(encoded, amount_bytes, bufpos, binbuf, binsize, bhex);
	tx_amount_decode(encoded, min_amount, false, amount_bits, exponent_bits);

	if (type != CC_TYPE_XCX_MINING_TRADE)
	{
		copy_from_buf(encoded, amount_bytes, bufpos, binbuf, binsize, bhex);
		tx_amount_decode(encoded, max_amount, false, amount_bits, exponent_bits);
	}
	else
		max_amount = min_amount;

	copy_from_buf(encoded, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
	net_rate_required = UniFloat::WireDecode(encoded);

	if (type != CC_TYPE_XCX_MINING_TRADE)
	{
		copy_from_buf(encoded, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
		wait_discount = UniFloat::WireDecode(encoded);
	}
	else
		wait_discount = 1;

	base_asset = 0;

	base_costs = 0;

	copy_from_buf(quote_asset, XCX_BLOCKCHAIN_WIRE_BYTES, bufpos, binbuf, binsize, bhex);

	if (type != CC_TYPE_XCX_MINING_TRADE)
	{
		copy_from_buf(encoded, UNIFLOAT_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
		quote_costs = UniFloat::WireDecode(encoded);
	}
	else
		quote_costs = 0;

	if (((type >= CC_TYPE_XCX_NAKED_BUY && type <= CC_TYPE_XCX_SIMPLE_SELL) || type == CC_TYPE_XCX_MINING_TRADE) && !TEST_XREQ)
	{
		consideration_required = 0;
		consideration_offered = 0;
		if (IsSimple())
			pledge = XREQ_SIMPLE_PLEDGE;
		else
			pledge = 0;
		hold_time = XREQ_SIMPLE_HOLD_TIME;
		hold_time_required = XREQ_SIMPLE_HOLD_TIME;
		accept_time_required = 0;
		accept_time_offered = 0;
		payment_time = DefaultPaymentTime();
		confirmations = DefaultConfirmations();
	}
	else
	{
		copy_from_buf(consideration_required, 1, bufpos, binbuf, binsize, bhex);

		copy_from_buf(consideration_offered, 1, bufpos, binbuf, binsize, bhex);

		copy_from_buf(pledge, 1, bufpos, binbuf, binsize, bhex);

		copy_from_buf(hold_time, 1, bufpos, binbuf, binsize, bhex);

		copy_from_buf(hold_time_required, 1, bufpos, binbuf, binsize, bhex);

		copy_from_buf(accept_time_required, 1, bufpos, binbuf, binsize, bhex);

		copy_from_buf(accept_time_offered, 1, bufpos, binbuf, binsize, bhex);

		unsigned encoded_payment_time;
		copy_from_buf(encoded_payment_time, 1, bufpos, binbuf, binsize, bhex);
		if (TEST_XREQ)
			payment_time = encoded_payment_time + 1;
		else
			payment_time = map_exp_decode(paytime_params, encoded_payment_time);

		copy_from_buf(confirmations, XCX_CONFIRMATIONS_WIRE_BYTES, bufpos, binbuf, binsize, bhex);
		confirmations += 1;
	}

	if (type != CC_TYPE_XCX_MINING_TRADE)
		copy_from_buf(min_wait_time, 1, bufpos, binbuf, binsize, bhex);
	else
		min_wait_time = XREQ_SIMPLE_WAIT_TIME;

	copy_from_buf(encoded, 1, bufpos, binbuf, binsize, bhex);

	flags.has_signing_key = encoded & 1; encoded >>= 1;
	flags.must_liquidate_below_minimum = encoded & 1; encoded >>= 1;
	flags.must_liquidate_crossing_minimum = encoded & 1; encoded >>= 1;
	flags.no_minimum_after_first_match = encoded & 1; encoded >>= 1;
	flags.auto_accept_matches = encoded & 1; encoded >>= 1;
	flags.add_immediately_to_blockchain = encoded & 1; encoded >>= 1;

	if (TypeIsCrosschain(type))
	{
		copy_from_buf(encoded, 1, bufpos, binbuf, binsize, bhex);
		copy_from_buf(strbuf, encoded, bufpos, binbuf, binsize, bhex);
		auto rc = cc_alpha_decode_best(strbuf, encoded, foreign_asset);
		if (rc) throw range_error("failure decoding foreign_asset");
	}

	if (TypeIsCrosschain(type) && TypeIsSeller(type))
	{
		copy_from_buf(encoded, 1, bufpos, binbuf, binsize, bhex);
		encoded += 1;
		copy_from_buf(strbuf, encoded, bufpos, binbuf, binsize, bhex);
		auto rc = cc_alpha_decode_best(strbuf, encoded, foreign_address);
		if (rc) throw range_error("failure decoding foreign_address");

		if (CheckForeignAddress())
			throw range_error("invalid foreign_address");
	}

	if (flags.has_signing_key)
		copy_from_buf(signing_public_key, sizeof(signing_public_key), bufpos, binbuf, binsize, bhex);

	if (type == CC_TYPE_XCX_NAKED_BUY)
	{
		uint64_t pow;
		copy_from_buf(pow, sizeof(pow), bufpos, binbuf, binsize, bhex);
	}
}

#if TEST_XREQ

static double randrate(unsigned exp_max)
{
	UniFloatTuple v;

	CCRandom(&v, sizeof(v));

	if (RandTest(8))
		v.first = (uint64_t)1 << (rand() % 70);

	auto neg = (v.second < 0);

	v.second *= (neg ? -1 : 1);

	if (0 && TRACE_WIRE_TEST) cout << "exp " << v.second << " DBL_MAX_EXP " << DBL_MAX_EXP << " exp_max " << exp_max << endl;

	v.second %= exp_max;

	v.second *= (neg ? -1 : 1);

	v.second -= 8*sizeof(v.first) - 1;

	double r = UniFloat::Recompose(v).asFloat();

	if (TRACE_WIRE_TEST) cout << "randrate " << r << endl;

	return r;
}

static long double randfloat(long double base, uint64_t exp_max)
{
	uint64_t a, b, c;

	CCRandom(&a, sizeof(a));
	CCRandom(&b, sizeof(b));
	CCRandom(&c, sizeof(c));

	if (c >> 63)
		base = 1/base;

	double r = base * (1 + a * pow((long double)2, -64) + b * pow((long double)2, -128)) * pow((long double)2, (int64_t)((c % (2*exp_max)) - exp_max));

	if (!isfinite(r))
		r = rand();

	if (TRACE_WIRE_TEST) cout << "randfloat " << r << endl;

	return r;
}

static bigint_t randamt()
{
	uint64_t a;

	CCRandom(&a, sizeof(a));

	a &= ((uint64_t)1 << (TX_AMOUNT_BITS - TX_AMOUNT_EXPONENT_BITS)) - 1;

	bigint_t r(a);

	unsigned exp = rand() % (((uint64_t)1 << TX_AMOUNT_EXPONENT_BITS) + 1);

	for (unsigned i = 0; i < exp; ++i)
		r = r * bigint_t(10UL);

	if (TRACE_WIRE_TEST) cout << "randamt " << r << endl;

	return r;
}

static string randstr()
{
	string r;
	unsigned len = 0;

	if (!RandTest(32))
		len = rand() % (XTX_MAX_ITEM_SIZE + 30);

	for (unsigned i = 0; i < len; ++i)
		r.push_back(rand() & 255);

	return r;
}

static void XtxTestRateOne(int test)
{
	//test = 6;	// for testing

	double vmin, vmax, diffmin, diffmax, v1, v2, v, r, s, diff;
	Xreq xreq;

	v1 = v2 = v = r = s = diff = 0;

	vmin = diffmin = DBL_MAX;
	vmax = diffmax = -DBL_MAX;

	for (unsigned i = 0; i < RATE_TEST_ITER; ++i)
	{
		int exp = DBL_MAX_EXP;
		int rounding = (rand() % 3) - 1;

		switch (test)
		{
		case 0: // random

			v = randrate(exp);
			r = UniFloat(v).asFloat();
			diff = (r - v)/(v ? v : 1);

			break;

		case 1: // random

			v = randfloat(1.1, exp);
			r = UniFloat(v).asFloat();
			diff = (r - v)/(v ? v : 1);

			break;

		case 2: // add

			--exp;
			v1 = randrate(exp);
			v2 = randrate(exp);
			v = v1 + v2;
			r = UniFloat::Add(v1, v2, rounding).asFloat();
			s = fmax(fabs(v1), fabs(v2));
			diff = (r - v)/(s ? s : 1);

			break;

		case 3: // average

			--exp;
			v1 = randrate(exp);
			v2 = randrate(exp);
			v = (v1 + v2) / 2;
			r = UniFloat::Average(v1, v2, rounding).asFloat();
			s = fmax(fabs(v1), fabs(v2));
			diff = (r - v)/(s ? s : 1);

			break;

		case 4: // multiply

			exp /= 2;
			v1 = randrate(exp);
			v2 = randrate(exp);
			v = v1*v2;
			r = UniFloat::Multiply(v1, v2, rounding).asFloat();
			diff = (r - v)/(v ? v : 1);

			break;

		case 5: // divide

			v1 = randrate(exp);
			v2 = randrate(exp);
			if (!v2) continue;
			v = v1/v2;
			if (!isfinite(v)) continue;
			r = UniFloat::Divide(v1, v2, rounding).asFloat();
			diff = (r - v)/(v ? v : 1);

			break;

		case 6: // power

			exp /= 120;
			v1 = randrate(exp);
			v2 = rand() % 280;
			v = pow(v1, v2);
			if (!isfinite(v) || abs(v) < DBL_MIN*2 || abs(v) > DBL_MAX/2) continue;
			r = UniFloat::Power(v1, v2).asFloat();
			diff = (r - v)/(v ? v : 1);

			break;

		case 7: // match

			exp /= 8;
			v1 = fabs(randrate(exp));	// v1 = amount
			v2 = fabs(randrate(exp));	// v2 = net_rate_required

			xreq.type = CC_TYPE_XCX_REQ_BUY + (rand() % (CC_TYPE_XCX_NAKED_SELL + 1 - CC_TYPE_XCX_REQ_BUY));
			xreq.net_rate_required = v2;
			xreq.base_costs = fabs(randrate(exp));
			xreq.quote_costs = fabs(randrate(exp));

			if (TRACE_RATE_TEST) cout << xreq.DebugString() << endl;

			if (v1 - xreq.base_costs.asFloat() < 1)
				continue;

			if (xreq.net_rate_required.asFloat() * (v1 - xreq.base_costs.asFloat()) - xreq.quote_costs.asFloat() < 1)
				continue;

			// Match_Rate[trade_amount] = trade_rate where Effective_Rate[trade_amount, trade_rate] = rate_required

			v = xreq.MatchRateRequired(UniFloat(v1)).asFloat();
			r = xreq.NetRate(UniFloat(v1), v).asFloat();
			s = fmax(fabs(r), fabs(v2));

			diff = (r - v2)/(s ? s : 1);

			if (TRACE_RATE_TEST) cout << "Match Test amount " << v1 << " MatchRateRequired " << v << " NetRate " << r << " diff " << diff << " xreq type " << xreq.type << " net_rate_required " << xreq.net_rate_required << " base_costs " << xreq.base_costs << " quote_costs " << xreq.quote_costs << endl;

			break;

		default:
			return;
		}

		if (TRACE_RATE_TEST) cout << "v1 " << v1 << " v2 " << v2 << " v " << v << " r " << r << " diff " << diff << endl;

		vmin = fmin(vmin, v);
		vmax = fmax(vmax, v);

		diffmin = fmin(diffmin, diff);
		diffmax = fmax(diffmax, diff);
	}

	cerr << "Case " << test << " DBL_MAX " << DBL_MAX << " vmin " << vmin << " vmax " << vmax << " diffmin " << diffmin << " diffmax " << diffmax << endl;
}

void XtxTestRate()
{
	cerr << "XtxTestRate" << endl;

	cerr << "(int)(0.9) = " << (int)(0.9) << endl;
	cerr << "(int)(-0.9) = " << (int)(-0.9) << endl;

	for (int i = -1; i > -5; --i)
		cerr << i << " / 2 = " << (i / 2) << endl;
	for (int i = -1; i > -5; --i)
		cerr << i << " >> 1 = " << (i >> 1) << endl;
	for (int i = -1; i > -5; --i)
		cerr << i << " / 4 = " << (i / 4) << endl;
	for (int i = -1; i > -5; --i)
		cerr << i << " >> 2 = " << (i >> 2) << endl;

	UniFloat::Recompose(UniFloat::Decompose(DBL_MAX/2));
	UniFloat::Recompose(UniFloat::Decompose(-DBL_MAX/2));

	for (int i = 0; i < 10; ++i)
		XtxTestRateOne(i);

	cerr << "XtxTestRate done\n" << endl;
}

void XreqTestWire()
{
	cerr << "XreqTestWire" << endl;

	Xreq test(CC_TYPE_XCX_REQ_BUY, 0, 0UL, 0UL, 1, 0, 0, "", "x", 0, 0UL);

	test.amount_bits = TX_AMOUNT_BITS;
	test.exponent_bits = TX_AMOUNT_EXPONENT_BITS;
	test.expire_time = unixtime();

	test.TestWire();

	cerr << "XreqTestWire done\n" << endl;
}

void Xreq::TestWire()
{
	const long double minrate = 0;	// Xtx::WireDecode(0, 0, false);
	const long double maxrate = UniFloat::WireDecode(((uint64_t)1 << (UNIFLOAT_WIRE_BYTES*8)) - 1).asFloat();

	unsigned good = 0;
	long double ratemin = 1e300;
	long double ratemax = -1e300;
	long double ratediffmin = 1e300;
	long double ratediffmax = -1e300;

	net_rate_required = 0;
	TryTestWireOne(minrate, maxrate, good, ratediffmin, ratediffmax);

	net_rate_required = minrate;
	TryTestWireOne(minrate, maxrate, good, ratediffmin, ratediffmax);

	net_rate_required = maxrate;
	TryTestWireOne(minrate, maxrate, good, ratediffmin, ratediffmax);

	for (unsigned i = 0; i < WIRE_TEST_ITER; ++i)
	{
		Randomize();

		if (!RandTest(64))
			type = CC_TYPE_XCX_REQ_BUY + (rand() % (CC_TYPE_XCX_NAKED_SELL + 1 - CC_TYPE_XCX_REQ_BUY));
		else
			type = rand() & 31;

		if (!RandTest(32))
			type = CC_TYPE_XCX_SIMPLE_BUY + (rand() % (CC_TYPE_XCX_NAKED_SELL + 1 - CC_TYPE_XCX_SIMPLE_BUY));

		amount_bits = TX_AMOUNT_BITS;
		exponent_bits = TX_AMOUNT_EXPONENT_BITS;

		expire_time = !RandTest(32) * (unixtime() + (rand() % 10000));

		min_amount = randamt();
		max_amount = randamt();

		net_rate_required = randrate(DBL_MAX_EXP);

		ratemin = fmin(ratemin, net_rate_required.asFloat());
		ratemax = fmax(ratemax, net_rate_required.asFloat());

		wait_discount = randrate(DBL_MAX_EXP);

		quote_costs = randrate(DBL_MAX_EXP);

		base_costs = RandTest(32);

		base_asset = RandTest(32);

		quote_asset %= ((uint64_t)1 << (XCX_BLOCKCHAIN_WIRE_BYTES*8)) + 1000;

		consideration_required %= 258;
		consideration_offered %= 258;
		pledge %= 258;
		hold_time %= 258;
		hold_time_required %= 258;
		min_wait_time %= 258;
		accept_time_required %= 258;
		accept_time_offered %= 258;

		if (IsNaked())
		{
			consideration_required %= 2;
			consideration_offered %= 2;
			pledge %= 2;
			hold_time %= 2;
			hold_time_required %= 2;
			min_wait_time %= 2;
			accept_time_required %= 2;
			accept_time_offered %= 2;
		}

		payment_time %= 258;
		confirmations %= 258;

		flags.add_immediately_to_blockchain = RandTest(2) + RandTest(32);
		flags.auto_accept_matches = RandTest(2) + RandTest(32);
		flags.no_minimum_after_first_match = RandTest(2) + RandTest(32);
		flags.must_liquidate_crossing_minimum = RandTest(2) + RandTest(32);
		flags.must_liquidate_below_minimum = RandTest(2) + RandTest(32);
		flags.has_signing_key = RandTest(2) + RandTest(32);

		foreign_asset = randstr();
		foreign_address = randstr();

		TryTestWireOne(minrate, maxrate, good, ratediffmin, ratediffmax);
	}

	cerr << "good " << good << endl;
	cerr << "minrate " << minrate << " maxrate " << maxrate << endl;
	cerr << "ratemin " << ratemin << " ratemax " << ratemax << endl;
	cerr << "ratediffmin " << ratediffmin << " ratediffmax " << ratediffmax << endl;
}

void Xreq::TryTestWireOne(const long double& minrate, const long double& maxrate, unsigned &count, long double &ratediffmin, long double &ratediffmax) const
{
	const bigint_t maxamt("343597383680000000000000000000000000000000");

	bool indet = false;
	bool good = false;
	int bad = false;

	if (!TypeIsXreq(type))
		bad = 1;

	if (!TypeIsNaked(type) && !TypeIsSimple(type))	// only these currently work in CCObject::TypeToWireTag
		bad = 1;

	if (!expire_time)
		bad = 2;

	if (amount_bits != TX_AMOUNT_BITS)
		bad = -1;	// shouldn't happen

	if (exponent_bits != TX_AMOUNT_EXPONENT_BITS)
		bad = -2;	// shouldn't happen

	if (min_amount > maxamt)
		bad = 3;

	if (max_amount > maxamt)
		bad = 4;

	if (base_costs != 0)
		bad = 5;

	if (base_asset != 0)
		bad = 6;

	if (quote_asset >= ((uint64_t)1 << (XCX_BLOCKCHAIN_WIRE_BYTES*8)))
		bad = 7;

	if (consideration_required > 255)
		bad = 8;

	if (consideration_offered > 255)
		bad = 9;

	if (pledge > 255)
		bad = 10;

	if (hold_time > 255)
		bad = 11;

	if (hold_time_required > 255)
		bad = 12;

	if (min_wait_time > 255)
		bad = 13;

	if (accept_time_required > 255)
		bad = 14;

	if (accept_time_offered > 255)
		bad = 15;

	if (IsNaked() && consideration_required)
		bad = 16;

	if (IsNaked() && consideration_offered)
		bad = 17;

	if (IsNaked() && pledge)
		bad = 18;

	if (IsNaked() && hold_time)
		bad = 19;

	if (IsNaked() && hold_time_required)
		bad = 20;

	if (IsNaked() && min_wait_time)
		bad = 21;

	if (IsNaked() && accept_time_required)
		bad = 22;

	if (IsNaked() && accept_time_offered)
		bad = 23;

	if (payment_time < 1 || payment_time > 256)
		bad = 24;

	if (confirmations < 1 || confirmations > 256)
		bad = 25;

	if (flags.add_immediately_to_blockchain > 1)
		bad = 26;

	if (flags.auto_accept_matches > 1)
		bad = 27;

	if (flags.no_minimum_after_first_match > 1)
		bad = 28;

	if (flags.must_liquidate_crossing_minimum > 1)
		bad = 29;

	if (flags.must_liquidate_below_minimum > 1)
		bad = 30;

	if (flags.has_signing_key > 1)
		bad = 31;

	if (foreign_asset.length() > XTX_MAX_ITEM_SIZE)
		bad = 32;

	if (IsSeller() && (foreign_address.length() < 1 || foreign_address.length() > 1 + XTX_MAX_ITEM_SIZE))
		bad = 33;

	if (foreign_asset.length() > XTX_MAX_ITEM_SIZE - 20 && foreign_asset.length() < XTX_MAX_ITEM_SIZE + 20)
		indet = true;

	if (IsSeller() && foreign_address.length() > XTX_MAX_ITEM_SIZE - 20 && foreign_address.length() < XTX_MAX_ITEM_SIZE + 20)
		indet = true;

	if (!bad && !indet && net_rate_required >= minrate && net_rate_required <= maxrate && wait_discount >= 0 && wait_discount <= maxrate && quote_costs >= 0 && quote_costs <= maxrate)
		good = true;

	//cout << "bad " << bad << " good " << good << endl;

	int rc;

	try
	{
		rc = TestWireOne(good && !indet, ratediffmin, ratediffmax);
	}
	catch (const exception& e)
	{
		cout << "exception: " << e.what() << endl;
		rc = -1;
	}

	cout << "bad " << bad << " good " << good << " indet " << indet << " rc " << rc << endl;

	if (bad && !indet) CCASSERT(rc);
	if (good && !indet) CCASSERTZ(rc);

	if (rc)
	{
		//cout << "rc " << rc << " good " << good << " minrate " << minrate << " net_rate_required " << net_rate_required << " wait_discount " << wait_discount << " quote_costs " << quote_costs << " maxrate " << maxrate << endl;

		CCASSERTZ(good && !indet);
	}
	else
	{
		CCASSERTZ(bad && !indet);

		++count;
	}
}

CCRESULT Xreq::TestWireOne(bool good, long double &ratediffmin, long double &ratediffmax) const
{
	char binbuf[1000];
	uint32_t bufpos;

	Xreq test;
	test.Randomize();

	// must be set for unpack to work
	test.type = type;
	test.amount_bits = amount_bits;
	test.exponent_bits = exponent_bits;

	if (TRACE_WIRE_TEST) cout << "random test" << test.DebugString() << endl;
	if (TRACE_WIRE_TEST) cout << "ToWire test" << DebugString() << endl;

	auto rc = ToWire("TestWireOne", binbuf, sizeof(binbuf), bufpos);
	if (rc) return rc;

	rc = test.FromWire("TestWireOne", 1, binbuf, bufpos);
	if (rc) return rc;

	// these are rounded

	auto test_limit = pow((double)2, -(1 << (UNIFLOAT_EXPONENT_BITS - 2)));

	if (good && net_rate_required > test_limit)
	{
		auto diff = 1 - test.net_rate_required.asFloat() / net_rate_required.asFloat();

		if (TRACE_WIRE_TEST) cout << "net_rate_required " << net_rate_required << " --> " << test.net_rate_required << " diff " << diff << endl;

		ratediffmin = fmin(ratediffmin, diff);
		ratediffmax = fmax(ratediffmax, diff);
	}

	if (good && wait_discount > test_limit)
	{
		auto diff = 1 - test.wait_discount.asFloat() / wait_discount.asFloat();

		if (TRACE_WIRE_TEST) cout << "wait_discount " << wait_discount << " --> " << test.wait_discount << " diff " << diff << endl;

		ratediffmin = fmin(ratediffmin, diff);
		ratediffmax = fmax(ratediffmax, diff);
	}

	if (good && quote_costs > test_limit)
	{
		auto diff = 1 - test.quote_costs.asFloat() / quote_costs.asFloat();

		if (TRACE_WIRE_TEST) cout << "quote_costs " << quote_costs << " --> " << test.quote_costs << " diff " << diff << endl;

		ratediffmin = fmin(ratediffmin, diff);
		ratediffmax = fmax(ratediffmax, diff);
	}

	test.net_rate_required = net_rate_required;
	test.wait_discount = wait_discount;
	test.quote_costs = quote_costs;

	// these are not included on the wire
	test.expiration = expiration;
	test.db_search_max = db_search_max;
	test.amount_carry_in = amount_carry_in;
	test.amount_carry_out = amount_carry_out;
	test.open_amount = open_amount;
	test.matching_amount = matching_amount;
	test.best_amount = best_amount;
	test.best_other_matching_amount = best_other_matching_amount;
	test.objid = objid;
	test.seqnum = seqnum;
	test.xreqnum = xreqnum;
	test.db_search_max_xreqnum = db_search_max_xreqnum
	test.best_other_seqnum = best_other_seqnum;
	test.best_other_xreqnum = best_other_xreqnum;
	test.open_rate_required = open_rate_required;
	test.matching_rate_required = matching_rate_required;
	test.best_rate = best_rate;
	test.best_net_rate = best_net_rate;
	test.best_other_net_rate = best_other_net_rate;
	test.recalc = recalc;
	test.recalc_time = recalc_time;
	test.last_matched = last_matched;
	test.for_witness = for_witness;

	memcpy(test.signing_private_key, signing_private_key, sizeof(signing_private_key));

	if (!test.flags.has_signing_key)
		memcpy(test.signing_public_key, signing_public_key, sizeof(signing_public_key));

	// compare structs

	rc = memcmp(&test.type, &this->type, sizeof(db_search_max) + (uintptr_t)&db_search_max - (uintptr_t)&type);
	if (rc)
	{
		cout << buf2hex(&test.type,  sizeof(db_search_max) + (uintptr_t)&db_search_max - (uintptr_t)&type) << endl;
		cout << buf2hex(&this->type, sizeof(db_search_max) + (uintptr_t)&db_search_max - (uintptr_t)&type) << endl;
	}
	CCASSERTZ(rc);

	rc = memcmp(&test.destination, &this->destination, (uintptr_t)&foreign_asset - (uintptr_t)&destination);
	if (rc)
	{
		cout << buf2hex(&test.destination,  (uintptr_t)&foreign_asset - (uintptr_t)&destination) << endl;
		cout << buf2hex(&this->destination, (uintptr_t)&foreign_asset - (uintptr_t)&destination) << endl;
	}
	CCASSERTZ(rc);

	CCASSERT(test.foreign_asset == foreign_asset);

	if (IsSeller())
		CCASSERT(test.foreign_address == foreign_address);

	return 0;
}

void XcxTest()
{
	XtxTestRate();

	XreqTestWire();	// !!!!! retest with more good
}

#endif // TEST_XREQ
