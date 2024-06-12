/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * xmatch.cpp
*/

#include "cclib.h"
#include "xtransaction-xreq.hpp"
#include "xmatch.hpp"

#define FOREIGN_PRECISION	8	// currently hard-coded for BTC

string Xmatchreq::DebugString() const
{
	// Note: Exchange_Match_Reqs table has 25 columns:
	//		Xreqnum integer
	//		Disposition integer, ExpireTime integer
	//		ObjId blob, Type integer
	//		BaseAsset integer, QuoteAsset integer, ForeignAsset string
	//		MinAmount integer, MaxAmount integer
	//		NetRateRequired float, WaitDiscount float, BaseCosts float, QuoteCosts float
	//		PackedFlags integer
	//		ConsiderationRequired integer, ConsiderationOffered integer, Pledge integer
	//		HoldTime integer, HoldTimeRequired integer, MinWaitTime integer
	//		AcceptTimeRequired integer, AcceptTimeOffered integer
	//		PaymentTime integer, Confirmations integer
	//
	// Exchange_Matching_Reqs table has 6 columns:
	//		Xreqnum integer
	//		DeleteTime integer
	//		ForeignAddress blob, Destination blob, OpenAmount blob, PubSigningKey blob
	// Plus 4 fields when PackedFlags is expanded into 5 fields:
	//		AddImmediatelyToBlockchain integer, AutoAcceptMatches integer, NoMinimumAfterFirstMatch integer, MustLiquidateCrossingMinimum integer, MustLiquidateBelowMinimum integer
	// Plus 1 field (has_signing_key) generated from PubSigningKey
	//
	// Minus these 2 columns:
	//		2*Xreqnum is redundant with Exchange_Matches table
	//
	// Plus 2 columns from the Exchange_Matches table:
	//		1: BuyXreqnum integer, SellXreqnum integer
	//		1: BuyerConsideration integer, SellerConsideration integer
	//
	// Exchange_Requests table in the wallet has 35 columns:
	//		XId integer
	//		TxId integer, AddressId integer, Xreqnum integer, QueryXmatchnum integer
	//		Disposition integer, ExpireTime integer, PollTime integer
	//		BaseAsset integer, QuoteAsset integer, ForeignAsset string
	//		MinAmount blob, MaxAmount blob
	//		NetRateRequired float, WaitDiscount float, BaseCosts float, QuoteCosts float
	//		AddImmediatelyToBlockchain integer, AutoAcceptMatches integer, NoMinimumAfterFirstMatch integer, MustLiquidateCrossingMinimum integer, MustLiquidateBelowMinimum integer
	//		ConsiderationRequired integer, ConsiderationOffered integer, Pledge integer
	//		HoldTime integer, HoldTimeRequired integer, MinWaitTime integer
	//		AcceptTimeRequired integer, AcceptTimeOffered integer
	//		PaymentTime integer, Confirmations integer
	//		ForeignAddress blob
	//		PrivateSigningKey blob
	//		OpenAmount blob
	// of these, the following 6 are unique:
	//		XId integer
	//		TxId integer
	//		AddressId integer
	//		QueryXmatchnum integer
	//		PollTime integer
	//		PrivateSigningKey
	// Minus 1 for union of PubSigningKey and PrivateSigningKey
	// Plus 1 for has_signing_key
	//
	// --> 25+6+4+1-2+2+6-1+1 = 42 columns in the Xmatchreq struct

	ostringstream out;

	out << "Xmatchreq";
	out << " id " << id;
	out << " tx_id " << tx_id;
	out << " address_id " << address_id;
	out << " xreqnum " << xreqnum;
	out << " query_matchnum " << query_matchnum;
	out << " poll_time " << poll_time;
	out << " expire_time " << expire_time;
	out << " delete_time " << delete_time;
	out << " disposition " << disposition << " = " << DispositionString();
	out << " objid " << buf2hex(&objid, CC_OID_TRACE_SIZE);
	out << " type " << type << " = " << Xtx::TypeString(type);
	out << " base_asset " << base_asset;
	out << " quote_asset " << quote_asset;
	out << " foreign_asset " << foreign_asset;
	out << " min_amount " << min_amount;
	out << " max_amount " << max_amount;
	out << " net_rate_required " << net_rate_required;
	out << " wait_discount " << wait_discount;
	out << " base_costs " << base_costs;
	out << " quote_costs " << quote_costs;
	out << " consideration_required " << consideration_required;
	out << " consideration_offered " << consideration_offered;
	out << " match_consideration " << match_consideration;
	out << " pledge " << pledge;
	out << " hold_time " << hold_time;
	out << " hold_time_required " << hold_time_required;
	out << " min_wait_time " << min_wait_time;
	out << " accept_time_required " << accept_time_required;
	out << " accept_time_offered " << accept_time_offered;
	out << " payment_time " << payment_time;
	out << " confirmations " << confirmations;
	out << " have_matching " << flags.have_matching;
	out << " add_immediately_to_blockchain " << flags.add_immediately_to_blockchain;
	out << " auto_accept_matches " << flags.auto_accept_matches;
	out << " no_minimum_after_first_match " << flags.no_minimum_after_first_match;
	out << " must_liquidate_crossing_minimum " << flags.must_liquidate_crossing_minimum;
	out << " must_liquidate_below_minimum " << flags.must_liquidate_below_minimum;
	out << " foreign_address " << foreign_address;
	if (flags.have_matching)
		out << " destination " << buf2hex(&destination, sizeof(destination));
	out << " open_amount " << open_amount;
	out << " has_signing_key " << flags.has_signing_key;
	if (flags.has_signing_key)
		out << " signing_public_key " << buf2hex(&signing_public_key, 12);

	return out.str();
}

void Xmatchreq::Init(const Xreq& xreq, const Xreq& other, const bigint_t& match_amount, uint64_t _tx_id)
{
	if (xreq.xreqnum != other.xreqnum)
	{
		CCASSERT(xreq.best_other_seqnum == other.seqnum);
		CCASSERT(xreq.consideration_offered >= other.consideration_required);
		CCASSERT(xreq.accept_time_required <= other.accept_time_offered);
	}

	id = 0;
	tx_id = _tx_id;
	address_id = xreq.address_id;
	query_matchnum = 0;
	poll_time = 0;
	expire_time = xreq.expire_time;
	delete_time = xreq.expire_time + xreq.payment_time;
	xreqnum = xreq.xreqnum;
	// disposition set below
	objid = xreq.objid;
	type = xreq.type;
	base_asset = xreq.base_asset;
	quote_asset = xreq.quote_asset;
	foreign_asset = xreq.foreign_asset;
	min_amount = xreq.min_amount;
	max_amount = xreq.max_amount;
	net_rate_required = xreq.net_rate_required;
	wait_discount = xreq.wait_discount;
	base_costs = xreq.base_costs;
	quote_costs = xreq.quote_costs;
	consideration_required = xreq.consideration_required;
	consideration_offered = xreq.consideration_offered;
	match_consideration = other.consideration_required;	// party's net consideration for the match
	pledge = xreq.pledge;								// match pledge is xbuy.pledge
	hold_time = xreq.hold_time;
	hold_time_required = xreq.hold_time_required;
	min_wait_time = xreq.min_wait_time;
	accept_time_required = xreq.accept_time_required;
	accept_time_offered = xreq.accept_time_offered;
	payment_time = xreq.payment_time;					// match payment_time is xsell.payment_time
	confirmations = xreq.confirmations;					// match confirmations is xsell.confirmations
	flags.have_matching = true;
	flags.add_immediately_to_blockchain = xreq.flags.add_immediately_to_blockchain;
	flags.auto_accept_matches = xreq.flags.auto_accept_matches;
	flags.no_minimum_after_first_match = xreq.flags.no_minimum_after_first_match;
	flags.must_liquidate_crossing_minimum = xreq.flags.must_liquidate_crossing_minimum;
	flags.must_liquidate_below_minimum = xreq.flags.must_liquidate_below_minimum;
	foreign_address = xreq.foreign_address;
	destination = xreq.destination;
	open_amount = xreq.open_amount;
	flags.has_signing_key = xreq.flags.has_signing_key;
	if (tx_id)
		memcpy(&signing_private_key, &xreq.signing_private_key, sizeof(signing_private_key));
	else
		memcpy(&signing_public_key, &xreq.signing_public_key, sizeof(signing_public_key));

	if (!open_amount && !xreq.max_amount)
		disposition = XMATCH_REQ_DISPOSITION_EXPIRED_ALL;
	else if (open_amount == xreq.max_amount)
		disposition = XMATCH_REQ_DISPOSITION_OPEN_ALL;
	else if (open_amount && xreq.foreign_address.length())
		disposition = XMATCH_REQ_DISPOSITION_MATCHED_PART;
	else if (open_amount)
		disposition = XMATCH_REQ_DISPOSITION_OPEN_PART;
	else
		disposition = XMATCH_REQ_DISPOSITION_MATCHED_ALL;
}

string Xmatchreq::DispositionString(unsigned disposition)
{
	static const char *dispositionstr[XMATCH_REQ_DISPOSITION_INVALID + 1] =
	{
		"VOID",
		"Cancelled All", "Cancelled Remainder", "Expired All", "Expired Remainder",
		"Open All", "Open Remainder", "Closed Matched Part", "Closed Matched All",
		"INVALID"
	};

	if (disposition > XMATCH_REQ_DISPOSITION_INVALID)
		disposition = XMATCH_REQ_DISPOSITION_INVALID;

	return dispositionstr[disposition];
}

string Xmatch::StatusString(unsigned status)
{
	static const char *statusstr[XMATCH_STATUS_INVALID + 1] =
	{
		"VOID",
		"Buyer Cancelled", "Seller Cancelled",
		"Matched", "Buyer Accepted", "Seller Accepted", "Match Accepted",
		"Partially Paid Open", "Expired Partially Paid", "Paid", "Expired Unpaid",
		"INVALID"
	};

	if (status > XMATCH_STATUS_INVALID)
		status = XMATCH_STATUS_INVALID;

	return statusstr[status];
}

string Xmatch::DebugString(bool show_requests) const
{
	// Note: Exchange_Matches table has 16 columns:
	//		Xmatchnum integer
	//		BuyXreqnum integer, SellXreqnum integer
	//		Type integer, Status integer, NextDeadline integer
	//		MatchTimestamp integer, AcceptTimestamp integer, FinalTimestamp integer
	//		BaseAmount blob, Rate float, AmountPaid float
	//		AcceptTime integer, BuyerConsideration integer, SellerConsideration integer
	//		BuyerPledge integer
	//
	// Minus these 4 columns which are placed in the xbuy and xsell structs:
	//		BuyXreqnum integer, SellXreqnum integer
	//		BuyerConsideration integer, SellerConsideration integer
	//
	// Plus 1 value (have_xreqs) to indicate which xbuy and xsell fields are valid
	//
	// Exchange_Matches table in wallet adds these 4 columns:
	//		WalletPaid integer, WalletPaymentForeignTxid text, WalletPollTime integer, WalletReminderTime integer
	//
	// --> 16-4+1+4 = 17 columns in the Xmatch struct

	ostringstream out;

	out << "xmatchnum " << xmatchnum;
	out << " type " << type << " = " << Xtx::TypeString(type);
	out << " status " << status << " = " << StatusString();
	out << " have_xreqs " << have_xreqs;
	out << " wallet_paid " << wallet_paid;
	out << " wallet_payment_foreign_txid " << wallet_payment_foreign_txid;
	out << " wallet_polltime " << wallet_polltime;
	out << " wallet_reminder_time " << wallet_reminder_time;
	out << " next_deadline " << next_deadline;
	out << " match_timestamp " << match_timestamp;
	out << " accept_timestamp " << accept_timestamp;
	out << " final_timestamp " << final_timestamp;
	out << " base_amount " << base_amount;
	out << " rate " << rate;
	out << " amount_paid " << amount_paid;
	out << " mining_amount " << mining_amount;
	out << " accept_time " << accept_time;
	out << " match_pledge " << match_pledge;

	if (show_requests)
	{
		out << " ; buy: " << xbuy.DebugString();
		out << " ; sell: " << xsell.DebugString();
	}

	return out.str();
}

void Xmatch::Init(const uint64_t matchtime, const Xreq& buyer, const Xreq& seller)
{
	CCASSERT(buyer.base_asset == seller.base_asset);
	CCASSERT(buyer.quote_asset == seller.quote_asset);
	CCASSERT(buyer.foreign_asset == seller.foreign_asset);
	CCASSERT(buyer.best_amount == seller.best_amount);
	CCASSERT(buyer.best_rate == seller.best_rate);
	CCASSERT(buyer.pledge >= seller.pledge);
	CCASSERT(buyer.payment_time >= seller.payment_time);

	bigint_t check;

	type = buyer.type;
	// status set below
	have_xreqs = true;
	wallet_paid = 0;
	wallet_payment_foreign_txid.clear();
	wallet_polltime = 0;
	wallet_reminder_time = 0;
	// next_deadline set below
	match_timestamp = matchtime;
	// accept_timestamp set below
	final_timestamp = 0;
	base_amount = buyer.best_amount;
	rate = buyer.best_rate;
	amount_paid = 0;
	mining_amount = 0;
	accept_time = max(buyer.accept_time_required, seller.accept_time_required);
	match_pledge = buyer.pledge;

	xbuy.Init(buyer, seller, buyer.best_amount);
	xsell.Init(seller, buyer, buyer.best_amount);

	if (accept_time)
	{
		CCASSERT(0);	// accept_time not yet supported
	}
	else
	{
		CCASSERT(xbuy.flags.have_matching);
		CCASSERT(xsell.flags.have_matching);

		CCASSERT(xbuy.flags.auto_accept_matches);
		CCASSERT(xsell.flags.auto_accept_matches);

		status = XMATCH_STATUS_ACCEPTED;

		accept_timestamp = matchtime;

		next_deadline = matchtime + seller.payment_time;

		if (xbuy.delete_time < next_deadline)
			xbuy.delete_time = next_deadline;

		if (xsell.delete_time < next_deadline)
		{
			CCASSERTZ(xsell.foreign_address.length());	// can't change this in crosschain sell req's because ForeignAddress conflicts affect the validity of other xreq's and therefore the validity of blocks
			xsell.delete_time = next_deadline;
		}
	}
}

UniFloat Xmatch::QuoteAmount() const
{
	return Xreq::QuoteAmount(Xtx::asUniFloat(xsell.base_asset, base_amount), rate);
}

UniFloat Xmatch::AmountToPay(bool roundup) const
{
	auto amount = UniFloat::Add(QuoteAmount(), -amount_paid, 1);	// round up

	if (amount < 0)
		amount = 0;

	if (!roundup)
		return amount;

	auto encoded = UniFloat::WireEncode(amount, 1);	// round up; note: WireEncode can throw an exception

	auto result = UniFloat::WireDecode(encoded);

	//cout << "AmountToPay " << amount << "=required_amount " << result << "=required_rounded " << encoded << "=required_encoded" << endl;

	CCASSERT(result >= amount);

	auto check_encoded = UniFloat::WireEncode(result, -1);	// round down
	auto check_decoded = UniFloat::WireDecode(check_encoded);

	//if (check_encoded != encoded) cout << "ERROR AmountToPay check_encoded " << check_encoded << " != " << encoded << endl;
	//if (check_decoded <  amount)  cout << "ERROR AmountToPay check_decoded " << check_decoded << " < "  << amount  << endl;

	CCASSERT(check_decoded >= amount);

	return result;
}

string Xmatch::AmountToPayString() const
{
	auto amount = AmountToPay(true);

	auto result = amount.asRoundedString(1, FOREIGN_PRECISION);

	//cout << "AmountToPay " << amount << "=required_rounded " << result << "=told_to_pay " << endl;

	return result;
}
