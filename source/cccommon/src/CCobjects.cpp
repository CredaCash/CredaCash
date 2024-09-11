/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * CCobjects.cpp
*/

#include "CCdef.h"
#include "CCobjects.hpp"

#include <blake2/blake2.h>

/*

Message format and in-memory object format:

	8 byte header:
		4 byte size = size of entire message or object (header + POW + body)
		4 byte tag
	for messages only, 48 byte Proof of Work:
		8 byte timestamp
		8*5 byte nonces
	body

128 bit oid = blake2b(key=tag, data=body)

*/

bool CCObject::HasPOW(uint32_t tag)
{
	return (tag >= CC_TAG_MINT && tag <= CC_TAG_LAST_OBJ && !(tag & CC_TAG_BLOCK_FLAG));
}

uint32_t CCObject::WireTag(uint32_t tag)
{
	return tag & ~CC_TAG_BLOCK_FLAG;
}

uint32_t CCObject::BlockTag(uint32_t tag)
{
	return tag | CC_TAG_BLOCK_FLAG;
}

uint32_t CCObject::TypeToWireTag(unsigned type)
{
	switch (type)
	{
	case CC_TYPE_BLOCK:
		return CC_TAG_BLOCK;

	case CC_TYPE_MINT:
		return CC_TAG_MINT;

	case CC_TYPE_TXPAY:
		return CC_TAG_TX;

	case CC_TYPE_XCX_SIMPLE_BUY:
		return CC_TAG_XCX_SIMPLE_BUY;

	case CC_TYPE_XCX_SIMPLE_SELL:
		return CC_TAG_XCX_SIMPLE_SELL;

	case CC_TYPE_XCX_MINING_TRADE:
		return CC_TAG_XCX_SIMPLE_TRADE;

	case CC_TYPE_XCX_NAKED_BUY:
		return CC_TAG_XCX_NAKED_BUY;

	case CC_TYPE_XCX_NAKED_SELL:
		return CC_TAG_XCX_NAKED_SELL;

	case CC_TYPE_XCX_PAYMENT:
		return CC_TAG_XCX_PAYMENT;

	default:
		return 0;
	}
}

unsigned CCObject::ObjType(uint32_t tag)
{
	//cout << "CCObject::ObjType tag " << hex << tag << " WireTag " << WireTag(tag) << dec << endl;

	switch (WireTag(tag))
	{
	case CC_TAG_BLOCK:
		return CC_TYPE_BLOCK;

	case CC_TAG_MINT:
		return CC_TYPE_MINT;

	case CC_TAG_TX:
	case CC_TAG_TX_XDOMAIN:
		return CC_TYPE_TXPAY;

	case CC_TAG_XCX_SIMPLE_BUY:
		return CC_TYPE_XCX_SIMPLE_BUY;

	case CC_TAG_XCX_SIMPLE_SELL:
		return CC_TYPE_XCX_SIMPLE_SELL;

	case CC_TAG_XCX_SIMPLE_TRADE:
		return CC_TYPE_XCX_MINING_TRADE;

	case CC_TAG_XCX_NAKED_BUY:
		return CC_TYPE_XCX_NAKED_BUY;

	case CC_TAG_XCX_NAKED_SELL:
		return CC_TYPE_XCX_NAKED_SELL;

	case CC_TAG_XCX_PAYMENT:
		return CC_TYPE_XCX_PAYMENT;

	default:
		return CC_TYPE_VOID;
	}
}

bool CCObject::IsValid() const
{
	// check packing
	CCASSERT((uintptr_t)&this->preamble == (uintptr_t)this);
	CCASSERT((uintptr_t)&this->header == (uintptr_t)this + sizeof(preamble));
	CCASSERT((uintptr_t)&this->data == (uintptr_t)this + sizeof(preamble) + sizeof(header));

	switch (ObjType())
	{
	case CC_TYPE_BLOCK:

		return BodySize() <= CC_BLOCK_MAX_SIZE;

	case CC_TYPE_MINT:
	case CC_TYPE_TXPAY:
	case CC_TYPE_XCX_SIMPLE_BUY:
	case CC_TYPE_XCX_SIMPLE_SELL:
	case CC_TYPE_XCX_MINING_TRADE:
	case CC_TYPE_XCX_NAKED_BUY:
	case CC_TYPE_XCX_NAKED_SELL:
	case CC_TYPE_XCX_PAYMENT:

		return BodySize() <= TX_MAX_SIZE;
	}

	return false;
}

ccoid_t* CCObject::OidPtr()
{
	if (ObjType() == CC_TYPE_BLOCK)
		return (ccoid_t*)(preamble.auxp[0]);	// points to struct BlockAux
	else
		return &preamble.oid;
}

uint8_t* CCObject::BodyPtr()
{
	if (HasPOW())
		return DataPtr() + TX_POW_SIZE;
	else
		return DataPtr();
}

unsigned CCObject::BodySize() const
{
	if (HasPOW())
		return DataSize() - TX_POW_SIZE;
	else
		return DataSize();
}

void CCObject::ComputeMessageObjId(const void* msg, ccoid_t *oid)
{
	auto obj = (CCObject*)((char*)msg - sizeof(CCObject::Preamble));
	obj->ComputeObjId(oid);
}

void CCObject::ComputeObjId(ccoid_t *oid) const
{
	CCASSERT(ObjType() != CC_TYPE_BLOCK);	// for blocks, call Block::CalcOid

	// @@! split tx hash into two parts, first everything except the zkproof, then add in the zkproof?
	// !!! the first part would become the "reference hash" used to link tx's together...

	uint32_t tag = WireTag();

	auto rc = blake2b(oid, sizeof(ccoid_t), &tag, sizeof(tag), BodyPtr(), BodySize());
	CCASSERTZ(rc);

	//cerr << "SetObjId hashkey " << tag << " hashed " << BodySize() << " bytes starting with " << hex << *(uint64_t*)BodyPtr() << " result " << *(uint64_t*)oid << dec << endl;
}

void CCObject::SetObjId()
{
	ComputeObjId(OidPtr());

#if TEST_SEQ_TX_OID
	*(uint32_t*)OidPtr() = *(uint32_t*)DataPtr();
#endif
}

