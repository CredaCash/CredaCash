/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * AutoCount.hpp
*/

#pragma once

#include <boost/noncopyable.hpp>

class RefCounted
	: private boost::noncopyable
{
public:
	virtual ~RefCounted() = default;

	virtual bool IncRef() = 0;
	virtual void DecRef() = 0;
};

class AutoCount
{
	RefCounted *m_obj;

public:

	~AutoCount()
	{
		if (m_obj)
			m_obj->DecRef();
	}

	AutoCount()
		: m_obj(NULL)
	{ }

	AutoCount(AutoCount&& a)
		: m_obj(a.m_obj)
	{
		a.m_obj = NULL;
	}

	AutoCount(const AutoCount& a)
		: m_obj(NULL)
	{
		if (a.m_obj && !a.m_obj->IncRef())
			m_obj = a.m_obj;
	}

	AutoCount(RefCounted *obj)
		: m_obj(NULL)
	{
		if (obj && !obj->IncRef())
			m_obj = obj;
	}

	bool AcquireRef(RefCounted *obj)
	{
		CCASSERTZ(m_obj);

		if (!obj->IncRef())
		{
			m_obj = obj;
			return false;
		}

		return true;
	}

	operator bool() const
	{
		return m_obj;
	}
};
