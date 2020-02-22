/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
 *
 * CCthread.hpp
*/

#pragma once

#include <thread>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>

class CCThread
	: private boost::noncopyable
{
public:
	std::thread *m_pthread;

	CCThread()
		: m_pthread(NULL)
	{ }

	virtual void Run(boost::function<void()> threadproc = NULL)
	{
		m_pthread = new std::thread(&CCThread::ThreadProc, this, threadproc);
	}

	virtual void ThreadProc(boost::function<void()> threadproc)
	{
		threadproc();
	}

	std::thread::id get_id() const
	{
		if (m_pthread)
			return m_pthread->get_id();
		else
			return std::thread::id();
	}

	virtual void join()
	{
		if (m_pthread)
		{
			m_pthread->join();
			m_pthread = NULL;
		}
	}

	virtual ~CCThread()
	{
		join();		// ensure thread is done before cleaning up any thread local storage
	}
};

class CCThreadFactory
{
public:
	virtual ~CCThreadFactory() = default;

	virtual CCThread* NewThread() const
	{
		return new CCThread;
	}
};

template <typename CT>
class CCThreadFactoryInstantiation : public CCThreadFactory
{
public:
	CCThread* NewThread() const
	{
		return new CT;
	}
};

extern CCThreadFactory CCDefaultThreadFac;
