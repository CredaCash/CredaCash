/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * Finally.hpp
*/

#pragma once

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>

class Finally
	: private boost::noncopyable
{
	boost::function<void()> m_callback;

public:

	Finally(boost::function<void()> callback)
	 :	m_callback(callback)
	{ }

	void Set(boost::function<void()> callback)
	{
		m_callback = callback;
	}

	void Clear()
	{
		m_callback = NULL;
	}

	~Finally()
	{
		if (m_callback)
			m_callback();
	}
};
