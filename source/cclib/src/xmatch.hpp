/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * xmatch.hpp
*/

// These classes are used by the node and wallet to track and report on exchange requests and matches

#pragma once

#include "CCapi.h"
#include "CCbigint.hpp"

#include <CCobjdefs.h>
#include <unifloat.hpp>
#include <ed25519/ed25519.h>

#define XMATCH_REQ_DISPOSITION_VOID				0
#define XMATCH_REQ_DISPOSITION_CANCELLED_ALL 	1
#define XMATCH_REQ_DISPOSITION_CANCELLED_REM 	2
#define XMATCH_REQ_DISPOSITION_EXPIRED_ALL		3
#define XMATCH_REQ_DISPOSITION_EXPIRED_REM		4	// not used for crosschain sell reqs because they can only match once
#define XMATCH_REQ_DISPOSITION_OPEN_ALL			5
#define XMATCH_REQ_DISPOSITION_OPEN_PART		6	// not used for crosschain sell reqs because they can only match once
#define XMATCH_REQ_DISPOSITION_MATCHED_PART		7	// used for crosschain sell reqs that can only match once
#define XMATCH_REQ_DISPOSITION_MATCHED_ALL		8
#define XMATCH_REQ_DISPOSITION_INVALID			9

#define XMATCH_STATUS_VOID				0
#define XMATCH_STATUS_BUYER_CANCEL		1
#define XMATCH_STATUS_SELLER_CANCEL		2
#define XMATCH_STATUS_MATCHED			3
#define XMATCH_STATUS_BUYER_ACCEPTED	4
#define XMATCH_STATUS_SELLER_ACCEPTED	5
#define XMATCH_STATUS_ACCEPTED			6
#define XMATCH_STATUS_PART_PAID_OPEN	7
#define XMATCH_STATUS_PART_PAID_EXPIRED	8
#define XMATCH_STATUS_PAID				9
#define XMATCH_STATUS_UNPAID_EXPIRED	10
#define XMATCH_STATUS_INVALID			11

#define MINED_ASSET						0

using namespace snarkfront;

class Xreq;

class Xmatchreq
{
public:
	// 42 fields total (counting union as one field):
	bigint_t destination;					// valid when have_matching set
	bigint_t open_amount;					// valid when have_matching set
	union
	{
		ed25519_secret_key signing_private_key;	// used by wallet
		ed25519_public_key signing_public_key;
	};
	ccoid_t	 objid;
	uint64_t id;							// used by wallet
	uint64_t tx_id;							// used by wallet
	uint64_t address_id;					// used by wallet
	uint64_t query_matchnum;				// used by wallet
	uint64_t poll_time;						// used by wallet
	uint64_t expire_time;
	uint64_t delete_time;
	uint64_t xreqnum;
	uint64_t base_asset;
	uint64_t quote_asset;
	bigint_t min_amount;
	bigint_t max_amount;
	UniFloat net_rate_required;
	UniFloat wait_discount;
	UniFloat base_costs;
	UniFloat quote_costs;
	unsigned type;
	unsigned disposition;
	uint16_t consideration_required;
	uint16_t consideration_offered;
	uint16_t match_consideration;			// in ccnode, read from the Exchange_Matches table
	uint16_t pledge;
	uint16_t hold_time;
	uint16_t hold_time_required;
	uint16_t min_wait_time;
	uint16_t accept_time_required;
	uint16_t accept_time_offered;
	uint16_t payment_time;
	uint16_t confirmations;
	struct
	{
		uint16_t	have_matching;
		uint16_t	add_immediately_to_blockchain;
		uint16_t	auto_accept_matches;
		uint16_t	no_minimum_after_first_match;
		uint16_t	must_liquidate_crossing_minimum;
		uint16_t	must_liquidate_below_minimum;
		uint16_t	has_signing_key;
	} flags;

	string foreign_asset;
	string foreign_address;								// valid for seller when have_matching set

	string DebugString() const;

	static string DispositionString(unsigned disposition);

	string DispositionString() const
	{
		return DispositionString(disposition);
	}

	Xmatchreq()
	{
		Clear();
	}

	Xmatchreq(const Xmatchreq& other)
	 :	foreign_asset(other.foreign_asset),
		foreign_address(other.foreign_address)
	{
		memcpy((void*)this, &other, (uintptr_t)&foreign_asset - (uintptr_t)this);
	}

	Xmatchreq(const Xreq& xreq, uint64_t _tx_id = 0)
	{
		Clear();

		Init(xreq, xreq, 0UL, _tx_id);
	}

	void Clear()
	{
		memset((void*)this, 0, (uintptr_t)&foreign_asset - (uintptr_t)this);

		foreign_asset.clear();
		foreign_address.clear();
	}

	void Init(const Xreq& xreq, const Xreq& other, const bigint_t& match_amount = 0UL, uint64_t _tx_id = 0);

	static bool BlockchainRequiresUniqueForeignAddress(uint64_t foreign_blockchain)
	{
		return true;
	}

	bool BlockchainRequiresUniqueForeignAddress() const
	{
		return BlockchainRequiresUniqueForeignAddress(quote_asset);
	}

	bool IsOpen() const
	{
		return (	!disposition
				||	 disposition == XMATCH_REQ_DISPOSITION_OPEN_ALL
				||	 disposition == XMATCH_REQ_DISPOSITION_OPEN_PART );
	}

	bool IsClosed() const
	{
		return !IsOpen();
	}

	bool HasMatches() const
	{
		return (	disposition == XMATCH_REQ_DISPOSITION_CANCELLED_REM
				||	disposition == XMATCH_REQ_DISPOSITION_EXPIRED_REM
				||	disposition == XMATCH_REQ_DISPOSITION_OPEN_PART
				||	disposition == XMATCH_REQ_DISPOSITION_MATCHED_PART
				||	disposition == XMATCH_REQ_DISPOSITION_MATCHED_ALL );
	}

	bool ExpectingFunds() const
	{
		return (type != CC_TYPE_XCX_NAKED_BUY && disposition != XMATCH_REQ_DISPOSITION_MATCHED_ALL);
	}
};

class Xmatch
{
public:
	// 17 fields total:
	unsigned type;
	unsigned status;
	uint64_t xmatchnum;
	uint64_t wallet_polltime;				// used by wallet
	uint64_t wallet_reminder_time;			// used by wallet
	uint64_t next_deadline;
	uint64_t match_timestamp;
	uint64_t accept_timestamp;
	uint64_t final_timestamp;
	bigint_t base_amount;
	UniFloat rate;
	UniFloat amount_paid;
	UniFloat mining_amount;
	uint16_t have_xreqs;
	uint16_t accept_time;
	uint16_t match_pledge;
	 int16_t wallet_paid;					// used by wallet

	string	 wallet_payment_foreign_txid;	// used by wallet

	Xmatchreq xbuy;
	Xmatchreq xsell;

	string DebugString(bool show_requests = true) const;

	static string StatusString(unsigned status);

	string StatusString() const
	{
		return StatusString(status);
	}

	Xmatch()
	{
		Clear(true);
	}

	Xmatch(const uint64_t matchtime, const Xreq& buyer, const Xreq& seller)
	{
		Clear(true);

		Init(matchtime, buyer, seller);
	}

	Xmatch(const Xmatch& other)
	 :	wallet_payment_foreign_txid(other.wallet_payment_foreign_txid),
		xbuy(other.xbuy),
		xsell(other.xsell)
	{
		memcpy((void*)this, &other, (uintptr_t)&wallet_payment_foreign_txid - (uintptr_t)this);
	}

	void Clear(bool top_only = false)
	{
		if (!top_only)
		{
			xbuy.Clear();
			xsell.Clear();
		}

		memset((void*)this, 0, (uintptr_t)&wallet_payment_foreign_txid - (uintptr_t)this);

		wallet_payment_foreign_txid.clear();
	}

	Xmatch* Clone() const
	{
		return new Xmatch(*this);
	}

	void Init(const uint64_t matchtime, const Xreq& buyer, const Xreq& seller);

	static bool StatusIsOpen(unsigned status)
	{
		return !status || (status >= XMATCH_STATUS_MATCHED && status <= XMATCH_STATUS_PART_PAID_OPEN);
	}

	static bool StatusIsClosed(unsigned status)
	{
		return !StatusIsOpen(status);
	}

	static bool StatusIsPending(unsigned status)
	{
		return !status || (status >= XMATCH_STATUS_MATCHED && status < XMATCH_STATUS_ACCEPTED);
	}

	bool IsOpen() const
	{
		return StatusIsOpen(status);
	}

	bool IsClosed() const
	{
		return !IsOpen();
	}

	bool IsPending() const
	{
		return StatusIsPending(status);
	}

	UniFloat QuoteAmount() const;
	UniFloat AmountToPay(bool roundup = false) const;
	string AmountToPayString() const;
};
