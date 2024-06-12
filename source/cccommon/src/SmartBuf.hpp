/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * SmartBuf.hpp
*/

#pragma once

#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <iostream>

#define TRACE_SMARTBUF		0

class SmartBuf
{
	typedef std::atomic<std::uint32_t> refcount_t;
	typedef volatile std::uint32_t nauxptrs_t;	// volatile in case a SmartBuf instance is accessed from more than one thread

	std::atomic<std::uint8_t*> buf;				// atomic in case a SmartBuf instance is accessed from more than one thread

	static std::size_t alloc_size(const std::uint8_t* bufp);

public:

	static int64_t ByteTotal();

#if TRACE_SMARTBUF
	SmartBuf();
#else
	SmartBuf()
		: buf(NULL)
	{ }
#endif

	SmartBuf(std::size_t bufsize);

	void CheckGuard(const std::uint8_t* bufp, bool refcount_iszero = false) const;

	std::size_t size() const;

	static std::size_t size(const std::uint8_t* bufp);

	std::uint8_t* data(int refcount_iszero = false) const;

	void SetAuxPtrCount(unsigned count);

	unsigned GetAuxPtrCount() const;

	static void SetRefCount(const std::uint8_t* bufp, unsigned count);

	static unsigned GetRefCount(const std::uint8_t* bufp);

	unsigned IncRef();

	unsigned DecRef();

	~SmartBuf();

	SmartBuf(void* p);

	SmartBuf(const SmartBuf& s);

	SmartBuf& operator= (const SmartBuf& s);

	SmartBuf(SmartBuf&& s);

	SmartBuf& operator= (SmartBuf&& s);

	void ClearRef();

	void SetBasePtr(void* p);

	void* BasePtr() const;

	operator bool() const
	{
		return BasePtr();
	}

	bool operator== (const SmartBuf& s) const
	{
		return BasePtr() == s.BasePtr();
	}

	bool operator!= (const SmartBuf& s) const
	{
		return !(*this == s);
	}
};
