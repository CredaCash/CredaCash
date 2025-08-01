/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * xtransaction.hpp
*/

// The Xtx class is used for bare messages, and as a base for the Xreq class.
// These classes are currently used by:
//		The wallet: to construct exchange request messages
//		The nodes: to deserialize exchange request messages, and to match requests.

#pragma once

#include "CCbigint.hpp"
#include "amounts.h"

#include <CCobjdefs.h>
#include <unifloat.hpp>

//#define TEST_XREQ				1	// for testing

#ifndef TEST_XREQ
#define TEST_XREQ				0	// don't test
#endif

#define XTX_TIME_DIVISOR		1		// expire_time accuracy; note at 1 second accuracy, 2^32 overflows after 136 years

#define XTX_MAX_ITEM_SIZE		247		// reserve 248-255 for longer items

using namespace snarkfront;

#if TEST_XREQ
#pragma pack(push, 1)
#endif

class Xtx
{
public:
	unsigned	type;
	unsigned	expiration;				// user input seconds until expiration
	unsigned	amount_bits;
	unsigned	exponent_bits;
	bigint_t	amount_carry_in;
	bigint_t	amount_carry_out;
	uint64_t	expire_time;
	uint64_t	db_search_max;			// upper bound for db search

	virtual ~Xtx() = default;

	virtual string DebugString() const;

	static string TypeString(unsigned type);

	string TypeString() const
	{
		return TypeString(type);
	}

	virtual void Clear()
	{
		memset(&type, 0, sizeof(db_search_max) + (uintptr_t)&db_search_max - (uintptr_t)&type);
	}

	virtual void Copy(const Xtx& other)
	{
		memcpy(&type, &other.type, sizeof(db_search_max) + (uintptr_t)&db_search_max - (uintptr_t)&type);
	}

	static std::shared_ptr<Xtx> New(const unsigned type, const bool for_testnet);

	virtual Xtx* Clone() const
	{
		return new Xtx(*this);
	}

	Xtx(const Xtx& other)
	{
		Xtx::Copy(other);
	}

	Xtx(const unsigned type_ = 0, const uint64_t expiration_ = 0)
	{
		Xtx::Clear();

		type = type_;

		if (expiration_ > 365*24*60*60)
			expire_time = expiration_;
		else
			expiration = expiration_;
	}

#if TEST_XREQ
	virtual void Randomize()
	{
		CCRandom(&type, sizeof(db_search_max) + (uintptr_t)&db_search_max - (uintptr_t)&type);
	}
#endif

	static bool TypeIsXtx(unsigned type)
	{
		return type >= CC_TYPE_XCX_NAKED_BUY && type <= CC_TYPE_XCX_MINING_SELL;
	}

	static bool TypeIsCrosschain(unsigned type)
	{
		return type >= CC_TYPE_XCX_NAKED_BUY && type <= CC_TYPE_XCX_MINING_SELL;
	}

	static bool TypeIsXpay(unsigned type)
	{
		return type == CC_TYPE_XCX_PAYMENT;
	}

	static bool TypeIsXreq(unsigned type)
	{
		return (type >= CC_TYPE_XCX_NAKED_BUY && type <= CC_TYPE_XCX_REQ_SELL) || (type >= CC_TYPE_XCX_MINING_TRADE && type <= CC_TYPE_XCX_MINING_SELL);
	}

	static bool TypeHasBareMsg(unsigned type)
	{
		return type == CC_TYPE_XCX_NAKED_BUY || type == CC_TYPE_XCX_ACCEPT || type == CC_TYPE_XCX_CANCEL || type == CC_TYPE_XCX_PAYMENT;
	}

	static bool TypeIsNaked(unsigned type)
	{
		return type == CC_TYPE_XCX_NAKED_BUY || type == CC_TYPE_XCX_NAKED_SELL;
	}

	static bool TypeIsBuyer(unsigned type)
	{
		return type == CC_TYPE_XCX_REQ_BUY || type == CC_TYPE_XCX_SIMPLE_BUY || type == CC_TYPE_XCX_NAKED_BUY || type == CC_TYPE_XCX_MINING_BUY || type == CC_TYPE_XCX_MINING_TRADE;		// a mining trade req is both a buy and a sell req
	}

	static bool TypeIsSeller(unsigned type)
	{
		return type == CC_TYPE_XCX_REQ_SELL || type == CC_TYPE_XCX_SIMPLE_SELL || type == CC_TYPE_XCX_NAKED_SELL || type == CC_TYPE_XCX_MINING_SELL || type == CC_TYPE_XCX_MINING_TRADE;	// a mining trade req is both a buy and a sell req
	}

	static bool TypeIsSimple(unsigned type)
	{
		return type == CC_TYPE_XCX_SIMPLE_BUY || type == CC_TYPE_XCX_SIMPLE_SELL || type == CC_TYPE_XCX_MINING_BUY || type == CC_TYPE_XCX_MINING_SELL || type == CC_TYPE_XCX_MINING_TRADE;
	}

	bool IsNaked() const
	{
		return TypeIsNaked(type);
	}

	bool IsBuyer() const
	{
		return TypeIsBuyer(type);
	}

	bool IsSeller() const
	{
		return TypeIsSeller(type);
	}

	bool IsSimple() const
	{
		return TypeIsSimple(type);
	}

	static amtfloat_t asFullFloat(uint64_t asset, const bigint_t& amount);

	static double asDouble(uint64_t asset, const bigint_t& amount)
	{
		return (double)asFullFloat(asset, amount);
	}

	static UniFloat asUniFloat(uint64_t asset, const bigint_t& amount)
	{
		return asDouble(asset, amount);
	}

	static uint64_t Encode_Time(uint64_t timestamp);
	static uint64_t Decode_Time(uint64_t timestamp);

	int ToWire(const string& fn, void *binbuf, const uint32_t binsize, uint32_t &bufpos);
	int FromWire(const string& fn, unsigned wire_tag, const void *binbuf, const uint32_t binsize);

	virtual void DataToWire(const string& fn, void *binbuf, const uint32_t binsize, uint32_t &bufpos);
	virtual void DataFromWire(const string& fn, const void *binbuf, const uint32_t binsize, uint32_t &bufpos);

	static int SetPow(void *binbuf, const uint32_t binsize, uint64_t difficulty, uint64_t expiration);
	static int CheckPow(const void *binbuf, const uint32_t binsize, uint64_t difficulty);
};

#if TEST_XREQ
#pragma pack(pop)
#endif
