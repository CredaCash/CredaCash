/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * SmartBuf.cpp
*/

#include "CCdef.h"
#include "SmartBuf.hpp"

#include <boost/log/trivial.hpp>

using namespace boost::log::trivial;

#define USE_SMARTBUF_GUARD		1

//#define RTEST_DELAY_SMARTBUF_RELEASE	32

#ifndef RTEST_DELAY_SMARTBUF_RELEASE
#define RTEST_DELAY_SMARTBUF_RELEASE	0	// don't test
#endif

#define SMARTBUF_GUARD	0x84758362
#define SMARTBUF_FREE	0x28472919

std::atomic<int64_t> bytecount(0);
std::atomic<unsigned> objcount(0);
std::atomic<unsigned> maxobjcount(0);
std::atomic<unsigned> maxrefcount(0);

int64_t SmartBuf::ByteTotal()
{
	return bytecount;
}

#if TRACE_SMARTBUF
SmartBuf::SmartBuf()
	: buf(NULL)
{
	//cerr << "SmartBuf " << (uintptr_t)this << " created empty" << endl;
	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " created empty";
}
#endif

SmartBuf::SmartBuf(size_t bufsize)
	: buf(NULL)
{
	if (!bufsize || bufsize > 258*1024*1024)
	{
		if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " created with no buffer";

		return;
	}

	auto msize = bufsize;
	msize += sizeof(refcount_t) + sizeof(nauxptrs_t) + 2 * USE_SMARTBUF_GUARD * sizeof(uint32_t);

	auto bufp = (uint8_t*)malloc(msize);

	buf.store(bufp);

	auto usize = size(bufp);

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " malloc'ed bufp " << (uintptr_t)bufp << " size " << bufsize << " useable size " << usize << " sizeof(refcount_t) " << sizeof(refcount_t) << " required alignment " << std::alignment_of<refcount_t>::value << " size of bufp " << sizeof(buf);

	if (bufp)
	{
		auto asize = alloc_size(bufp);

		CCASSERT(asize >= msize);
		CCASSERT(usize >= bufsize);

		memset(bufp, 0, asize);

		if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " zero'ed bufp " << (uintptr_t)bufp << " size " << asize;

		if (USE_SMARTBUF_GUARD)
		{
			*(uint32_t*)bufp = SMARTBUF_GUARD;

			*(uint32_t*)(bufp + asize - sizeof(uint32_t)) = SMARTBUF_GUARD;
		}

		SetRefCount(bufp, 1);

		if (USE_SMARTBUF_GUARD) CheckGuard(bufp);

		static unsigned t0 = 0;
		if (!t0)
			t0 = time(NULL);

		bytecount += asize;

		auto nobjs = objcount.fetch_add(1);
		if (!(nobjs & (127)))
		{
			if (nobjs > maxobjcount.load())
			{
				maxobjcount.store(nobjs);

				unsigned t = time(NULL);
				unsigned dt = t - t0;
				t0 = t;

				//cerr << "SmartBuf nobjs " << nobjs << " dt " << dt << endl;
				if (1 || TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(info) << "SmartBuf nobjs " << nobjs << " dt " << dt;	// !!! note "1+..." and "info"
			}
		}
	}
}

void SmartBuf::CheckGuard(const uint8_t* bufp, bool refcount_iszero) const
{
	//if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " CheckGuard bufp " << (uintptr_t)bufp;

	auto refcount = GetRefCount(bufp);

	auto guard = *(uint32_t*)bufp;
	if (guard != SMARTBUF_GUARD)
	{
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " CheckGuard bufp " << (uintptr_t)bufp << " bad front guard " << hex << guard << dec << " buffer size " << size(bufp) << " refcount " << refcount;
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(bufp, size(bufp));

		CCASSERT(0); raise(SIGTERM);
	}

	if (refcount_iszero && refcount != 0)
	{
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " CheckGuard bufp " << (uintptr_t)bufp << " refcount " << refcount << " is not zero; buffer size " << size(bufp);
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(bufp, size(bufp));

		CCASSERT(0); raise(SIGTERM);
	}

	if (!refcount_iszero && (refcount < 1 || refcount > 0xFFFF0000))
	{
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " CheckGuard bufp " << (uintptr_t)bufp << " refcount " << refcount << " invalid; buffer size " << size(bufp);
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(bufp, size(bufp));

		CCASSERT(0); raise(SIGTERM);
	}

	guard = *(uint32_t*)(bufp + alloc_size(bufp) - sizeof(uint32_t));
	if (guard != SMARTBUF_GUARD)
	{
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " CheckGuard bufp " << (uintptr_t)bufp << " bad back guard " << hex << guard << dec << " buffer size " << size(bufp) << " refcount " << refcount;
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(bufp, size(bufp));

		CCASSERT(0); raise(SIGTERM);
	}
}

size_t SmartBuf::alloc_size(const uint8_t* bufp)
{
#ifdef _WIN32
	return _msize((void*)bufp);
#else
	return malloc_usable_size((void*)bufp);
#endif
}

std::size_t SmartBuf::size() const
{
	auto bufp = buf.load();

	if (!bufp) return 0;

	if (USE_SMARTBUF_GUARD) CheckGuard(bufp);

	return size(bufp);
}

size_t SmartBuf::size(const uint8_t* bufp)
{
	auto asize = alloc_size(bufp);

	if (!asize)
		return 0;

	asize -= sizeof(refcount_t) + sizeof(nauxptrs_t) + 2 * USE_SMARTBUF_GUARD * sizeof(uint32_t);

	return asize;
}

uint8_t* SmartBuf::data(int refcount_iszero) const
{
	auto bufp = buf.load();

	if (!bufp) return NULL;

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " bufp " << (uintptr_t)bufp;

	if (USE_SMARTBUF_GUARD && refcount_iszero >= 0) CheckGuard(bufp, refcount_iszero);

	return bufp + sizeof(refcount_t) + sizeof(nauxptrs_t) + USE_SMARTBUF_GUARD * sizeof(uint32_t);
}

void SmartBuf::SetAuxPtrCount(unsigned count)
{
	auto bufp = buf.load();

	*((nauxptrs_t*)(bufp + sizeof(refcount_t) + USE_SMARTBUF_GUARD * sizeof(uint32_t))) = count;
}

unsigned SmartBuf::GetAuxPtrCount() const
{
	auto bufp = buf.load();

	auto count = *((nauxptrs_t*)(bufp + sizeof(refcount_t) + USE_SMARTBUF_GUARD * sizeof(uint32_t)));

	if (count > 20)
	{
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " GetAuxPtrCount bufp " << (uintptr_t)bufp << " bad aux count " << count << " buffer size " << size(bufp) << " refcount " << GetRefCount(bufp);
		BOOST_LOG_TRIVIAL(fatal) << "@ *** ERROR SmartBuf " << (uintptr_t)this << " buffer contents " << buf2hex(bufp, size(bufp));

		CCASSERT(0); raise(SIGTERM);
	}

	return count;
}

void SmartBuf::SetRefCount(const uint8_t* bufp, unsigned count)
{
	*((refcount_t*)(bufp + USE_SMARTBUF_GUARD * sizeof(uint32_t))) = count;
}

unsigned SmartBuf::GetRefCount(const uint8_t* bufp)
{
	return ((refcount_t*)(bufp + USE_SMARTBUF_GUARD * sizeof(uint32_t)))->load();
}

unsigned SmartBuf::IncRef()
{
	auto bufp = buf.load();

	if (!bufp) return 0;

	//if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " IncRef bufp " << (uintptr_t)bufp;

	if (USE_SMARTBUF_GUARD) CheckGuard(bufp);

	auto refcount = ((refcount_t*)(bufp + USE_SMARTBUF_GUARD * sizeof(uint32_t)))->fetch_add(1) + 1;

	if (!(refcount & 127) && refcount > maxrefcount.load())
	{
		maxrefcount.store(refcount);

		//cout << "SmartBuf max refcount " << refcount << endl;	// comment this out

		if (1 || TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(info) << "SmartBuf max refcount " << refcount;	// !!! note "1+..." and "info"
	}

	// debugging; don't use in production code:
	// if (refcount && !(refcount & 0x3fff)) BOOST_LOG_TRIVIAL(warning) << "SmartBuf " << (uintptr_t)this << " IncRef bufp " << (uintptr_t)bufp << " to refcount " << refcount << " buffer contents " << buf2hex(data(), size(bufp) < 256 ? size(bufp) : 256);

	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " IncRef bufp " << (uintptr_t)bufp << " to refcount " << refcount;

	return refcount;
}

unsigned SmartBuf::DecRef()
{
	auto bufp = buf.load();

	if (!bufp) return 0;

	//if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " DecRef bufp " << (uintptr_t)bufp;

	if (USE_SMARTBUF_GUARD) CheckGuard(bufp);

	auto refcount = ((refcount_t*)(bufp + USE_SMARTBUF_GUARD * sizeof(uint32_t)))->fetch_sub(1) - 1;

	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " DecRef bufp " << (uintptr_t)bufp << " to refcount " << refcount;

	if (!refcount)
	{
		if (bufp && RandTest(RTEST_DELAY_SMARTBUF_RELEASE)) sleep(1);

		auto asize = alloc_size(bufp);

		if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " freeing bufp " << (uintptr_t)bufp << " size " << asize;

		auto auxp = (void**)data(true);
		auto naux = GetAuxPtrCount();

		if (USE_SMARTBUF_GUARD)
		{
			*(uint32_t*)bufp = SMARTBUF_FREE;

			*(uint32_t*)(bufp + asize - sizeof(uint32_t)) = SMARTBUF_FREE;
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

		bytecount -= asize;

		auto nobjs = objcount.fetch_sub(1);
		(void)nobjs;
		//cout << "SmartBuf nobjs " << nobjs << endl;
	}

	return refcount;
}

SmartBuf::~SmartBuf()
{
	auto bufp = buf.load();

	if (!bufp) return;

	if (RandTest(RTEST_DELAY_SMARTBUF_RELEASE)) sleep(1);

	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " destructor bufp " << (uintptr_t)bufp;

	DecRef();
}

SmartBuf::SmartBuf(void* p)
	: buf((uint8_t*)p)
{
	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " constructed from pointer " << (uintptr_t)buf.load();

	IncRef();
}

SmartBuf::SmartBuf(const SmartBuf& s)
	: buf(s.buf.load())
{
	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " constructed from smartbuf " << (uintptr_t)&s << " bufp " << (uintptr_t)buf.load();

	IncRef();
}

SmartBuf& SmartBuf::operator= (const SmartBuf& s)
{
	auto bufp = buf.load();
	auto sbufp = s.buf.load();

	if (bufp && RandTest(RTEST_DELAY_SMARTBUF_RELEASE)) sleep(1);

	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " bufp " << (uintptr_t)bufp << " assigned from smartbuf " << (uintptr_t)&s << " bufp " << (uintptr_t)sbufp;

	if (sbufp != bufp)
	{
		DecRef();
		buf.store(sbufp);
		IncRef();
	}

	return *this;
}

SmartBuf::SmartBuf(SmartBuf&& s)
	: buf(s.buf.load())
{
	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " move constructed from smartbuf " << (uintptr_t)&s << " bufp " << (uintptr_t)buf.load();

	s.buf.store(NULL);
}

SmartBuf& SmartBuf::operator= (SmartBuf&& s)
{
	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " bufp " << (uintptr_t)buf.load() << " moved from smartbuf " << (uintptr_t)&s << " bufp " << (uintptr_t)s.buf.load();

	DecRef();

	buf.store(s.buf.load());
	s.buf.store(NULL);

	return *this;
}

void SmartBuf::ClearRef()
{
	auto bufp = buf.load();

	if (!bufp) return;

	if (RandTest(RTEST_DELAY_SMARTBUF_RELEASE)) sleep(1);

	if (0 && TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " ClearRef bufp " << (uintptr_t)bufp;

	DecRef();
	buf.store(NULL);
}

void SmartBuf::SetBasePtr(void* p)
{
	auto bufp = buf.load();

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " bufp " << (uintptr_t)bufp << " SetBasePtr " << (uintptr_t)p;

	if (p != bufp)
	{
		DecRef();
		buf.store((uint8_t*)p);
		IncRef();
	}
}

void* SmartBuf::BasePtr() const
{
	auto bufp = buf.load();

	if (!bufp) return NULL;

	if (TRACE_SMARTBUF) BOOST_LOG_TRIVIAL(debug) << "SmartBuf " << (uintptr_t)this << " BasePtr bufp " << (uintptr_t)bufp;

	if (USE_SMARTBUF_GUARD) CheckGuard(bufp);

	return bufp;
}
