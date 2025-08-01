/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * xtransaction-xpay.hpp
*/

// The Xpay class represents exchange trade payments (msg type CC_TYPE_XCX_PAYMENT)

#pragma once

#include "xtransaction.hpp"

#define SATOSHI_PER_BITCOIN		1e8

//#define TRACE_XPAYS		1

#ifndef TRACE_XPAYS
#define TRACE_XPAYS		0	// don't trace
#endif

#if TEST_XREQ
#pragma pack(push, 1)
#endif

class Xpay : public Xtx
{
public:
	UniFloat	match_left_to_pay;
	UniFloat	foreign_amount;
	uint64_t	foreign_amount_fp;

	uint64_t	xmatchnum;
	uint64_t	match_timestamp;
	uint64_t	foreign_blockchain;
	unsigned	foreign_confirmations_required;
	uint16_t	payment_time;

	string		foreign_block_id;
	string		foreign_txid;
	string		foreign_address;

	~Xpay() = default;

	string DebugString() const;

	void Clear(bool top_only = false)
	{
		if (!top_only)
			Xtx::Clear();

		memset((void*)&foreign_amount, 0, (uintptr_t)&foreign_block_id - (uintptr_t)&foreign_amount);

		foreign_block_id.clear();
		foreign_txid.clear();
		foreign_address.clear();
	}

	void Copy(const Xpay& other, bool top_only = false)
	{
		if (!top_only)
			Xtx::Copy(other);

		memcpy((void*)&foreign_amount, &other.foreign_amount, (uintptr_t)&foreign_block_id - (uintptr_t)&foreign_amount);

		foreign_block_id = other.foreign_block_id;
		foreign_txid = other.foreign_txid;
		foreign_address = other.foreign_address;
	}

	Xpay* Clone() const
	{
		return new Xpay(*this);
	}

	Xpay(const Xpay& other)
		: Xtx(other)
	{
		Copy(other, true);
	}

	Xpay(const unsigned type_ = CC_TYPE_XCX_PAYMENT)
		: Xtx(type_)
	{
		Clear(true);
	}

	Xpay(const uint64_t xmatchnum_, const UniFloat& foreign_amount_, const string& foreign_block_id_, const string& foreign_txid_);

	static Xpay* Cast(Xtx* p)
	{
		return dynamic_cast<Xpay*>(p);
	}

	static Xpay* Cast(shared_ptr<Xtx>& p)
	{
		return Cast(p.get());
	}

	void ComputePaymentIdHash(void *hash, unsigned hashsize) const;

	void DataToWire(const string& fn, void *binbuf, const uint32_t binsize, uint32_t &bufpos);
	void DataFromWire(const string& fn, const void *binbuf, const uint32_t binsize, uint32_t &bufpos);
};

#if TEST_XREQ
#pragma pack(pop)
#endif
