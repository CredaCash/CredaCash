/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * CCobjects.hpp
*/

#pragma once

#include <CCobjdefs.h>

#include <cstdint>
#include <cstring>

#include <boost/noncopyable.hpp>

//#define TEST_SEQ_TX_OID	1

#ifndef TEST_SEQ_TX_OID
#define TEST_SEQ_TX_OID		0	// don't test
#endif

#pragma pack(push, 1)

class CCObject : private boost::noncopyable
{
public:

	struct Preamble
	{
		union
		{
			std::array<void*, 2> auxp;

			ccoid_t oid;
		};
	};

	struct Header
	{
		std::uint32_t size;
		std::uint32_t tag;
	};

protected:

	Preamble preamble;

	Header header;

	std::uint8_t data;

public:

	void SetSize(unsigned size)
	{
		header.size = size;
	}

	void SetTag(unsigned tag)
	{
		header.tag = tag;
	}

	ccoid_t* OidPtr();

	inline const ccoid_t* OidPtr() const
	{
		return ((CCObject*)this)->OidPtr();
	}

	inline std::uint8_t* ObjPtr()
	{
		return (std::uint8_t*)&header;
	}

	inline const std::uint8_t* ObjPtr() const
	{
		return ((CCObject*)this)->ObjPtr();
	}

	inline unsigned ObjSize() const
	{
		return header.size;
	}

	inline const std::uint8_t* ObjEndPtr() const
	{
		return ObjPtr() + ObjSize();
	}

	inline std::uint8_t* DataPtr()
	{
		return &data;
	}

	inline const std::uint8_t* DataPtr() const
	{
		return &data;
	}

	inline unsigned DataSize() const
	{
		if (ObjSize() < sizeof(header))
			return 0;
		else
			return ObjSize() - sizeof(header);
	}

	inline std::uint32_t ObjTag() const
	{
		return header.tag;
	}

	static bool HasPOW(std::uint32_t tag);
	bool HasPOW() const
	{
		return HasPOW(ObjTag());
	}

	static std::uint32_t BlockTag(std::uint32_t tag);
	std::uint32_t BlockTag() const
	{
		return BlockTag(ObjTag());
	}

	static std::uint32_t WireTag(std::uint32_t tag);
	std::uint32_t WireTag() const
	{
		return WireTag(ObjTag());
	}

	static std::uint32_t TypeToWireTag(unsigned type);

	static unsigned ObjType(std::uint32_t tag);
	unsigned ObjType() const
	{
		return ObjType(ObjTag());
	}

	bool IsValid() const;

	std::uint8_t* BodyPtr();

	const std::uint8_t* BodyPtr() const
	{
		return ((CCObject*)this)->BodyPtr();
	}

	unsigned BodySize() const;

	void ComputeObjId(ccoid_t *oid) const;
	void SetObjId();

	static void ComputeMessageObjId(const void* msg, ccoid_t *oid);
};

#pragma pack(pop)
