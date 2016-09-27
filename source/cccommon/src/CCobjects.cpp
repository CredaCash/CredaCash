/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * CCobjects.cpp
*/

#include <CCobjects.hpp>
#include <blake2/blake2b.h>
#include <CCassert.h>

#include <cstdint>
#include <iostream>

using namespace std;

bool CCObject::IsValid() const
{
	// check packing
	CCASSERT((uintptr_t)&this->preamble == (uintptr_t)this);
	CCASSERT((uintptr_t)&this->header == (uintptr_t)this + sizeof(preamble));
	CCASSERT((uintptr_t)&this->data == (uintptr_t)this + sizeof(preamble) + sizeof(header));

	switch (ObjTag())
	{
	case CC_TAG_BLOCK:
		return BodySize() <= CC_BLOCK_MAX_SIZE;
	case CC_TAG_TX_WIRE:
	case CC_TAG_TX_BLOCK:
		return BodySize() <= TX_MAX_SIZE;
	}

	return false;
}

ccoid_t* CCObject::OidPtr()
{
	if (ObjTag() == CC_TAG_BLOCK)
		return (ccoid_t*)(preamble.auxp[0]);	// points to struct BlockAux
	else
		return &preamble.oid;
}

uint8_t* CCObject::BodyPtr()
{
	if (ObjTag() == CC_TAG_TX_WIRE)
		return DataPtr() + TX_POW_SIZE;
	else
		return DataPtr();
}

unsigned CCObject::BodySize() const
{
	if (ObjTag() == CC_TAG_TX_WIRE)
		return DataSize() - TX_POW_SIZE;
	else
		return DataSize();
}

void CCObject::SetObjId()
{
	CCASSERT(ObjTag() != CC_TAG_BLOCK);	// for blocks, call Block::CalcOid

	auto rc = blake2b(OidPtr(), sizeof(ccoid_t), NULL, 0, BodyPtr(), BodySize());
	CCASSERTZ(rc);

	//cerr << "SetObjId hashed " << BodySize() << " bytes starting with " << hex << *(uint64_t*)BodyPtr() << " result " << *(uint64_t*)OidPtr() << dec << endl;

#if TEST_SEQ_TX_OID
	*(uint32_t*)OidPtr() = *(uint32_t*)DataPtr();
#endif
}

