/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
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

class FastSpinLock
{
	std::atomic_flag mutex;

public:

	FastSpinLock()
	 :	mutex(ATOMIC_FLAG_INIT)
	{ }

	~FastSpinLock()
	{
		CCASSERTZ(mutex.test_and_set(std::memory_order_acquire));
	}

	void lock()
	{
		while (mutex.test_and_set(std::memory_order_acquire))
			 ;;;
	}

	void unlock()
	{
        mutex.clear(std::memory_order_release);
	}
};


class SpinLock	// re-entrant
{
	std::atomic_flag mutex;
	std::atomic<std::uint32_t> count;
	std::atomic<std::thread::id> threadid;	// value only used when count > 0

public:

	SpinLock()
	 :	mutex(ATOMIC_FLAG_INIT),
		count(0)
	{ }

	~SpinLock()
	{
		CCASSERTZ(mutex.test_and_set(std::memory_order_acquire));
		CCASSERTZ(count.load(std::memory_order_acquire));
	}

	void lock()
	{
		while (true)
		{
			while (mutex.test_and_set(std::memory_order_acquire))
				 ;;;

			std::cerr << "thread " << std::this_thread::get_id() << " SpinLock lock " << (std::uintptr_t)this << " owning thread " << threadid.load() << " count " << count.load() << std::endl;

			if (!count.load(std::memory_order_acquire))
			{
				count.store(1, std::memory_order_release);
				threadid.store(std::this_thread::get_id(), std::memory_order_release);
		        mutex.clear(std::memory_order_release);
				return;
			}

			if (threadid.load(std::memory_order_acquire) == std::this_thread::get_id())
			{
				count.fetch_add(1, std::memory_order_acq_rel);
		        mutex.clear(std::memory_order_release);
				return;
			}

	        mutex.clear(std::memory_order_release);

			std::atomic<std::uint32_t> spinner;

			for (unsigned i = 0; i < 20; ++i)
				spinner.fetch_add(1, std::memory_order_acq_rel);
		}
	}

	void unlock()
	{
			while (mutex.test_and_set(std::memory_order_acquire))
				 ;;;

			std::cerr << "thread " << std::this_thread::get_id() << " SpinLock unlock " << (std::uintptr_t)this << " owning thread " << threadid.load() << " count " << count.load() << std::endl;

			CCASSERT(threadid.load(std::memory_order_acquire) == std::this_thread::get_id());

			CCASSERT(count.fetch_sub(1, std::memory_order_acq_rel));

	        mutex.clear(std::memory_order_release);
	}
};
