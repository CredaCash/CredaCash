/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * CCobjects.cpp
*/

#include "CCdef.h"
#include "CCobjects.hpp"

#include <blake2/blake2.h>

unsigned CCObject::ObjType() const
{
	switch (ObjTag())
	{
	case CC_TAG_BLOCK:
		return CC_TYPE_BLOCK;
	case CC_TAG_TX_WIRE:
	case CC_TAG_TX_BLOCK:
		return CC_TYPE_TXPAY;
	case CC_TAG_MINT_WIRE:
	case CC_TAG_MINT_BLOCK:
		return CC_TYPE_MINT;
	default:
		return 0;
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
	case CC_TYPE_TXPAY:
	case CC_TYPE_MINT:
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
	if (ObjTag() == CC_TAG_TX_WIRE || ObjTag() == CC_TAG_MINT_WIRE)
		return DataPtr() + TX_POW_SIZE;
	else
		return DataPtr();
}

unsigned CCObject::BodySize() const
{
	if (ObjTag() == CC_TAG_TX_WIRE || ObjTag() == CC_TAG_MINT_WIRE)
		return DataSize() - TX_POW_SIZE;
	else
		return DataSize();
}

void CCObject::SetObjId()
{
	CCASSERT(ObjType() != CC_TYPE_BLOCK);	// for blocks, call Block::CalcOid

	// @@! split tx hash into two parts, first everything except the zkproof, then add in the zkproof?
	// !!! the first part would become the "reference hash" used to link tx's together...

	auto rc = blake2b(OidPtr(), sizeof(ccoid_t), &header.tag, sizeof(header.tag), BodyPtr(), BodySize());
	CCASSERTZ(rc);

	//cerr << "SetObjId hashed " << BodySize() << " bytes starting with " << hex << *(uint64_t*)BodyPtr() << " result " << *(uint64_t*)OidPtr() << dec << endl;

#if TEST_SEQ_TX_OID
	*(uint32_t*)OidPtr() = *(uint32_t*)DataPtr();
#endif
}

