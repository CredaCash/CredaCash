/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * SmartBuf.cpp
*/

#include "SmartBuf.hpp"
#include "CCUtil.h"

#include <CCassert.h>
#include <boost/log/trivial.hpp>

#include <unistd.h>
#include <atomic>

//#define TEST_DELAY_SMARTBUF_RELEASE		31

#ifndef TEST_DELAY_SMARTBUF_RELEASE
#define TEST_DELAY_SMARTBUF_RELEASE 0	// don't test
#endif

std::atomic<unsigned> objcount(0);
std::atomic<unsigned> maxobjcount(0);
std::atomic<unsigned> maxrefcount(0);

#if TRACE_SMARTBUF
SmartBuf::SmartBuf()
	: buf(NULL)
{
	//std::cerr << "SmartBuf " << (uintptr_t)this << " created empty" << std::endl;
	BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " created empty";
}
#endif

SmartBuf::SmartBuf(std::size_t bufsize)
	: buf(NULL)
{
	if (!bufsize || bufsize > 258*1024*1024)
	{
		if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " created with no buffer";

		return;
	}

	auto msize = bufsize;
	msize += sizeof(refcount_t) + sizeof(nauxptrs_t) + 2 * USE_SMARTBUF_GUARD * sizeof(std::uint32_t);

	auto bufp = (std::uint8_t*)malloc(msize);

	buf.store(bufp, std::memory_order_release);

	auto usize = size();

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " malloc'ed bufp " << (uintptr_t)bufp << " size " << bufsize << " useable size " << usize << " sizeof(refcount_t) " << sizeof(refcount_t) << " required alignment " << std::alignment_of<refcount_t>::value << " size of bufp " << sizeof(buf);

	if (bufp)
	{
		auto asize = alloc_size();

		CCASSERT(asize >= msize);
		CCASSERT(usize >= bufsize);

		memset(bufp, 0, asize);

		if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " zero'ed bufp " << (uintptr_t)bufp << " size " << asize;

		if (USE_SMARTBUF_GUARD)
		{
			*(std::uint32_t*)bufp = SMARTBUF_GUARD;

			*(std::uint32_t*)(bufp + asize - sizeof(std::uint32_t)) = SMARTBUF_GUARD;
		}

		SetRefCount(1);

		if (USE_SMARTBUF_GUARD) CheckGuard();

		static unsigned t0 = 0;
		if (!t0)
			t0 = time(NULL);

		auto nobjs = objcount.fetch_add(1);
		if (!(nobjs & (127)))
		{
			if (nobjs > maxobjcount.load())
			{
				maxobjcount.store(nobjs);

				unsigned t = time(NULL);
				unsigned dt = t - t0;
				t0 = t;

				//std::cerr << "SmartBuf nobjs " << nobjs << " dt " << dt << std::endl;
				if (1+TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(info) << "SmartBuf nobjs " << nobjs << " dt " << dt;	// !!! note "1+..." and "info"
			}
		}
	}
}

void SmartBuf::CheckGuard(bool refcount_iszero) const
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (bufp && USE_SMARTBUF_GUARD)
	{
		//if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " CheckGuard bufp " << (uintptr_t)bufp;

		auto refcount = GetRefCount();

		auto guard = *(std::uint32_t*)bufp;
		if (guard != SMARTBUF_GUARD)
		{
			BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " CheckGuard bufp " << (uintptr_t)bufp << " bad front guard " << std::hex << guard << std::dec << " buffer size " << size() << " refcount " << refcount;
			BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(data(-1), size());

			raise(SIGTERM);
		}

		if (refcount_iszero && refcount != 0)
		{
			BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " refcount " << refcount << " is not zero; buffer size " << size() << " refcount " << refcount;
			BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(data(-1), size());

			raise(SIGTERM);
		}

		if (!refcount_iszero && (refcount < 1 || refcount > 0xFFFF0000))
		{
			BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " refcount " << refcount << " buffer size " << size();
			BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(data(-1), size());

			raise(SIGTERM);
		}

		guard = *(std::uint32_t*)(bufp + alloc_size() - sizeof(std::uint32_t));
		if (guard != SMARTBUF_GUARD)
		{
			BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " CheckGuard bufp " << (uintptr_t)bufp << " bad back guard " << std::hex << guard << std::dec << " buffer size " << size() << " refcount " << refcount;
			BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(data(-1), size());

			raise(SIGTERM);
		}
	}
}

std::size_t SmartBuf::alloc_size() const
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (!bufp)
		return 0;

#ifdef _WIN32
	return _msize(bufp);
#else
	return malloc_usable_size(bufp);
#endif
}

std::size_t SmartBuf::size() const
{
	auto asize = alloc_size();

	if (!asize)
		return 0;

	asize -= sizeof(refcount_t) + sizeof(nauxptrs_t) + 2 * USE_SMARTBUF_GUARD * sizeof(std::uint32_t);

	return asize;
}

std::uint8_t* SmartBuf::data(int refcount_iszero) const
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (!bufp)
		return NULL;

	if (USE_SMARTBUF_GUARD && refcount_iszero >= 0) CheckGuard(refcount_iszero);

	return bufp + sizeof(refcount_t) + sizeof(nauxptrs_t) + USE_SMARTBUF_GUARD * sizeof(std::uint32_t);
}

void SmartBuf::SetAuxPtrCount(unsigned count)
{
	auto bufp = buf.load(std::memory_order_acquire);

	*((nauxptrs_t*)(bufp + sizeof(refcount_t) + USE_SMARTBUF_GUARD * sizeof(std::uint32_t))) = count;
}

unsigned SmartBuf::GetAuxPtrCount() const
{
	auto bufp = buf.load(std::memory_order_acquire);

	auto count = *((nauxptrs_t*)(bufp + sizeof(refcount_t) + USE_SMARTBUF_GUARD * sizeof(std::uint32_t)));

	if (count > 20)
	{
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " GetAuxPtrCount bufp " << (uintptr_t)bufp << " bad aux count " << count << " buffer size " << size() << " refcount " << GetRefCount();
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(data(-1), size());

		raise(SIGTERM);
	}

	return count;
}

void SmartBuf::SetRefCount(unsigned count)
{
	auto bufp = buf.load(std::memory_order_acquire);

	*((refcount_t*)(bufp + USE_SMARTBUF_GUARD * sizeof(std::uint32_t))) = count;
}

unsigned SmartBuf::GetRefCount() const
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (!bufp)
		return 0;

	return ((refcount_t*)(bufp + USE_SMARTBUF_GUARD * sizeof(std::uint32_t)))->load(std::memory_order_acquire);
}

unsigned SmartBuf::IncRef()
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (!bufp)
		return 0;

	//if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " IncRef bufp " << (uintptr_t)bufp;

	if (USE_SMARTBUF_GUARD) CheckGuard();

	auto refcount = ((refcount_t*)(bufp + USE_SMARTBUF_GUARD * sizeof(std::uint32_t)))->fetch_add(1, std::memory_order_acq_rel);

	if (refcount && !(refcount & 127) && refcount > maxrefcount.load())
	{
		maxrefcount.store(refcount);

		//std::cout << "SmartBuf max refcount " << refcount << std::endl;	// comment this out

		if (1+TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(info) << "SmartBuf max refcount " << refcount;	// !!! note "1+..." and "info"
	}

	// debugging; don't use in production code:
	// if (refcount && !(refcount & 0x3fff)) BOOST_LOG_TRIVIAL(warning) << "SmartBuf " << (uintptr_t)this << " IncRef bufp " << (uintptr_t)bufp << " from refcount " << refcount << " buffer contents " << buf2hex(data(), size() < 256 ? size() : 256);

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " IncRef bufp " << (uintptr_t)bufp << " from refcount " << refcount;

	return refcount+1;
}

unsigned SmartBuf::DecRef()
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (!bufp)
		return 0;

	//if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " DecRef bufp " << (uintptr_t)bufp;

	if (USE_SMARTBUF_GUARD) CheckGuard();

	auto refcount = ((refcount_t*)(bufp + USE_SMARTBUF_GUARD * sizeof(std::uint32_t)))->fetch_sub(1, std::memory_order_acq_rel);

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " DecRef bufp " << (uintptr_t)bufp << " from refcount " << refcount;

	if (refcount == 1)
	{
		if (bufp && (TEST_DELAY_SMARTBUF_RELEASE & rand()) == 1) sleep(1);

		if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " freeing bufp " << (uintptr_t)bufp;

		auto auxp = (void**)data(true);
		auto naux = GetAuxPtrCount();

		if (USE_SMARTBUF_GUARD)
		{
			*(std::uint32_t*)bufp = SMARTBUF_FREE;

			*(std::uint32_t*)(bufp + alloc_size() - sizeof(std::uint32_t)) = SMARTBUF_FREE;
		}

		if (naux && auxp[0])
		{
			if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " freeing aux pointer " << (uintptr_t)auxp[0];

			free(auxp[0]);
		}

		for (unsigned i = 1; i < naux; ++i)
		{
			if (auxp[i])
			{
				if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " DecRef aux pointer " << (uintptr_t)auxp[i];

				auto smartobj = SmartBuf(auxp[i]);
				smartobj.DecRef();
			}
		}

		free(bufp);

		auto nobjs = objcount.fetch_sub(1);
		(void)nobjs;
		//std::cout << "SmartBuf nobjs " << nobjs << std::endl;
	}

	return refcount-1;
}

SmartBuf::~SmartBuf()
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (!bufp)
		return;

	if ((TEST_DELAY_SMARTBUF_RELEASE & rand()) == 1) sleep(1);

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " destructor bufp " << (uintptr_t)bufp;

	DecRef();
}

SmartBuf::SmartBuf(void* p)
	: buf((std::uint8_t*)p)
{
	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " constructed from pointer " << (uintptr_t)buf.load(std::memory_order_acquire);

	IncRef();
}

SmartBuf::SmartBuf(const SmartBuf& s)
	: buf(s.buf.load(std::memory_order_acquire))
{
	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " constructed from smartbuf " << (uintptr_t)&s << " bufp " << (uintptr_t)buf.load(std::memory_order_acquire);

	IncRef();
}

SmartBuf& SmartBuf::operator= (const SmartBuf& s)
{
	auto bufp = buf.load(std::memory_order_acquire);
	auto sbufp = s.buf.load(std::memory_order_acquire);

	if (bufp && (TEST_DELAY_SMARTBUF_RELEASE & rand()) == 1) sleep(1);

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " bufp " << (uintptr_t)bufp << " assigned from smartbuf " << (uintptr_t)&s << " bufp " << (uintptr_t)sbufp;

	if (sbufp != bufp)
	{
		DecRef();
		buf.store(sbufp, std::memory_order_release);
		IncRef();
	}

	return *this;
}

SmartBuf::SmartBuf(SmartBuf&& s)
	: buf(s.buf.load(std::memory_order_acquire))
{
	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " move constructed from smartbuf " << (uintptr_t)&s << " bufp " << (uintptr_t)buf.load(std::memory_order_acquire);

	s.buf.store(NULL, std::memory_order_release);
}

SmartBuf& SmartBuf::operator= (SmartBuf&& s)
{
	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " bufp " << (uintptr_t)buf.load(std::memory_order_acquire) << " moved from smartbuf " << (uintptr_t)&s << " bufp " << (uintptr_t)s.buf.load(std::memory_order_acquire);

	DecRef();

	buf.store(s.buf.load(std::memory_order_acquire), std::memory_order_release);
	s.buf.store(NULL, std::memory_order_release);

	return *this;
}

void SmartBuf::ClearRef()
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (!bufp)
		return;

	if ((TEST_DELAY_SMARTBUF_RELEASE & rand()) == 1) sleep(1);

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " ClearRef bufp " << (uintptr_t)bufp;

	DecRef();
	buf.store(NULL, std::memory_order_release);
}

void SmartBuf::SetBasePtr(void* p)
{
	auto bufp = buf.load(std::memory_order_acquire);

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " bufp " << (uintptr_t)bufp << " SetBasePtr " << (uintptr_t)p;

	if (p != bufp)
	{
		DecRef();
		buf.store((std::uint8_t*)p, std::memory_order_release);
		IncRef();
	}
}

void* SmartBuf::BasePtr() const
{
	if (USE_SMARTBUF_GUARD) CheckGuard();

	return buf.load(std::memory_order_acquire);
}

SmartBuf::operator bool() const
{
	if (USE_SMARTBUF_GUARD) CheckGuard();

	return buf.load(std::memory_order_acquire);
}
