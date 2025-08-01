/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * SpinLock.hpp
*/

#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>
#include <iostream>

#include <CCassert.h>

#include "CCticks.hpp"

#define TRACE_SPINLOCK	0

//!#define RTEST_CUZZ_SPINLOCK		64

#ifndef RTEST_CUZZ_SPINLOCK
#define RTEST_CUZZ_SPINLOCK		0	// don't test
#endif

class FastSpinLock
{
	std::atomic_flag mutex;

	#if TRACE_SPINLOCK
	std::string file;
	unsigned line;
	std::uint64_t max_spin;
	#endif

public:

	FastSpinLock(const char *_file, unsigned _line)
	{
		mutex.clear();

		#if TRACE_SPINLOCK
		file = _file;
		line = _line;
		max_spin = 0;
		#endif
	}

	~FastSpinLock()
	{
		CCASSERTZ(mutex.test_and_set());
	}

	void lock()
	{
		#if TRACE_SPINLOCK
		//std::cout << "FastSpinLock " << (uintptr_t)this << " locking..." << std::endl;
		auto t0 = highres_ticks();
		std::uint64_t spin = 0;
		#endif

		while (mutex.test_and_set())
		#if TRACE_SPINLOCK
			++spin;
		#endif
			;;;

		#if TRACE_SPINLOCK
		auto dt = highres_ticks() - t0;
		if (spin > max_spin)
			max_spin = spin;
		if (spin >= max_spin/10*9 && report_highres_ticks(dt))
			std::cout << "FastSpinLock " << (uintptr_t)this << " " << file << " " << line << " locked in " << dt << " spin " << spin << " max_spin " << max_spin << std::endl;
		#endif
	}

	void unlock()
	{
		//if (TRACE_SPINLOCK) std::cout << "FastSpinLock " << (uintptr_t)this << " unlocking" << std::endl;

		mutex.clear();

		//if (RandTest(RTEST_CUZZ_SPINLOCK)) usleep(rand() & 63);
	}
};


class SpinLock	// re-entrant
{
	std::atomic_flag mutex;
	std::atomic<std::uint32_t> count;
	std::atomic<std::thread::id> threadid;	// value only used when count > 0

public:

	SpinLock()
	 :	count(0)
	{
		mutex.clear();
	}

	~SpinLock()
	{
		CCASSERTZ(mutex.test_and_set());
		CCASSERTZ(count.load());
	}

	void lock()
	{
		while (true)
		{
			while (mutex.test_and_set())
				;;;

			//if (TRACE_SPINLOCK) std::cout << "thread " << std::this_thread::get_id() << " SpinLock lock " << (std::uintptr_t)this << " owning thread " << threadid.load() << " count " << count.load() << std::endl;

			if (!count.load())
			{
				count.store(1);
				threadid.store(std::this_thread::get_id());
				mutex.clear();
				return;
			}

			if (threadid.load() == std::this_thread::get_id())
			{
				count.fetch_add(1);
				mutex.clear();
				return;
			}

			mutex.clear();

			std::atomic<std::uint32_t> spinner;

			for (unsigned i = 0; i < 20; ++i)
				spinner.fetch_add(1);
		}
	}

	void unlock()
	{
			while (mutex.test_and_set())
				;;;

			//if (TRACE_SPINLOCK) std::cout << "thread " << std::this_thread::get_id() << " SpinLock unlock " << (std::uintptr_t)this << " owning thread " << threadid.load() << " count " << count.load() << std::endl;

			CCASSERT(threadid.load() == std::this_thread::get_id());

			CCASSERT(count.fetch_sub(1));

			mutex.clear();

			if (RandTest(RTEST_CUZZ_SPINLOCK)) usleep(rand() & 63);
	}
};
