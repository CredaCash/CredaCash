/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2020 Creda Software, Inc.
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

	unsigned next_index(unsigned val) const
	{
		++val;

		if (val >= m_maxelem)
			val -= m_maxelem;

		return val;
	}

	bool empty() const
	{
		return m_nextin == m_nextout;
	}

	unsigned size() const
	{
		int s = (int)m_nextin - (int)m_nextout;

		if (s < 0)
			s += m_maxelem;

		return s;
	}

	unsigned space() const
	{
		return m_maxelem - 1 - size();
	}

	bool full() const
	{
		auto nextin = next_index(m_nextin);

		return nextin == m_nextout;
	}

	bool push(void *obj, unsigned size)
	{
		CCASSERT(size == m_objsize);

		auto nextin = next_index(m_nextin);

		if (nextin == m_nextout)
			return true;

		//std::cerr << "ObjQueue::push m_nextin " << m_nextin << " nextin " << nextin << " m_maxelem " << m_maxelem << std::endl;

		memcpy(m_buf + m_nextin * m_objsize, obj, m_objsize);

		m_nextin = nextin;

		return false;
	}

	uint8_t* next(unsigned size) const
	{
		CCASSERT(size == m_objsize);

		if (empty())
			return NULL;

		auto obj = m_buf + m_nextout * m_objsize;

		return obj;
	}

	uint8_t* pop(unsigned size)
	{
		auto obj = next(size);

		if (obj)
		{
			//std::cerr << "ObjQueue::pop  pre-pop m_nextout " << m_nextout << " m_maxelem " << m_maxelem << std::endl;

			m_nextout = next_index(m_nextout);

			//std::cerr << "ObjQueue::pop post-pop m_nextout " << m_nextout << " m_maxelem " << m_maxelem << std::endl;
		}

		return obj;
	}
};
