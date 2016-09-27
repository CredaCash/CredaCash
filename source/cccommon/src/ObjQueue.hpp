/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * ObjQueue.hpp
*/

#pragma once

#include <cstdint>
#include <iostream>

class ObjQueue
{
	unsigned m_objsize;
	unsigned m_maxelem;
	uint8_t *m_buf;

	unsigned m_nextin;
	unsigned m_nextout;

public:
	ObjQueue(unsigned objsize, unsigned maxelem)
	{
		m_objsize = objsize;
		m_maxelem = maxelem + 1;	// +1 so we know when the queue is full

		m_buf = (uint8_t*)malloc(objsize * m_maxelem);

		clear();
	};

	~ObjQueue()
	{
		free(m_buf);
	}

	void clear()
	{
		m_nextin = 0;
		m_nextout = 0;
	}

	void increment(unsigned& val)
	{
		++val;

		if (val >= m_maxelem)
			val -= m_maxelem;
	}

	unsigned next(unsigned val)
	{
		increment(val);
		return val;
	}

	bool empty()
	{
		return m_nextin == m_nextout;
	}

	bool full()
	{
		auto nextin = next(m_nextin);

		return nextin == m_nextout;
	}

	unsigned size()
	{
		int s = (int)m_nextin - (int)m_nextout;

		if (s < 0)
			s += m_maxelem;

		return s;
	}

	unsigned space()
	{
		return m_maxelem - 1 - size();
	}

	bool push(void *obj)
	{
		auto nextin = next(m_nextin);

		if (nextin == m_nextout)
			return true;

		//std::cerr << "ObjQueue::push m_nextin " << m_nextin << " nextin " << nextin << " m_maxelem " << m_maxelem << std::endl;

		memcpy(m_buf + m_nextin * m_objsize, obj, m_objsize);

		m_nextin = nextin;

		return false;
	}

	uint8_t* pop()
	{
		if (m_nextin == m_nextout)
			return NULL;

		auto obj = m_buf + m_nextout * m_objsize;

		//std::cerr << "ObjQueue::pop  pre-pop m_nextout " << m_nextout << " m_maxelem " << m_maxelem << std::endl;

		increment(m_nextout);

		//std::cerr << "ObjQueue::pop post-pop m_nextout " << m_nextout << " m_maxelem " << m_maxelem << std::endl;

		return obj;
	}
};
