/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
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

#define TRACE_SPINLOCK	0

class FastSpinLock
{
	std::atomic_flag mutex;

public:

	FastSpinLock()
	{
        mutex.clear();
	}

	~FastSpinLock()
	{
		CCASSERTZ(mutex.test_and_set());
	}

	void lock()
	{
		if (TRACE_SPINLOCK) std::cerr << "FastSpinLock " << (uintptr_t)this << " locking..." << std::endl;

		while (mutex.test_and_set())
			;;;

		if (TRACE_SPINLOCK) std::cerr << "FastSpinLock " << (uintptr_t)this << " locked" << std::endl;
	}

	void unlock()
	{
		if (TRACE_SPINLOCK) std::cerr << "FastSpinLock " << (uintptr_t)this << " unlocking" << std::endl;

        mutex.clear();
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

			if (TRACE_SPINLOCK) std::cerr << "thread " << std::this_thread::get_id() << " SpinLock lock " << (std::uintptr_t)this << " owning thread " << threadid.load() << " count " << count.load() << std::endl;

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

			if (TRACE_SPINLOCK) std::cerr << "thread " << std::this_thread::get_id() << " SpinLock unlock " << (std::uintptr_t)this << " owning thread " << threadid.load() << " count " << count.load() << std::endl;

			CCASSERT(threadid.load() == std::this_thread::get_id());

			CCASSERT(count.fetch_sub(1));

			mutex.clear();
	}
};
