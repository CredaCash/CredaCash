/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * xtransaction.cpp
*/

#include "cclib.h"
#include "xtransaction.hpp"
#include "xtransaction-xreq.hpp"
#include "xtransaction-xpay.hpp"
#include "CCparams.h"

#include <CCobjects.hpp>
#include <PackedInt.hpp>

//#define TEST_SHOW_WIRE_ERRORS	1	// for debugging

#ifndef TEST_SHOW_WIRE_ERRORS
#define TEST_SHOW_WIRE_ERRORS	0	// don't debug
#endif

typedef PackedUnsigned<CC_BLOCKTIME_WIRE_BYTES, XTX_TIME_DIVISOR, TX_TIME_OFFSET> EncodedTime;

amtfloat_t Xtx::asFullFloat(uint64_t asset, const bigint_t& amount)
{
	amtfloat_t val;

	amount_to_float(asset, amount, val);

	return val;
}

uint64_t Xtx::Encode_Time(uint64_t timestamp)
{
	EncodedTime encoded_val;

	encoded_val.SetValue(timestamp);

	auto result = encoded_val.ExtractValue();

	//cerr << "Encode_Time " << timestamp << " result " << result << endl;

	return result;
}

uint64_t Xtx::Decode_Time(uint64_t timestamp)
{
	EncodedTime encoded_val;

	encoded_val.ImplantValue(&timestamp);

	auto result = encoded_val.GetValue();

	//cerr << "Decode_Time " << timestamp << " result " << result << endl;

	return result;
}

string Xtx::TypeString(unsigned type)
{
	static const char *typestr[CC_TYPE_INVALID + 1] =
	{
		"VOID",
		"Mint", "Send", "Block",
		"Crosschain Naked Buy Request", "Crosschain Naked Sell Request",
		"Crosschain Simple Buy Request", "Crosschain Simple Sell Request",
		"Crosschain Buy Request", "Crosschain Sell Request",
		"Crosschain Payment Claim", "Crosschain Forfeit", "Crosschain Cancel",
		"INVALID"
	};

	if (type > CC_TYPE_INVALID)
		type = CC_TYPE_INVALID;

	return typestr[type];
}

string Xtx::DebugString() const
{
	ostringstream out;

	out << "Xtx";
	out << " type " << type << " = " << TypeString();

	//out << " amount_bits " << amount_bits;
	//out << " exponent_bits " << exponent_bits;
	//out << " amount_carry_in " << amount_carry_in;
	//out << " amount_carry_out " << amount_carry_out;

	out << " expire_time " << expire_time;

	return out.str();
}

shared_ptr<Xtx> Xtx::New(const unsigned type_, const bool for_testnet_)
{
	if (TypeIsXreq(type_))
	{
		shared_ptr<Xtx> p = make_shared<Xreq>(type_, for_testnet_);
		//auto p = shared_ptr<Xtx>(new Xreq(type_));
		//cerr << "new Xreq shared_ptr " << hex << (uintptr_t)p.get() << " xreq " << (uintptr_t)Xreq::Cast(p) << dec << endl;
		return p;
	}
	else if (TypeIsXpay(type_))
	{
		shared_ptr<Xtx> p = make_shared<Xpay>(type_);
		//auto p = shared_ptr<Xtx>(new Xpay(type_));
		//cerr << "new Xpay shared_ptr " << hex << (uintptr_t)p.get() << " xpay " << (uintptr_t)Xpay::Cast(p) << dec << endl;
		return p;
	}
	else if (TypeIsXtx(type_))
	{
		shared_ptr<Xtx> p = make_shared<Xtx>(type_);
		//auto p = shared_ptr<Xtx>(new Xtx(type_));
		//cerr << "new Xtx shared_ptr " << hex << (uintptr_t)p.get() << dec << endl;
		return p;
	}
	else
		return NULL;
}

int Xtx::ToWire(const string& fn, void *binbuf, const uint32_t binsize, uint32_t &bufpos)
{
	bufpos = 0;
	const bool bhex = false;

	auto wire_tag = CCObject::TypeToWireTag(type);
	CCASSERT(wire_tag);
	copy_to_buf(wire_tag, sizeof(wire_tag), bufpos, binbuf, binsize, bhex);

	DataToWire(fn, binbuf, binsize, bufpos);

	if (binsize && bufpos > binsize)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "Xtx::ToWire error bufpos " << bufpos << " > binsize " << binsize << endl;

		return bufpos;
	}

	//cerr << "ToWire nbytes " << bufpos << " data " << buf2hex(binbuf, bufpos) << endl;

	return 0;
}

// wire_tag == 0 -> ignore wire_tag parameter
// wire_tag == 1 -> read wire_tag from binbuf
// type == 0 -> set type from wire_tag
// otherwise type is checked against wire_tag if wire_tag != 0

int Xtx::FromWire(const string& fn, unsigned wire_tag, const void *binbuf, const uint32_t binsize)
{
	uint32_t bufpos = 0;
	const bool bhex = false;

	//cerr << "FromWire nbytes " << binsize << " data " << buf2hex(binbuf, binsize) << endl;

	if (wire_tag == 1)
	{
		copy_from_buf(wire_tag, sizeof(CCObject::Header::tag), bufpos, binbuf, binsize, bhex);
		auto wire_tag = CCObject::TypeToWireTag(type);
		CCASSERT(wire_tag);
	}

	if (wire_tag)
	{
		if (type && type != CCObject::ObjType(wire_tag))
			throw range_error("type mismatch");
		else
			type = CCObject::ObjType(wire_tag);
	}

	DataFromWire(fn, binbuf, binsize, bufpos);

	if (bufpos > binsize)
	{
		if (TEST_SHOW_WIRE_ERRORS) cerr << "Xtx::FromWire error bufpos " << bufpos << " > binsize " << binsize << endl;

		return 1;
	}

	if (bufpos != binsize && TEST_SHOW_WIRE_ERRORS) cerr << "Xtx::FromWire warning bufpos " << bufpos << " != binsize " << binsize << endl;

	return 0;
}

void Xtx::DataToWire(const string& fn, void *binbuf, const uint32_t binsize, uint32_t &bufpos)
{
	CCASSERT(TEST_XREQ);
}

void Xtx::DataFromWire(const string& fn, const void *binbuf, const uint32_t binsize, uint32_t &bufpos)
{
	CCASSERT(TEST_XREQ);
}

int Xtx::SetPow(void *binbuf, const uint32_t binsize, uint64_t difficulty, uint64_t expiration)
{
	auto hash_start = (char*)binbuf + sizeof(CCObject::Header::tag);
	auto hash_bytes = binsize - sizeof(CCObject::Header::tag) - sizeof(uint64_t);

	CCASSERT(hash_bytes < binsize);

	auto pnonce = (uint64_t*)(hash_start + hash_bytes);

	auto rc = ComputePOW(hash_start, hash_bytes, difficulty, expiration, *pnonce);

	if (!rc)
		CCASSERTZ(CheckPow(hash_start, hash_bytes + sizeof(uint64_t), difficulty));

	return rc;
}

int Xtx::CheckPow(const void *binbuf, const uint32_t binsize, uint64_t difficulty)
{
	auto hash_start = (char*)binbuf;
	auto hash_bytes = binsize - sizeof(uint64_t);

	CCASSERT(hash_bytes < binsize);

	auto pnonce = (uint64_t*)(hash_start + hash_bytes);

	return CheckPOW(hash_start, hash_bytes, difficulty, *pnonce);
}
